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

#include <sys/storage.h>
#include "pthread_key.h"

JEMALLOC_EXPORT
void *
pthread_getspecific(pthread_key_t const key)
{
    _key_data_t *data;
    int         rc;

    // Get the keys current sequence number
    uintptr_t const seqnum = _process_keys[key]._key_seqnum;

    // Get the thread-specific value.
    _key_table_t table = (_key_table_t)__tls()->__keydata;
    rc = _key_get(&table, key, &data, 0);
    if (rc != 0) {
        return NULL;
    }

    if (data->_key_seqnum != seqnum) {
        return NULL;
    }

    return (void *)data->_key_value;
}

#if defined(__QNXNTO__) && defined(__USESRCVERSION)
#include <sys/srcversion.h>
__SRCVERSION("$URL: http://f27svn.qnx.com/svn/repos/osr/trunk/jemalloc/dist/src/nto/pthread_getspecific.c $ $Rev: 2185 $")
#endif
