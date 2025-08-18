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

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <pthread.h>
#include <atomic.h>
#include <dlfcn.h>
#include <sys/neutrino.h>
#include <sys/storage.h>
#include "pthread_key.h"

/**
 * Clean up pthread keys on thread exit.
 *
 * @param   none
 * @return  none
 *
 * This function is called by `pthread_exit()` and is responsible for performing any clean up
 * required by the pthread keys implementation.
 *
 */
JEMALLOC_EXPORT
void
__pthread_key_exit(void)
{
    struct _thread_local_storage	* const tls = __tls();
    _key_thread_exit((_key_table_t)tls->__keydata);
}

#if defined(__QNX__) && (_NTO_VERSION < 710)
void
pthread_exit(void * const value_ptr)
{
    struct __cleanup_handler                *handler;
    int                                     again;
    volatile struct _thread_local_storage   * const tls = __tls();

    // Disable pthread_cancel and set cancel type to deferred
    atomic_set(&tls->__flags, PTHREAD_CANCEL_DISABLE);
    atomic_clr(&tls->__flags, PTHREAD_CANCEL_ASYNCHRONOUS); /* make defered */

    // call the cleanup handlers
    while((handler = tls->__cleanup)) {
        tls->__cleanup = handler->__next;
        handler->__routine(handler->__save);
    }

    // Call the thread_specific_data destructor functions
    __pthread_key_exit();

    // Unlock hanging file mutexes before exiting
    _Unlockfilemtx();

    // Unlock hanging system mutexes before exiting
    _Unlocksysmtx();

    // Clean up per-thread ldd data
    __ldd_data_cleanup();

    // Destroy the thread
    for (;;) {
        (void) ThreadDestroy_r(0, -1, value_ptr);
    }

}
#endif
#if defined(__QNXNTO__) && defined(__USESRCVERSION)
#include <sys/srcversion.h>
__SRCVERSION("$URL: http://f27svn.qnx.com/svn/repos/osr/trunk/jemalloc/dist/src/nto/pthread_exit.c $ $Rev: 2185 $")
#endif
