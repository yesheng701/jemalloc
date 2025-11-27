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
#include <semaphore.h>

static prof_record_t *prof_shm_ptr = NULL;

void navi_monitor_init(void) {
    int fd = open("/dev/shmem/jemalloc_prof", O_RDWR | O_CREAT, 0777);
    if (fd < 0) {
        malloc_printf("<jemalloc>: open failed: %d\n", errno);
        return;
    }

    sem_t *sem = sem_open("/jemalloc_prof_sem", O_CREAT, 0777, 1);
    if (sem == SEM_FAILED) {
        malloc_printf("<jemalloc>: sem_open failed: %d\n", errno);
        close(fd);
        return;
    }

    if (sem_wait(sem) != 0) {
        malloc_printf("<jemalloc>: sem_wait failed: %d\n", errno);
        sem_close(sem);
        close(fd);
        return;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        malloc_printf("<jemalloc>: fstat failed: %d\n", errno);
        goto cleanup;
    }

    if (st.st_size > 0 && st.st_size != sizeof(prof_record_t)) {
        malloc_printf("<jemalloc>: shm size mismatch (expected %zu, got %lld), recreating...\n", sizeof(prof_record_t), (long long)st.st_size);
        close(fd);
        unlink("/dev/shmem/jemalloc_prof");
        fd = open("/dev/shmem/jemalloc_prof", O_RDWR | O_CREAT, 0777);
        if (fd < 0) {
            malloc_printf("<jemalloc>: open failed during recreate: %d\n", errno);
            sem_post(sem);
            sem_close(sem);
            return;
        }
        st.st_size = 0;
    }

    if (st.st_size == 0) {
        if (fchmod(fd, 0777) != 0) {
            malloc_printf("<jemalloc>: fchmod failed: %d\n", errno);
            goto cleanup;
        }

        if (ftruncate(fd, sizeof(prof_record_t)) == -1) {
            malloc_printf("<jemalloc>: ftruncate failed: %d\n", errno);
            goto cleanup;
        }
    }

    void *ptr = mmap(NULL, sizeof(prof_record_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        malloc_printf("<jemalloc>: mmap failed: %d\n", errno);
        goto cleanup;
    }

    prof_shm_ptr = (prof_record_t *)ptr;

cleanup:
    sem_post(sem);
    sem_close(sem);
    close(fd);
}

prof_heap_info_t *navi_monitor_alloc_heap_info(const prof_cnt_t *cnt_all, int32_t thread_cnt) {
    if (prof_shm_ptr == NULL) {
        navi_monitor_init();
    }

    if (prof_shm_ptr != NULL) {
        uint64_t idx = atomic_fetch_add(&prof_shm_ptr->heap_cursor, 1) % MAX_HEAP_CNT;
        prof_heap_info_t *heap_info = &prof_shm_ptr->heaps[idx];

        heap_info->timestamp = (uint64_t)time(NULL);
        heap_info->pid = getpid();
        heap_info->sample = (uint8_t)lg_prof_sample;
        heap_info->interval = (uint8_t)opt_lg_prof_interval;
        heap_info->objs_all = (uint32_t)cnt_all->curobjs;
        heap_info->bytes_all = cnt_all->curbytes;
        heap_info->thread_cnt = 0;

        // Reserve thread slots in the global ring buffer
        uint64_t thread_start_idx = atomic_fetch_add(&prof_shm_ptr->thread_cursor, thread_cnt) % MAX_TOTAL_THREADS;
        heap_info->thread_offset = (int32_t)thread_start_idx;

        return heap_info;
    }
    return NULL;
}

void navi_monitor_add_thread(prof_heap_info_t *heap_info, int32_t tid, uint64_t objs, uint64_t bytes, const char *name) {
    if (heap_info != NULL) {
        int idx = (heap_info->thread_offset + heap_info->thread_cnt) % MAX_TOTAL_THREADS;
        prof_shm_ptr->thread_infos[idx].tid = (uint32_t)tid;
        prof_shm_ptr->thread_infos[idx].objs = (uint32_t)objs;
        prof_shm_ptr->thread_infos[idx].bytes = bytes;
        if (name != NULL) {
            strncpy(prof_shm_ptr->thread_infos[idx].name, name, sizeof(prof_shm_ptr->thread_infos[idx].name) - 1);
            prof_shm_ptr->thread_infos[idx].name[sizeof(prof_shm_ptr->thread_infos[idx].name) - 1] = '\0';
        } else {
            prof_shm_ptr->thread_infos[idx].name[0] = '\0';
        }
        heap_info->thread_cnt++;
    }
}

#endif /* NAVI_MONITOR */
