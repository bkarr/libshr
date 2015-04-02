#define _GNU_SOURCE
#include "shared_q.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/times.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>
#include <poll.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sched.h>
#include <syscall.h>
#include <time.h>
#include <pthread.h>
#include <limits.h>

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
#define CACHE_SIZE 64
#define QNAME "testq"

typedef struct proc_item pitem_t;
typedef void *(*thread_task_t)(void *);

struct proc_item
{
    int aff;
    int process;
    int id;
};

typedef enum test_type { VALIDATE, ENQ_DEQ } test_type_t;


static int64_t iterations;
static volatile unsigned long input = 0;
static volatile unsigned long output = 0;
static volatile unsigned long verif = 0;
static volatile unsigned long count1 = 0;
static volatile unsigned long count2 = 0;
static shr_q_s *queue;
static int waiting = 0;
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

void *enqueue_dequeue(
    void *arg
)   {
    cpu_set_t set;
    int i = 0;
    int j = 0;
    int id = (long)arg;
    int sys_cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    int cpu = id % sys_cpu_count;
    unsigned long *ptr;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
//    int count  = (int) (1000.0 * (random() / (RAND_MAX + 1.0)));
    int count  = 1000;

    if (sched_setaffinity(GETTID(), sizeof(cpu_set_t), &set) < 0) {
        fprintf(stderr, "setting cpu affinity for producer failed errno:%i\n",
            errno);
        exit(1);
    }

    assert(wait() == 0);

    shr_q_s *q = queue;
    ptr = (unsigned long *)malloc(CACHE_SIZE);
    assert(ptr);
    void *buffer = NULL;
    int64_t size = 0;
    for (i = 0; i < iterations; ++i) {
        while (shr_q_add(q, (void *)ptr, CACHE_SIZE) != SQ_OK)
            ;
        for (j = 0; j < count; j++)
            ;
        sq_item_s item = {.status = SQ_ERR_EMPTY};
        while (item.status != SQ_OK) {
            item = shr_q_remove(q, (void**)&buffer, &size);
        }
        assert(item.value);
        for (j = 0; j < count; j++)
            ;
    }
    return NULL;
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
    sq_status_e status = shr_q_open(&q, QNAME, SQ_WRITE_ONLY);
    assert(status == SQ_OK);
#endif
    assert(wait() == 0);
    for (i = 0; i < iterations; ++i) {
        ptr = (unsigned long *)malloc(CACHE_SIZE);
        assert(ptr);
        //printf("add %lx\n", (uint64_t)ptr);
        *ptr = AAF(&input, 1);
        //printf("%li\n", *ptr);
        total += *ptr;
        while (shr_q_add(q, (void *)ptr, CACHE_SIZE) != SQ_OK)
            printf("add failed\n");
    }
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
        sq_status_e status = shr_q_open(&q, QNAME, SQ_READ_ONLY);
        assert(status == SQ_OK);
    #endif
    assert(wait() == 0);
    void *buffer = NULL;
    int64_t size = 0;
    for (i = 0; i < iterations; ++i) {
        sq_item_s item = {.status = SQ_ERR_EMPTY};
        while (item.status != SQ_OK) {
            item = shr_q_remove(q, &buffer, &size);
            if (item.status != SQ_OK && item.status != SQ_ERR_EMPTY)
                printf("remove failed %i\n", errno);
        }
        assert(item.value);
        ptr = item.value;
        if (ptr) {
            //printf("remove %lx\n", (uint64_t)ptr);
            total += *ptr;
            AAF(&count1, 1);
            ptr = 0;
        }
        AAF(&count2, 1);
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
    sq_status_e result = SQ_OK;
    pitem_t *pitem;
    shr_q_s *q;
    void *buffer = NULL;
    int64_t size = 0;
    int i;
    int j;
    result = shr_q_create(&q, QNAME, limit, NULL, SQ_READWRITE);
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

int main(
    int argc,
    char **argv
)   {
    sq_status_e result = SQ_OK;
    int i;
    int j;
    pthread_t *t;
    int64_t thread_count;
    int cpu_count;
    int sys_cpu_count;
    int total;
    struct timespec start;
    struct timespec end;
    struct timespec diff;
    thread_task_t producer;
    thread_task_t consumer;
    test_type_t type;
    struct timespec sleep = {0, 10000000};
    int rc;

    srandom(time(NULL));

    remove("/dev/shm/testq");

    if (argc != 5) {
        fprintf(stderr, "%s: <testtype> <ncpus> <nthreads> <iterations>\n",
                argv[0]);
        return 1;
    }


    if (memcmp(argv[1], "validate", 8) == 0) {
        type = VALIDATE;
        producer = validate_producer;
        consumer = validate_consumer;
    } else if (memcmp(argv[1], "enqdeq", 6) == 0) {
        type = ENQ_DEQ;
        producer = enqueue_dequeue;
        consumer = enqueue_dequeue;
    } else {
        fprintf(stderr, "%s: invalid test type\n", argv[0]);
        return 1;
    }


    verif = 0;
    sys_cpu_count = sysconf(_SC_NPROCESSORS_ONLN);

    iterations = atol(argv[4]);
    thread_count = atoi(argv[3]);
    cpu_count = atoi(argv[2]);
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
    if (type == VALIDATE && total > 1 && total % 2) {
        fprintf(stderr, "%s: need an even number of threads\n", argv[0]);
        return 1;
    }

    if (type == VALIDATE && cpu_count == 1 && thread_count == 1) {
        validate_basic_queue(iterations);
    } else {
        result = shr_q_create(&queue, QNAME, INT_MAX, NULL, SQ_READWRITE);
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
