#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <signal.h>

#define MAX_HEAP_CNT 100000
#define MAX_TOTAL_THREADS 300000
#define SHM_PATH "/dev/shmem/jemalloc_prof"

volatile sig_atomic_t keep_running = 1;

void handle_sigint(int sig) {
    keep_running = 0;
}

typedef struct prof_thread_info_s {
    uint64_t bytes;
    uint32_t objs;
    uint32_t tid;
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
    volatile uint64_t heap_cursor;
    volatile uint64_t thread_cursor;
    prof_heap_info_t heaps[MAX_HEAP_CNT];
    prof_thread_info_t thread_infos[MAX_TOTAL_THREADS];
} prof_record_t;

void print_records(prof_record_t *record, uint64_t start, uint64_t end) {
    for (uint64_t i = start; i < end; i++) {
        uint64_t idx = i % MAX_HEAP_CNT;
        prof_heap_info_t *heap = &record->heaps[idx];

        if (heap->timestamp == 0) continue;

        time_t ts = (time_t)heap->timestamp;
        struct tm *tm_info = localtime(&ts);
        char time_buf[64];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

        printf("[%lu] Time: %s | PID: %u | Total Bytes: %lu | Total Objs: %u | Threads: %u | Sample: %u | Interval: %u\n",
               i, time_buf, heap->pid, heap->bytes_all, heap->objs_all, heap->thread_cnt, heap->sample, heap->interval);

        for (int j = 0; j < heap->thread_cnt; j++) {
            int t_idx = (heap->thread_offset + j) % MAX_TOTAL_THREADS;
            prof_thread_info_t *thread = &record->thread_infos[t_idx];
            printf("    Thread [%u] (TID: %u): %lu bytes, %u objs\n",
                   j, thread->tid, thread->bytes, thread->objs);
        }
        printf("\n");
    }
}

int main(int argc, char *argv[]) {
    signal(SIGINT, handle_sigint);

    int fd = -1;
    prof_record_t *record = NULL;
    uint64_t last_cursor = 0;
    struct stat st;
    ino_t current_inode = 0;

    while (keep_running) {
        // Check if file exists and get stats
        if (stat(SHM_PATH, &st) == -1) {
            if (fd != -1) {
                printf("Shared memory file removed. Waiting for recreation...\n");
                munmap(record, sizeof(prof_record_t));
                close(fd);
                fd = -1;
                record = NULL;
            }
            sleep(1);
            continue;
        }

        // Check if file replaced (inode changed) or not opened yet
        if (fd == -1 || st.st_ino != current_inode) {
            if (fd != -1) {
                printf("Shared memory file replaced. Reopening...\n");
                munmap(record, sizeof(prof_record_t));
                close(fd);
            }

            fd = open(SHM_PATH, O_RDONLY);
            if (fd < 0) {
                perror("open");
                sleep(1);
                continue;
            }

            if (st.st_size != sizeof(prof_record_t)) {
                printf("Warning: File size mismatch. Expected %zu, got %ld. Waiting...\n", sizeof(prof_record_t), st.st_size);
                close(fd);
                fd = -1;
                sleep(1);
                continue;
            }

            record = (prof_record_t *)mmap(NULL, sizeof(prof_record_t), PROT_READ, MAP_SHARED, fd, 0);
            if (record == MAP_FAILED) {
                perror("mmap");
                close(fd);
                fd = -1;
                sleep(1);
                continue;
            }

            current_inode = st.st_ino;
            // On first open or reopen, start from the beginning of the valid history buffer
            // to print existing data, then continue monitoring.
            uint64_t current = record->heap_cursor;
            if (current > MAX_HEAP_CNT) {
                last_cursor = current - MAX_HEAP_CNT;
            } else {
                last_cursor = 0;
            }
            printf("Monitoring started. Current cursor: %lu. Replaying history from: %lu\n", current, last_cursor);
        }

        uint64_t current_cursor = record->heap_cursor;

        if (current_cursor < last_cursor) {
            printf("Counter reset detected (Old: %lu, New: %lu). Resetting...\n", last_cursor, current_cursor);
            last_cursor = 0;
        }

        if (current_cursor > last_cursor) {
            print_records(record, last_cursor, current_cursor);
            last_cursor = current_cursor;
        }

        sleep(1);
    }

    if (fd != -1) {
        munmap(record, sizeof(prof_record_t));
        close(fd);
    }
    printf("\nExiting...\n");

    return 0;
}
