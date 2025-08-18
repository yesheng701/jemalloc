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

#ifndef _PTHREAD_KEY_INCLUDED
#define _PTHREAD_KEY_INCLUDED

#include <pthread.h>
#include <limits.h>
#include <stdint.h>
#include <sys/neutrino.h>

typedef struct
{
    void const  *_key_value;
    uintptr_t   _key_seqnum;
}   _key_data_t;

#define _KEY_NONE			((_key_destructor_t)-1)

typedef _key_data_t         *_key_table_t;
typedef void				(* _key_destructor_t)(void *);

extern _key_data_t          _process_keys[PTHREAD_KEYS_MAX];
extern void					_key_delete(pthread_key_t);

int _key_get(_key_table_t *tablep, pthread_key_t key, _key_data_t **datap,
             int create);
void _key_delete_table(_key_table_t table);
void _key_thread_exit(_key_table_t table);

#endif

#if defined(__QNX__) && (_NTO_VERSION >= 710)
void __pthread_key_exit(void);
#endif

#if defined(__QNXNTO__) && defined(__USESRCVERSION)
#include <sys/srcversion.h>
__SRCVERSION("$URL: http://f27svn.qnx.com/svn/repos/osr/trunk/jemalloc/dist/src/nto/pthread_key.h $ $Rev: 2185 $")
#endif
