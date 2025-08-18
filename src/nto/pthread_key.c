/*
 * $QNXLicenseC:
 * Copyright 2007, 2019, QNX Software Systems. All Rights Reserved.
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

#include <jemalloc/internal/jemalloc_preamble.h>
#include <jemalloc/jemalloc_macros.h>

#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <pthread.h>
#include <process.h>
#include "pthread_key.h"

static pthread_mutex_t key_mutex = PTHREAD_MUTEX_INITIALIZER;

#define ULONG_BITS      (sizeof(unsigned long) * 8)

/**
 * Bit mask of used keys.
 */
static unsigned long    used_keys[PTHREAD_KEYS_MAX / ULONG_BITS];

JEMALLOC_EXPORT
int
pthread_key_create(pthread_key_t * const key, void (*destructor)(void *))
{
    int ret = EOK;

    // Do not allow a value of _KEY_NONE, which is used to indicate that a key
    // is not valid.
    if (destructor == _KEY_NONE) {
        return EINVAL;
    }

    ret = pthread_mutex_lock(&key_mutex);
    if (ret != EOK) {
        printf("lock failed: %d\n", ret);
        abort();
    }

    // Find an available key.
    pthread_key_t   newkey = PTHREAD_KEYS_MAX;
    for (unsigned i = 0; i < PTHREAD_KEYS_MAX / ULONG_BITS; i++) {
        const unsigned long   free_keys = ~used_keys[i];
        if (free_keys != 0) {
            const unsigned    idx = __builtin_ffsl(free_keys) - 1;
            newkey = (pthread_key_t)(idx + (i * ULONG_BITS));
            used_keys[i] |= (1UL << idx);
            break;
        }
    }

    if (newkey >= PTHREAD_KEYS_MAX) {
        pthread_mutex_unlock(&key_mutex);
        return EAGAIN;
    }

    // Install the destructor
    _process_keys[newkey]._key_value = (void *)destructor;
    if (_process_keys[newkey]._key_seqnum == 0) {
        _process_keys[newkey]._key_seqnum = 1;
    }

    pthread_mutex_unlock(&key_mutex);
    *key = newkey;
    return 0;
}


JEMALLOC_EXPORT 
int
pthread_key_delete(pthread_key_t const key)
{
    if (pthread_mutex_lock(&key_mutex) != EOK) {
        abort();
    }

    // Invalidate the key
    _process_keys[key]._key_value = _KEY_NONE;
    _process_keys[key]._key_seqnum += 1;

    used_keys[(unsigned long)key / ULONG_BITS] &= ~(1UL << ((unsigned long)key % ULONG_BITS));

    pthread_mutex_unlock(&key_mutex);
    return EOK;
}

/**
 * Call the destructor associated with the given key.
 *
 * @param   table   The table of thread-specific values
 * @param   key     The key to destroy
 * @returns         true if destructor was called, false otherwise
 *
 */
static bool
call_destructor(_key_table_t table, const pthread_key_t key)
{
    _key_data_t *data;
    int         rc;

    _key_destructor_t const destructor = _process_keys[key]._key_value;
    uintptr_t const          seqnum = _process_keys[key]._key_seqnum;

    if (destructor == NULL) {
        return false;
    }

    // Get the thread-specific value.
    rc = _key_get(&table, key, &data, 0);
    if (rc != 0) {
        return false;
    }

    if (data->_key_value == NULL) {
        return false;
    }

    // Check if the thread-specific value was installed for this version of the
    // key.
    if (data->_key_seqnum != seqnum) {
        return false;
    }

    // POSIX states that we must first NULL the key_value before calling the destructor on the
    // previous value (see POSIX documentation for pthread_key_create()).
    void * old_key_value = data->_key_value;
    data->_key_value = NULL;
    destructor(old_key_value);

    return true;
}

/**
 * Cleanup TLS on thread exit.
 *
 * @param  table   The table of thread-specific values to cleanup
 *
 */
void
_key_thread_exit(const _key_table_t table)
{
    // POSIX states that we must invoke the destructor on non-NULL values at most
    // PTHREAD_DESTRUCTOR_ITERATIONS times. If non-NULL values still exist then the behaviour is
    // implementation defined.
    for (unsigned iter = 0; iter < PTHREAD_DESTRUCTOR_ITERATIONS; iter++) {
        bool destroy_again = false;
        pthread_key_t key_base = 0;

        // Invoke the destructor function for every defined key.
        for (unsigned i = 0; i < PTHREAD_KEYS_MAX / ULONG_BITS; i++) {
            if (used_keys[i] != 0) {
                for (unsigned j = 0; j < ULONG_BITS; j++) {
                    if ((used_keys[i] & (1UL << j)) != 0) {
                        pthread_key_t const key = key_base + j;
                        bool destructor_called = call_destructor(table, key);

                        if (destructor_called) {
                            destroy_again = true;
                        }
                    }
                }
            }

            key_base += ULONG_BITS;
        }

        // If destroy_again is false then no destructors were called. This likely means all
        // thread-specific values have been set to NULL.
        if (destroy_again == false) {
            break;
        }
    }

    _key_delete_table(table);
}

#if defined(__QNXNTO__) && defined(__USESRCVERSION)
#include <sys/srcversion.h>
__SRCVERSION("$URL: http://f27svn.qnx.com/svn/repos/osr/trunk/jemalloc/dist/src/nto/pthread_key.c $ $Rev: 2053 $")
#endif
