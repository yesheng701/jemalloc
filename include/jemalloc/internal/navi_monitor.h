#ifndef JEMALLOC_INTERNAL_NAVI_MONITOR_H
#define JEMALLOC_INTERNAL_NAVI_MONITOR_H

#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#ifdef NAVI_MONITOR

#define MAX_THREAD_CNT 300
#define MAX_HEAP_CNT 10000
#define MAX_TOTAL_THREADS 300000

typedef struct prof_thread_info_s {
    uint64_t bytes;
    uint32_t objs;
    uint32_t tid;
    char name[16];
} prof_thread_info_t;

typedef struct prof_heap_info_s {
    uint64_t timestamp;
    uint64_t bytes_all;
    uint32_t objs_all;
    uint32_t pid;
    uint32_t thread_offset;
    uint16_t thread_cnt;
    uint8_t sample;
    uint8_t interval;
} prof_heap_info_t;

typedef struct prof_record_s {
    _Atomic uint64_t heap_cursor;
    _Atomic uint64_t thread_cursor;
    prof_heap_info_t heaps[MAX_HEAP_CNT];
    prof_thread_info_t thread_infos[MAX_TOTAL_THREADS];
} prof_record_t;

void navi_monitor_init(void);
prof_heap_info_t *navi_monitor_alloc_heap_info(const prof_cnt_t *cnt_all, int32_t thread_cnt);
void navi_monitor_add_thread(prof_heap_info_t *heap_info, int32_t tid, uint64_t objs, uint64_t bytes, const char *name);

#endif /* NAVI_MONITOR */

#endif /* JEMALLOC_INTERNAL_NAVI_MONITOR_H */
