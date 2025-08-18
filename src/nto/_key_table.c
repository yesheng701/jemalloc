/*
 * $QNXLicenseC:
 * Copyright 2019, QNX Software Systems. All Rights Reserved.
 *
 * You must obtain a written license from and pay applicable license fees to QNX
 * Software Systems before you may reproduce, modify or distribute this software,
 * or any work that includes all or part of this software.   Free development
 * licenses are available for evaluation and non-commercial purposes.  For more
 * information visit http://licensing.qnx.com or email licensing@qnx.com.
 *
 * This file may contain contributions from others.  Please review this entire
 * file for other proprietary rights or license notices, as well as the QNX
 * Development Suite License Guide at http://licensing.qnx.com/license-guide/
 * for other information.
 * $
 */

/**
 * @file    _key_table.c
 *
 * @brief   Support for the POSIX pthread key API
 *
 * This is a malloc-free implementation of the pthread key API, which supports
 * the following functions:
 *
 * - pthread_key_create()
 * - pthread_key_delete()
 * - pthread_setspecific()
 * - pthread_getspecific()
 *
 * The main data structure is a table (_key_table_t), which is simply a
 * fixed-sized array of _key_data_t. A table is used for implementing both the
 * process-wide set of keys, each with a destructor function, and the per-thread
 * set of values associated with each key.
 *
 * In order to support the maximum number of keys (PTHREAD_KEYS_MAX) the table
 * can potentially comprise two levels. A first level table is divided in two,
 * with the first part referencing key data directly while the second part
 * points to second-level tables. Note that the second part of an L1 table is
 * tightly packed.
 *
 *     +------+
 *     | val0 |
 *     +------+
 *     | seq0 |
 *     +------+
 *     | val1 |
 *     +------+
 *     | seq1 |
 *     +------+
 *     | ...  |
 *     +------+
 *     | valN |
 *     +------+
 *     | seqN |
 *     +------+
 *     | L2_0 |  ---------> +------+
 *     +------+             | valK |
 *     | L2_1 |             +------+
 *     +------+             | seqK |
 *     | ...  |             +------+
 *     +------+             | ...  |
 *     | L2_M |             +------+
 *     +------+
 *
 * This layout is optimized for the common case
 * of a small number of keys:
 *
 * - No extra memory is used for a process that doesn't use any keys (other than
 *   the per-process and per-thread table pointers, which are set to NULL).
 * - A process with 1 to (3/4 TABLE_KEYS) keys uses one table for the process and
 *   one table for each thread that defines a specific value for a given key.
 * - A process with more than 3/4 TABLE_KEYS keys employs the necessary number
 *   of tables, with a small overhead for the L2 pointers.
 *
 * One of the major objectives of this implementation is to avoid calls to
 * malloc(), as several heap implementations use the pthread key API for
 * per-thread pools. The use of fixed-sized tables allows us to implement the
 * API with a simple slab allocator that only depends on mmap().
 *
 * The slab is implemented as a circular, doubly-linked list. slab_head points
 * to the head of the list and slab_head->sh_prev to the tail. The allocator
 * maintains the invariant that if there are any free objects then slab_head is
 * guaranteed to have at least one such object. This invariant is maintained by:
 *
 * 1. A new page is always linked at the head
 * 2. Any page that becomes empty is moved to the tail
 * 3. Any empty page that becomes non-empty is moved to the head
 *
 */

#include <jemalloc/internal/jemalloc_internal_decls.h>

#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <pthread.h>
#include <string.h>
#include <sys/mman.h>
#include "pthread_key.h"
#include <assert.h>

// The number of keys in a table needs to be at least
// ceil(sqrt(PTHREAD_KEYS_MAX*2)). The current value works for a maximum value
// of 128, but will have to be modified if the maximum changes.
// This is verified with a static assertion in the code.
#define TABLE_KEYS      16u
#define TABLE_SIZE      (TABLE_KEYS * sizeof(_key_data_t))
#define L2_TABLES       (TABLE_KEYS / 2)
#define L1_KEYS         (TABLE_KEYS * 3 / 4)
#define MAX_KEYS        (L1_KEYS + (L2_TABLES * TABLE_KEYS))

// The number of objects in a slab page.
// The first object is used for the slab header.
#define SLAB_OBJS       ((__PAGESIZE / TABLE_SIZE) - 1)

// The mask set when all objects are free.
// Note that the calculation works even if SLAB_OBJS is 32.
#define SLAB_FREEMASK   ((1u << SLAB_OBJS) - 1)

typedef struct slab_header  slab_header_t;
struct slab_header
{
    slab_header_t   *sh_next;
    slab_header_t   *sh_prev;
    uint32_t        sh_freemask;
};

static  slab_header_t       *slab_head;

/**
 * Adds a page to the slab allocator's linked list at a given position.
 * @param   page    The page to link
 * @param   next    The list position
 */
static void
slab_link(slab_header_t * const page, slab_header_t * const next)
{
    page->sh_next = next;
    page->sh_prev = next->sh_prev;
    page->sh_prev->sh_next = page;
    page->sh_next->sh_prev = page;
}

/**
 * Removes a page from the slab allocator's linked list.
 * @param   page    The page to link
 */
static void
slab_unlink(slab_header_t * const page)
{
    page->sh_next->sh_prev = page->sh_prev;
    page->sh_prev->sh_next = page->sh_next;
}

/**
 * Converts a one-based index in a slab page to the corresponding table object.
 * @param   page    The page that holds the object
 * @param   idx     The object's position
 * @return  A table object
 */
static inline void *
slab_to_obj(slab_header_t const * const page, const unsigned idx)
{
    uintptr_t   ptr = (uintptr_t)page;
    ptr += (idx * TABLE_SIZE);
    return (void *)ptr;
}

/**
 * Adds a new slab page to the allocator.
 * @return  1 if successful, 0 otherwise
 */
static int
slab_replenish(void)
{
    // Slab allocator assumes 4k page size
    assert(__PAGESIZE == 0x1000);

    slab_header_t * const   slab_page = mmap(NULL, __PAGESIZE,
                                             PROT_READ | PROT_WRITE,
                                             MAP_PRIVATE | MAP_ANON,
                                             NOFD,
                                             0);

    if (slab_page == MAP_FAILED) {
        // mmap() failed, but another thread could have replenished the
        // allocator in the mean time.
        if ((slab_head == NULL) || (slab_head->sh_freemask == 0)) {
            return 0;
        }

        return 1;
    }

    // Mark all objects as free.
    slab_page->sh_freemask = SLAB_FREEMASK;

    // Link as the new head.
    if (slab_head == NULL) {
        slab_page->sh_next = slab_page;
        slab_page->sh_prev = slab_page;
    } else {
        slab_link(slab_page, slab_head);
    }

    slab_head = slab_page;
    return 1;
}

/**
 * Allocates a table object.
 * @return  The newly allocated object if successful, NULL otherwise
 */
static _key_table_t
slab_alloc(void)
{
    // Try to get an element from the head page. If the head page is empty than
    // the entire list is empty.
    if ((slab_head == NULL) || (slab_head->sh_freemask == 0)) {
        if (!slab_replenish()) {
            return NULL;
        }
    }

    // Slab free mask is too small
    assert(SLAB_OBJS <= 32);

    // Find a free object and mark it as allocated.
    // Note that __builtin_ffs() returns a 1-based index, which works with
    // slab_to_obj(), but the free mask needs a 0-based index.
    unsigned const          freeidx = __builtin_ffs(slab_head->sh_freemask);
    _key_table_t const      table = slab_to_obj(slab_head, freeidx);
    slab_head->sh_freemask &= ~(1u << (freeidx - 1));

    // Move an empty slab to the tail of the list. Since the list is circular
    // and we are removing the object from the head, all that needs to be done
    // is move the head pointer.
    if (slab_head->sh_freemask == 0) {
        slab_head = slab_head->sh_next;
    }

    memset(table, 0, TABLE_SIZE);
    return table;
}

/**
 * Frees a table object.
 * @param   table   The object to free
 */
static void
slab_free(_key_table_t const table)
{
    // Get the slab page and index for the object.
    uintptr_t const            object = (uintptr_t)table;
    slab_header_t * const   slab_page = (void *)(object & ((uintptr_t)~(__PAGESIZE - 1)));
    unsigned long const        objidx = ((object & (uintptr_t)(__PAGESIZE - 1)) / TABLE_SIZE) - 1;

    // Move a previously-empty slab page to the head of the list.
    if ((slab_page->sh_freemask == 0) && (slab_page != slab_head)) {
        slab_unlink(slab_page);
        slab_link(slab_page, slab_head);
        slab_head = slab_page;
    }

    // Mark the object as free.
    slab_page->sh_freemask |= (1u << objidx);

    // Remove a fully free slab page, but only if the next one is also fully
    // free (to avoid excessive calls to mmap()/munmap()).
    if ((slab_page->sh_freemask == SLAB_FREEMASK)
        && (slab_page->sh_next != slab_page)) {
        if (slab_page == slab_head) {
            slab_head = slab_page->sh_next;
        }
        slab_unlink(slab_page);

        // Mark the page for unampping.
        slab_page->sh_next = NULL;
    }

    if (slab_page->sh_next == NULL) {
        munmap(slab_page, __PAGESIZE);
    }
}

/**
 * Sets a L2 table from the given L1 table and key.
 * @param   table   The L1 table
 * @param   key     The requested key
 * @param   l2_table l2_table
 * @return  void
 *
 * Note: It is assumed that `key` is in the range `L1_KEYS <= key <
 * PTHREAD_MAX_KEYS`.
 */
static inline void
set_l2_table(
  _key_table_t const table, pthread_key_t const key, _key_table_t l2_table) {
    unsigned const l1_idx = ((key - L1_KEYS) / TABLE_KEYS);
    // unsigned const l2_idx = (key - L1_KEYS) % TABLE_KEYS;
    void **const base = (void **)&table[L1_KEYS];
    _key_table_t *l2_table_ptr = (_key_table_t *)&base[l1_idx];

    *l2_table_ptr = l2_table;
    return;
}

/**
 * Finds a L2 table from the given L1 table and key.
 * @param   table   The L1 table
 * @param   key     The requested key
 * @param   l2_idxp Holds the L2 index for the key, upon return
 * @return  A pointer to the L1 slot that references the L2 table matching the
 *          key.
 *
 * Note: It is assumed that `key` is in the range `L1_KEYS <= key < PTHREAD_MAX_KEYS`.
 */
static inline _key_table_t *
get_l2_table(_key_table_t const table, pthread_key_t const key, unsigned * const l2_idxp)
{
    unsigned const  l1_idx = ((key - L1_KEYS) / TABLE_KEYS);
    unsigned const  l2_idx = (key - L1_KEYS) % TABLE_KEYS;
    void ** const   base = (void **)&table[L1_KEYS];

    *l2_idxp = l2_idx;
    return (_key_table_t *)&base[l1_idx];
}

/**
 * Finds (and potentially creates) a key data object in the given table.
 * @param   tablep  A pointer to an L1 table. If NULL and create is non-zero the
 *                  function will attempt to allocate a new table.
 * @param   key     The requested key
 * @param   datap   Holds a pointer to the found data object, upon successful
 *                  return
 * @param   create  If non-zero, the function will attempt to create any tables
 *                  needed to store a data object for the key
 * @retval  EOK     Successful
 * @retval  ERANGE  Unsupported key
 * @retval  ENOMEM  Failed to create tables (only if create is non-zero)
 * @retval  ESRCH   The key cannot be found (only if create is 0)
 */
int
_key_get(_key_table_t * tablep, pthread_key_t const key,
         _key_data_t ** const datap, int const create)
{
    _Static_assert(MAX_KEYS >= PTHREAD_KEYS_MAX, "TABLE_KEYS is too small");

    unsigned const  l1_idx = (unsigned)key;

    // Sanity check.
    // Callers should already be checking for bad values.
    if (l1_idx >= PTHREAD_KEYS_MAX) {
        return ERANGE;
    }

    // Get the L1 table.
    _key_table_t    table = *tablep;
    if (table == NULL) {
        if (!create) {
            return ESRCH;
        }

        table = slab_alloc();
        if (table == NULL) {
            return ENOMEM;
        }

        *tablep = table;
    }

    // Handle L1-direct keys.
    if (l1_idx < L1_KEYS) {
        *datap = &table[l1_idx];
        return EOK;
    }

    // Get the L2 table.
    unsigned                    l2_idx = 0;
    _key_table_t const * const  l2_table_ptr = get_l2_table(table, (pthread_key_t)l1_idx, &l2_idx);
    _key_table_t                l2_table = *l2_table_ptr;

    if (l2_table == NULL) {
        if (!create) {
            return ESRCH;
        }

        l2_table = slab_alloc();
        if (l2_table == NULL) {
            return ENOMEM;
        }
        if (create) {
            set_l2_table(table, key, l2_table);
        }
    }

    *datap = &l2_table[l2_idx];
    return EOK;
}

/**
 * Frees all memory allocated for the given table.
 * @param   table   The table to delete
 */
void
_key_delete_table(_key_table_t const table)
{
    if (table == NULL) {
        return;
    }

    void ** const base = (void **)&table[L1_KEYS];

    for (unsigned i = 0; i < L2_TABLES; i++) {
        _key_table_t const l2_table = base[i];
        if (l2_table != NULL) {
            slab_free(l2_table);
        }
    }

    slab_free(table);
}

#if defined(__QNXNTO__) && defined(__USESRCVERSION)
#include <sys/srcversion.h>
__SRCVERSION("$URL: http://f27svn.qnx.com/svn/repos/osr/trunk/jemalloc/dist/src/nto/_key_table.c $ $Rev: 2053 $")
#endif
