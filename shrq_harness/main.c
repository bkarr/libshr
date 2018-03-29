/*
The MIT License (MIT)

Copyright (c) 2015 Bryan Karr

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

#define _GNU_SOURCE
#include <shared_q.h>

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>

#define GETTID() (syscall(__NR_gettid))

# define timespecsub(a, b, result)                                            \
  do {                                                                        \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;                             \
    (result)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec;                          \
    if ((result)->tv_nsec < 0) {                                              \
      --(result)->tv_sec;                                                     \
      (result)->tv_nsec += 1000000000;                                        \
    }                                                                         \
  } while (0)

#define AAF __sync_add_and_fetch
#define DEFAULT_SIZE 32
#define QNAME "testq"

typedef struct proc_item pitem_t;
typedef void *(*thread_task_t)(void *);

struct proc_item
{
    int aff;
    int process;
    int id;
};

static long iterations;
static volatile unsigned long input = 0;
static volatile unsigned long output = 0;
static volatile unsigned long verif = 0;
static shr_q_s *queue;
static int waiting = 0;
static long msg_size = DEFAULT_SIZE;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;


static int wait(
) {
    int rc = pthread_mutex_lock(&mutex);
    if (rc) {
        return rc;
    }
    waiting++;
    rc = pthread_cond_wait(&cond, &mutex);
    if (rc) {
        return rc;
    }
    waiting--;
    pthread_mutex_unlock(&mutex);
    return rc;
}


void *validate_producer(
    void *arg
)   {
    cpu_set_t set;
    int i = 0;
    int id = (long)arg;
    int sys_cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    int cpu = id % sys_cpu_count;
    unsigned long *ptr;
    unsigned long total = 0;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);

    if (sched_setaffinity(GETTID(), sizeof(cpu_set_t), &set) < 0) {
        fprintf(stderr, "setting cpu affinity for producer failed errno:%i\n",
            errno);
        exit(1);
    }


#ifdef MTHRD
    shr_q_s *q = queue;
#else
    shr_q_s *q = NULL;
    sh_status_e status = shr_q_open(&q, QNAME, SQ_WRITE_ONLY);
    assert(status == SH_OK);
#endif
    assert(wait() == 0);
    ptr = (unsigned long *)malloc(msg_size);
    assert(ptr);
    for (i = 0; i < iterations; ++i) {
        *ptr = AAF(&input, 1);
        total += *ptr;
        while (shr_q_add(q, (void *)ptr, msg_size) != SH_OK)
            printf("add failed\n");
    }
    free(ptr);
    AAF(&verif, total);
    #ifndef MTHRD
        shr_q_close(&q);
    #endif
    return NULL;
}

void *validate_consumer(
    void *arg
)   {
    cpu_set_t set;
    int i = 0;
    int id = (long)arg;
    int sys_cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    int cpu = id % sys_cpu_count;
    unsigned long *ptr = 0;
    unsigned long total = 0;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);

    if (sched_setaffinity(GETTID(), sizeof(cpu_set_t), &set) < 0) {
        fprintf(stderr, "setting cpu affinity for consumer failed errno:%i\n",
            errno);
        exit(1);
    }


#ifdef MTHRD
    shr_q_s *q = queue;
#else
    shr_q_s *q = NULL;
    sh_status_e status = shr_q_open(&q, QNAME, SQ_READ_ONLY);
    assert(status == SH_OK);
#endif
    assert(wait() == 0);
    for (i = 0; i < iterations; ++i) {
        sq_item_s item = {.status = SH_ERR_EMPTY};
        while (item.status != SH_OK) {
            item = shr_q_remove(q, &item.buffer, &item.buf_size);
            if (item.status != SH_OK && item.status != SH_ERR_EMPTY)
                printf("remove failed %i\n", errno);
        }
        assert(item.value);
        ptr = item.value;
        if (ptr) {
            total += *ptr;
            ptr = 0;
        }
    }
    AAF(&output, total);
#ifndef MTHRD
    shr_q_close(&q);
#endif
    return NULL;
}


void validate_basic_queue(
    int limit
)   {
    sh_status_e result = SH_OK;
    pitem_t *pitem;
    shr_q_s *q;
    void *buffer = NULL;
    size_t size = 0;
    int i;
    int j;
    result = shr_q_create(&q, QNAME, limit, SQ_READWRITE);
    for (j = 0; j < 2 && !result; j++) {
        for (i = 0; i < limit; i++) {
            pitem = calloc(1, sizeof(pitem_t));
            if (pitem != NULL) {
                pitem->id = i + 1;
                result = shr_q_add(q, pitem, sizeof(pitem_t));
                if (result) {
                    printf("enqueue failed\n");
                } else {
                    printf("enqueue successful id: %i\n", pitem->id);
                }
            } else {
                printf("item create failed\n");
            }
        }

        for (i = 0; i < limit; i++) {
            pitem = NULL;
            sq_item_s item = shr_q_remove(q, (void**)&buffer, &size);
            if (item.status) {
                printf("queue remove failed\n");
            } else {
                pitem = item.value;
                if (pitem != NULL) {
                    printf("dequeue successful id: %i\n", pitem->id);
                } else {
                    printf("dequeue returned NULL\n");
                }
            }

        }
    }
    shr_q_destroy(&q);
}

static long parse_arg_to_long(
    char *string,
    int arg_no
)   {
    long result = 0;
    char *end;

    result = strtol(string, &end, 10);
    if (*string != '\0' && *end == '\0') {
        return result;
    }
    printf("argument %i is an invalid number\n", arg_no);
    exit(0);
}

int main(
    int argc,
    char **argv
)   {
    sh_status_e result = SH_OK;
    int i;
    int j;
    pthread_t *t;
    long thread_count;
    int cpu_count;
    int sys_cpu_count;
    int total;
    struct timespec start;
    struct timespec end;
    struct timespec diff;
    thread_task_t producer;
    thread_task_t consumer;
    struct timespec sleep = {0, 10000000};
    int rc;

    srandom(time(NULL));

    (void)remove("/dev/shm/testq");

    if (argc < 4 || argc > 5) {
        fprintf(stderr, "%s: <ncpus> <nthreads> <iterations> [<size>]\n",
                argv[0]);
        return 1;
    }

    producer = validate_producer;
    consumer = validate_consumer;

    verif = 0;
    sys_cpu_count = sysconf(_SC_NPROCESSORS_ONLN);

    if (argc == 5) {
        msg_size = parse_arg_to_long(argv[4], 4);
    }
    iterations = parse_arg_to_long(argv[3], 3);
    thread_count = parse_arg_to_long(argv[2], 2);
    cpu_count = parse_arg_to_long(argv[1], 1);
    total = cpu_count * thread_count;
    if (cpu_count < 1) {
        fprintf(stderr, "%s: need at least 1 cpu\n", argv[0]);
        return 1;
    }
    if (cpu_count > sys_cpu_count) {
        fprintf(stderr, "%s: cannot exceed system cpu count\n", argv[0]);
        return 1;
    }
    if (thread_count < 1) {
        fprintf(stderr, "%s: need at least 1 thread\n", argv[0]);
        return 1;
    }
    if (total > 1 && total % 2) {
        fprintf(stderr, "%s: need an even number of threads\n", argv[0]);
        return 1;
    }

    if (cpu_count == 1 && thread_count == 1) {
        validate_basic_queue(iterations);
    } else {
        result = shr_q_create(&queue, QNAME, INT_MAX, SQ_READWRITE);
        if (result) {
            printf("unable to create queue, mutex, or cond\n");
        } else {
            t = (pthread_t *)calloc(total, sizeof(pthread_t));
            for (i = 0; i < cpu_count; ++i) {
                for (j = 0; j < thread_count; j++) {
                    if (j % 2) {
                        assert(
                            !pthread_create(
                                &(t[(i * thread_count) + j]),
                                NULL,
                                (i % 2) ? producer : consumer,
                                (void*)((long)i)
                            )
                        );
                    } else {
                        assert(
                            !pthread_create(
                                &(t[(i * thread_count) + j]),
                                NULL,
                                (i % 2) ? consumer : producer,
                                (void*)((long)i)
                            )
                        );
                    }
                }
            }
            while (waiting < total)
                {};
            do {
                rc  = nanosleep(&sleep, &sleep);
            } while (rc < 0 && errno == EINTR);
            clock_gettime(CLOCK_REALTIME, &start);
            pthread_cond_broadcast(&cond);
            for (i = 0; i < total; ++i) {
                assert(!pthread_join(t[i], NULL));
            }
            clock_gettime(CLOCK_REALTIME, &end);
            printf("input SUM[0..%lu]=%lu output=%lu\n", input, verif, output);
            timespecsub(&end, &start, &diff);
            printf("time:  %lu.%04lu\n", diff.tv_sec, diff.tv_nsec / 100000);
        }
        shr_q_destroy(&queue);
    }
    return 0;
}
