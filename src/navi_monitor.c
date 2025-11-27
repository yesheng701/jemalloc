#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/navi_monitor.h"

#ifdef NAVI_MONITOR
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdatomic.h>
#include <errno.h>

static prof_record_t *prof_shm_ptr = NULL;

void navi_monitor_init(void) {
    int fd = open("/dev/shmem/jemalloc_prof", O_RDWR | O_CREAT | O_EXCL, 0777);
    if (fd < 0) {
        malloc_printf("<jemalloc>: open failed: %d\n", errno);
        return;
    }

    if (fchmod(fd, 0777) != 0) {
        malloc_printf("<jemalloc>: fchmod failed: %d\n", errno);
        close(fd);
        return;
    }

    if (ftruncate(fd, sizeof(prof_record_t)) == -1) {
        malloc_printf("<jemalloc>: ftruncate failed: %d\n", errno);
        close(fd);
        return;
    }

    void *ptr = mmap(NULL, sizeof(prof_record_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        malloc_printf("<jemalloc>: mmap failed: %d\n", errno);
        close(fd);
        return;
    }

    prof_shm_ptr = (prof_record_t *)ptr;
    // Don't close fd immediately if we want to keep it mapped?
    // Actually mmap keeps the reference.
    close(fd);
}

prof_heap_info_t *navi_monitor_alloc_heap_info(const prof_cnt_t *cnt_all) {
    if (prof_shm_ptr == NULL) {
        navi_monitor_init();
    }

    if (prof_shm_ptr != NULL) {
        uint64_t idx = atomic_fetch_add(&prof_shm_ptr->cursor, 1) % MAX_HEAP_CNT;
        prof_heap_info_t *heap_info = &prof_shm_ptr->heaps[idx];

        heap_info->timestamp = (uint64_t)time(NULL);
        heap_info->pid = getpid();
        heap_info->sample = (uint8_t)lg_prof_sample;
        heap_info->interval = 0;
        heap_info->objs_all = (uint32_t)cnt_all->curobjs;
        heap_info->bytes_all = cnt_all->curbytes;
        heap_info->thread_cnt = 0;

        return heap_info;
    }
    return NULL;
}

void navi_monitor_add_thread(prof_heap_info_t *heap_info, int32_t tid, uint64_t objs, uint64_t bytes) {
    if (heap_info != NULL && heap_info->thread_cnt < MAX_THREAD_CNT) {
        int idx = heap_info->thread_cnt;
        heap_info->threads[idx].tid = tid;
        heap_info->threads[idx].objs = objs;
        heap_info->threads[idx].bytes = bytes;
        heap_info->thread_cnt++;
    }
}

#endif /* NAVI_MONITOR */
