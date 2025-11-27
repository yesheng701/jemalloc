#ifndef JEMALLOC_INTERNAL_NAVI_MONITOR_H
#define JEMALLOC_INTERNAL_NAVI_MONITOR_H

#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#ifdef NAVI_MONITOR

#define MAX_THREAD_CNT 300
#define MAX_HEAP_CNT 10000

typedef struct prof_thread_info_s {
    int32_t tid;
    uint64_t objs;
    uint64_t bytes;
} prof_thread_info_t;

typedef struct prof_heap_info_s {
    uint64_t timestamp;
    int32_t pid;
    uint8_t sample;
    uint8_t interval;
    uint32_t objs_all;
    uint64_t bytes_all;
    int32_t thread_cnt;
    prof_thread_info_t threads[MAX_THREAD_CNT];
} prof_heap_info_t;

typedef struct prof_record_s {
    _Atomic uint64_t cursor;
    prof_heap_info_s heaps[MAX_HEAP_CNT];
} prof_record_t;

void navi_monitor_init(void);
prof_heap_info_t *navi_monitor_alloc_heap_info(const prof_cnt_t *cnt_all);
void navi_monitor_add_thread(prof_heap_info_t *heap_info, int32_t tid, uint64_t objs, uint64_t bytes);

#endif /* NAVI_MONITOR */

#endif /* JEMALLOC_INTERNAL_NAVI_MONITOR_H */
