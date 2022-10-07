/*
The MIT License (MIT)

Copyright (c) 2017 Bryan Karr

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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "shared_int.h"
#include "shared_q.h"

long adds;
long events;

static void sig_usr(int signo)
{
    if (signo == SIGUSR1) {
        adds++;
    } else if (signo == SIGUSR2) {
        events++;
    }
}


static void set_signal_handlers(void)
{
    if (signal(SIGUSR1, sig_usr) == SIG_ERR) {
        printf("cannot catch SIGUSR1\n");
    }
    if (signal(SIGUSR2, sig_usr) == SIG_ERR) {
        printf("cannot catch SIGUSR1\n");
    }
}


static void test_create_error_paths(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;

    shm_unlink("testq");
    status = shr_q_create(&q, "testq", UINT_MAX, SQ_IMMUTABLE);
    assert(status == SH_ERR_ARG);
    assert(q == NULL);
    status = shr_q_create(&q, "testq", -1, SQ_IMMUTABLE);
    assert(status == SH_ERR_ARG);
    assert(q == NULL);
    status = shr_q_create(&q, NULL, 1, SQ_READ_ONLY);
    assert(status == SH_ERR_ARG);
    assert(q == NULL);
    status = shr_q_create(&q, NULL, 1, SQ_WRITE_ONLY);
    assert(status == SH_ERR_ARG);
    assert(q == NULL);
    status = shr_q_create(&q, NULL, 1, SQ_IMMUTABLE);
    assert(status == SH_ERR_ARG);
    assert(q == NULL);
    status = shr_q_create(&q, "/fake/testq", 1, SQ_IMMUTABLE);
    assert(status == SH_ERR_PATH);
    assert(q == NULL);
    status = shr_q_create(&q, "fake/testq", 1, SQ_IMMUTABLE);
    assert(status == SH_ERR_PATH);
    assert(q == NULL);
    int fd = shm_open("/test",  O_RDWR | O_CREAT, FILE_MODE);
    assert(fd > 0);
    status = shr_q_create(&q, "/test", 1, SQ_IMMUTABLE);
    assert(status == SH_ERR_EXIST);
    assert(q == NULL);
    assert(shm_unlink("/test") == 0);
}

static void test_create_namedq(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shr_q_s *pq = NULL;

    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    assert(q != NULL);
    status = shr_q_destroy(NULL);
    assert(status == SH_ERR_ARG);
    assert(q != NULL);
    status = shr_q_destroy(&pq);
    assert(status == SH_ERR_ARG);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}

static void test_monitor(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;

    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    assert(q != NULL);
    status = shr_q_monitor(q, -1);
    assert(status == SH_ERR_ARG);
    status = shr_q_monitor(q, 0);
    assert(status == SH_OK);
    status = shr_q_monitor(q, SIGURG);
    assert(status == SH_OK);
    status = shr_q_monitor(q, SIGUSR1);
    assert(status == SH_OK);
    status = shr_q_monitor(q, 0);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}

static void test_listen(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;

    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    assert(q != NULL);
    status = shr_q_listen(q, -1);
    assert(status == SH_ERR_ARG);
    status = shr_q_listen(q, 0);
    assert(status == SH_OK);
    status = shr_q_listen(q, SIGURG);
    assert(status == SH_OK);
    status = shr_q_listen(q, SIGUSR1);
    assert(status == SH_OK);
    status = shr_q_listen(q, 0);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}

static void test_call(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;

    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    assert(q != NULL);
    status = shr_q_call(q, -1);
    assert(status == SH_ERR_ARG);
    status = shr_q_call(q, 0);
    assert(status == SH_OK);
    status = shr_q_call(q, SIGURG);
    assert(status == SH_OK);
    status = shr_q_call(q, SIGUSR1);
    assert(status == SH_OK);
    status = shr_q_call(q, 0);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}


static void test_open_close(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;

    // close error conditions
    status = shr_q_close(NULL);
    assert(status == SH_ERR_ARG);
    status = shr_q_close(&q);
    assert(status == SH_ERR_ARG);

    // open error conditions
    status = shr_q_open(NULL, "testq", SQ_READWRITE);
    assert(status == SH_ERR_ARG);
    status = shr_q_open(&q, NULL, SQ_READWRITE);
    assert(status == SH_ERR_ARG);
    status = shr_q_open(&q, "badq", SQ_READWRITE);
    assert(status == SH_ERR_EXIST);

    // successful open and close
    status = shr_q_open(&q, "testq", SQ_READWRITE);
    assert(status == SH_OK);
    assert(q != NULL);
    status = shr_q_close(&q);
    assert(status == SH_OK);
    assert(q == NULL);
    status = shr_q_open(&q, "testq", SQ_READ_ONLY);
    assert(status == SH_OK);
    assert(q != NULL);
    status = shr_q_close(&q);
    assert(status == SH_OK);
    assert(q == NULL);
    status = shr_q_open(&q, "testq", SQ_WRITE_ONLY);
    assert(status == SH_OK);
    assert(q != NULL);
    status = shr_q_close(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}

static void test_add(void)
{
    sh_status_e status;
    sq_item_s item;
    long length = 4;
    size_t size = length;
    char *msg = calloc(1, length);
    void *buffer = msg;
    shr_q_s *q = NULL;
    status = shr_q_open(&q, "testq", SQ_READWRITE);
    assert(status == SH_OK);
    status = shr_q_add(q, "test", 4);
    assert(status == SH_OK);
    status = shr_q_add(q, "test1", 5);
    assert(status == SH_ERR_LIMIT);
    item = shr_q_remove(q, &buffer, &size);
    assert(item.status == SH_OK);
    assert(buffer != msg);
    assert(item.length == 4);
    assert(memcmp(item.value, "test", item.length) == 0);
    status = shr_q_add(q, "test1", 5);
    assert(status == SH_OK);
    item = shr_q_remove(q, &buffer, &size);
    assert(item.status == SH_OK);
    assert(item.length == 5);
    assert(memcmp(item.value, "test1", item.length) == 0);
    status = shr_q_close(&q);
    assert(status == SH_OK);
    assert(q == NULL);
    free(buffer);
}

static void test_add_errors(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shr_q_s *tq = NULL;

    status = shr_q_add(q, "test", 4);
    assert(status == SH_ERR_ARG);
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    status = shr_q_add(q, "test", 4);
    assert(status == SH_ERR_STATE);
    status = shr_q_open(&tq, "testq", SQ_READ_ONLY);
    assert(status == SH_OK);
    status = shr_q_add(q, "test", 4);
    assert(status == SH_ERR_STATE);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_open(&tq, "testq", SQ_WRITE_ONLY);
    assert(status == SH_OK);
    status = shr_q_add(tq, NULL, 4);
    assert(status == SH_ERR_ARG);
    status = shr_q_add(tq, "test", 0);
    assert(status == SH_ERR_ARG);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
}

static void test_add_wait_errors(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shr_q_s *tq = NULL;

    status = shr_q_add_wait(q, "test", 4);
    assert(status == SH_ERR_ARG);
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    status = shr_q_add_wait(q, "test", 4);
    assert(status == SH_ERR_STATE);
    status = shr_q_open(&tq, "testq", SQ_READ_ONLY);
    assert(status == SH_OK);
    status = shr_q_add_wait(q, "test", 4);
    assert(status == SH_ERR_STATE);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_open(&tq, "testq", SQ_WRITE_ONLY);
    assert(status == SH_OK);
    status = shr_q_add_wait(tq, NULL, 4);
    assert(status == SH_ERR_ARG);
    status = shr_q_add_wait(tq, "test", 0);
    assert(status == SH_ERR_ARG);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
}

static void test_add_timedwait_errors(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shr_q_s *tq = NULL;
    struct timespec ts = {0};

    status = shr_q_add_timedwait(q, "test", 4, &ts);
    assert(status == SH_ERR_ARG);
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    status = shr_q_add_timedwait(q, "test", 4, &ts);
    assert(status == SH_ERR_STATE);
    status = shr_q_open(&tq, "testq", SQ_READ_ONLY);
    assert(status == SH_OK);
    status = shr_q_add_timedwait(q, "test", 4, &ts);
    assert(status == SH_ERR_STATE);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_open(&tq, "testq", SQ_WRITE_ONLY);
    assert(status == SH_OK);
    status = shr_q_add_timedwait(tq, NULL, 4, &ts);
    assert(status == SH_ERR_ARG);
    status = shr_q_add_timedwait(tq, "test", 0, &ts);
    assert(status == SH_ERR_ARG);
    status = shr_q_add_timedwait(tq, "test", 4, NULL);
    assert(status == SH_ERR_ARG);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
}

static void test_remove_errors(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shr_q_s *tq = NULL;
    sq_item_s item;

    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_ERR_ARG);
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_ERR_STATE);
    status = shr_q_open(&tq, "testq", SQ_WRITE_ONLY);
    assert(status == SH_OK);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_ERR_STATE);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_open(&tq, "testq", SQ_READ_ONLY);
    assert(status == SH_OK);
    item = shr_q_remove(tq, NULL, &item.buf_size);
    assert(item.status == SH_ERR_ARG);
    item = shr_q_remove(q, &item.buffer, NULL);
    assert(item.status == SH_ERR_ARG);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    free(item.buffer);
}

static void test_remove_wait_errors(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shr_q_s *tq = NULL;
    sq_item_s item;

    item = shr_q_remove_wait(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_ERR_ARG);
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    item = shr_q_remove_wait(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_ERR_STATE);
    status = shr_q_open(&tq, "testq", SQ_WRITE_ONLY);
    assert(status == SH_OK);
    item = shr_q_remove_wait(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_ERR_STATE);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_open(&tq, "testq", SQ_READ_ONLY);
    assert(status == SH_OK);
    item = shr_q_remove_wait(tq, NULL, &item.buf_size);
    assert(item.status == SH_ERR_ARG);
    item = shr_q_remove_wait(q, &item.buffer, NULL);
    assert(item.status == SH_ERR_ARG);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    free(item.buffer);
}

static void test_remove_timedwait_errors(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shr_q_s *tq = NULL;
    sq_item_s item;
    struct timespec ts = {0};

    item = shr_q_remove_timedwait(q, &item.buffer, &item.buf_size, &ts);
    assert(item.status == SH_ERR_ARG);
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    item = shr_q_remove_timedwait(q, &item.buffer, &item.buf_size, &ts);
    assert(item.status == SH_ERR_STATE);
    status = shr_q_open(&tq, "testq", SQ_WRITE_ONLY);
    assert(status == SH_OK);
    item = shr_q_remove_timedwait(q, &item.buffer, &item.buf_size, &ts);
    assert(item.status == SH_ERR_STATE);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_open(&tq, "testq", SQ_READ_ONLY);
    assert(status == SH_OK);
    item = shr_q_remove_timedwait(tq, NULL, &item.buf_size, &ts);
    assert(item.status == SH_ERR_ARG);
    item = shr_q_remove_timedwait(q, &item.buffer, NULL, &ts);
    assert(item.status == SH_ERR_ARG);
    item = shr_q_remove_timedwait(q, &item.buffer, &item.buf_size, NULL);
    assert(item.status == SH_ERR_ARG);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    free(item.buffer);
}


static void test_is_valid(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shm_unlink("testq");
    assert(!shr_q_is_valid(NULL));
    assert(!shr_q_is_valid(""));
    assert(!shr_q_is_valid("testq"));
    int fd = shm_open("testq", O_RDWR | O_CREAT | O_EXCL, FILE_MODE);
    assert(fd > 0);
    int rc = ftruncate(fd, PAGE_SIZE >> 1);
    assert(rc == 0);
    assert(!shr_q_is_valid("testq"));
    rc = ftruncate(fd, PAGE_SIZE);
    assert(rc == 0);
    assert(!shr_q_is_valid("testq"));
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    assert(shr_q_is_valid("testq"));
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
}

static void test_addv_errors(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shr_q_s *tq = NULL;
    sq_vec_s vector[2] = {{0, 0}, {0, 0}};

    status = shr_q_addv(q, vector, 1);
    assert(status == SH_ERR_ARG);
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    status = shr_q_addv(q, NULL, 1);
    assert(status == SH_ERR_ARG);
    status = shr_q_addv(q, vector, 0);
    assert(status == SH_ERR_ARG);
    status = shr_q_addv(q, vector, 1);
    assert(status == SH_ERR_STATE);
    status = shr_q_open(&tq, "testq", SQ_READ_ONLY);
    assert(status == SH_OK);
    status = shr_q_addv(tq, vector, 1);
    assert(status == SH_ERR_STATE);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_open(&tq, "testq", SQ_WRITE_ONLY);
    assert(status == SH_OK);
    status = shr_q_addv(tq, vector, 1);
    assert(status == SH_ERR_ARG);
    vector[0].base = "test";
    status = shr_q_addv(tq, vector, 1);
    assert(status == SH_ERR_ARG);
    vector[0].base = NULL;
    vector[0].len = 4;
    status = shr_q_addv(tq, vector, 1);
    assert(status == SH_ERR_ARG);
    vector[0].base = "test";
    vector[0].len = 4;
    vector[1].base = "test1";
    vector[1].len = 0;
    status = shr_q_addv(tq, vector, 2);
    assert(status == SH_ERR_ARG);
    vector[1].base = NULL;
    vector[1].len = 5;
    status = shr_q_addv(tq, vector, 2);
    assert(status == SH_ERR_ARG);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
}

static void test_addv_wait_errors(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shr_q_s *tq = NULL;
    sq_vec_s vector[2] = {{0, 0}, {0, 0}};

    status = shr_q_addv_wait(q, vector, 1);
    assert(status == SH_ERR_ARG);
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    status = shr_q_addv_wait(q, NULL, 1);
    assert(status == SH_ERR_ARG);
    status = shr_q_addv_wait(q, vector, 0);
    assert(status == SH_ERR_ARG);
    status = shr_q_addv_wait(q, vector, 1);
    assert(status == SH_ERR_STATE);
    status = shr_q_open(&tq, "testq", SQ_READ_ONLY);
    assert(status == SH_OK);
    status = shr_q_addv_wait(tq, vector, 1);
    assert(status == SH_ERR_STATE);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_open(&tq, "testq", SQ_WRITE_ONLY);
    assert(status == SH_OK);
    status = shr_q_addv_wait(tq, vector, 1);
    assert(status == SH_ERR_ARG);
    vector[0].base = "test";
    status = shr_q_addv_wait(tq, vector, 1);
    assert(status == SH_ERR_ARG);
    vector[0].base = NULL;
    vector[0].len = 4;
    status = shr_q_addv_wait(tq, vector, 1);
    assert(status == SH_ERR_ARG);
    vector[0].base = "test";
    vector[0].len = 4;
    vector[1].base = "test1";
    vector[1].len = 0;
    status = shr_q_addv_wait(tq, vector, 2);
    assert(status == SH_ERR_ARG);
    vector[1].base = NULL;
    vector[1].len = 5;
    status = shr_q_addv_wait(tq, vector, 2);
    assert(status == SH_ERR_ARG);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
}

static void test_addv_timedwait_errors(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shr_q_s *tq = NULL;
    struct timespec ts = {0};
    sq_vec_s vector[2] = {{0, 0}, {0, 0}};

    status = shr_q_addv_timedwait(q, vector, 1, &ts);
    assert(status == SH_ERR_ARG);
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    status = shr_q_addv_timedwait(q, NULL, 1, &ts);
    assert(status == SH_ERR_ARG);
    status = shr_q_addv_timedwait(q, vector, 0, &ts);
    assert(status == SH_ERR_ARG);
    status = shr_q_addv_timedwait(q, vector, 1, NULL);
    assert(status == SH_ERR_ARG);
    status = shr_q_addv_timedwait(q, vector, 1, &ts);
    assert(status == SH_ERR_STATE);
    status = shr_q_open(&tq, "testq", SQ_READ_ONLY);
    assert(status == SH_OK);
    status = shr_q_addv_timedwait(tq, vector, 1, &ts);
    assert(status == SH_ERR_STATE);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_open(&tq, "testq", SQ_WRITE_ONLY);
    assert(status == SH_OK);
    status = shr_q_addv_timedwait(tq, vector, 1, &ts);
    assert(status == SH_ERR_ARG);
    vector[0].base = "test";
    status = shr_q_addv_timedwait(tq, vector, 1, &ts);
    assert(status == SH_ERR_ARG);
    vector[0].base = NULL;
    vector[0].len = 4;
    status = shr_q_addv_timedwait(tq, vector, 1, &ts);
    assert(status == SH_ERR_ARG);
    vector[0].base = "test";
    vector[0].len = 4;
    vector[1].base = "test1";
    vector[1].len = 0;
    status = shr_q_addv_timedwait(tq, vector, 2, &ts);
    assert(status == SH_ERR_ARG);
    vector[1].base = NULL;
    vector[1].len = 5;
    status = shr_q_addv_timedwait(tq, vector, 2, &ts);
    assert(status == SH_ERR_ARG);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
}


static void test_clean(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    struct timespec limit = {0, 10000000};
    struct timespec sleep = {0, 20000000};
    struct timespec max = {1, 0};
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 0, SQ_READWRITE);
    assert(status == SH_OK);
    assert(q != NULL);
    status = shr_q_add(q, "test", 4);
    assert(status == SH_OK);
    assert(shr_q_count(q) == 1);
    while (nanosleep(&sleep, &sleep) < 0) {
        if (errno != EINTR) {
            break;
        }
    }
    assert(shr_q_clean(NULL, &limit) == SH_ERR_ARG);
    assert(shr_q_clean(q, NULL) == SH_ERR_ARG);
    assert(shr_q_clean(q, &limit) == SH_OK);
    assert(shr_q_count(q) == 0);
    status = shr_q_add(q, "test1", 5);
    assert(status == SH_OK);
    status = shr_q_add(q, "test2", 5);
    assert(status == SH_OK);
    assert(shr_q_count(q) == 2);
    while (nanosleep(&sleep, &sleep) < 0) {
        if (errno != EINTR) {
            break;
        }
    }
    assert(shr_q_clean(q, &max) == SH_OK);
    assert(shr_q_count(q) == 2);
    assert(shr_q_clean(q, &limit) == SH_OK);
    assert(shr_q_count(q) == 0);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}


static void test_subscription(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;

    assert(!shr_q_is_subscribed(q, SQ_EVNT_INIT));
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_READWRITE);
    assert(status == SH_OK);
    assert(q != NULL);
    assert(!shr_q_is_subscribed(q, SQ_EVNT_ALL));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_NONE));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_INIT));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_LIMIT));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_EMPTY));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_NONEMPTY));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_LEVEL));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_TIME));
    assert(shr_q_subscribe(q, SQ_EVNT_ALL) == SH_OK);
    assert(!shr_q_is_subscribed(q, SQ_EVNT_ALL));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_NONE));
    assert(shr_q_is_subscribed(q, SQ_EVNT_INIT));
    assert(shr_q_is_subscribed(q, SQ_EVNT_LIMIT));
    assert(shr_q_is_subscribed(q, SQ_EVNT_EMPTY));
    assert(shr_q_is_subscribed(q, SQ_EVNT_NONEMPTY));
    assert(shr_q_is_subscribed(q, SQ_EVNT_LEVEL));
    assert(shr_q_is_subscribed(q, SQ_EVNT_TIME));
    assert(shr_q_unsubscribe(q, SQ_EVNT_ALL) == SH_OK);
    assert(!shr_q_is_subscribed(q, SQ_EVNT_ALL));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_NONE));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_INIT));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_LIMIT));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_EMPTY));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_NONEMPTY));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_LEVEL));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_TIME));
    assert(shr_q_subscribe(q, SQ_EVNT_INIT) == SH_OK);
    assert(shr_q_is_subscribed(q, SQ_EVNT_INIT));
    assert(shr_q_unsubscribe(q, SQ_EVNT_INIT) == SH_OK);
    assert(!shr_q_is_subscribed(q, SQ_EVNT_INIT));
    assert(shr_q_subscribe(q, SQ_EVNT_LIMIT) == SH_OK);
    assert(shr_q_is_subscribed(q, SQ_EVNT_LIMIT));
    assert(shr_q_unsubscribe(q, SQ_EVNT_LIMIT) == SH_OK);
    assert(!shr_q_is_subscribed(q, SQ_EVNT_LIMIT));
    assert(shr_q_subscribe(q, SQ_EVNT_EMPTY) == SH_OK);
    assert(shr_q_is_subscribed(q, SQ_EVNT_EMPTY));
    assert(shr_q_unsubscribe(q, SQ_EVNT_EMPTY) == SH_OK);
    assert(!shr_q_is_subscribed(q, SQ_EVNT_EMPTY));
    assert(shr_q_subscribe(q, SQ_EVNT_NONEMPTY) == SH_OK);
    assert(shr_q_is_subscribed(q, SQ_EVNT_NONEMPTY));
    assert(shr_q_unsubscribe(q, SQ_EVNT_NONEMPTY) == SH_OK);
    assert(!shr_q_is_subscribed(q, SQ_EVNT_NONEMPTY));
    assert(shr_q_subscribe(q, SQ_EVNT_LEVEL) == SH_OK);
    assert(shr_q_is_subscribed(q, SQ_EVNT_LEVEL));
    assert(shr_q_unsubscribe(q, SQ_EVNT_LEVEL) == SH_OK);
    assert(!shr_q_is_subscribed(q, SQ_EVNT_LEVEL));
    assert(shr_q_subscribe(q, SQ_EVNT_TIME) == SH_OK);
    assert(shr_q_is_subscribed(q, SQ_EVNT_TIME));
    assert(shr_q_unsubscribe(q, SQ_EVNT_TIME) == SH_OK);
    assert(!shr_q_is_subscribed(q, SQ_EVNT_TIME));
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}


static void test_empty_queue(void)
{
    sh_status_e status;
    sq_item_s item = {0};
    shr_q_s *q = NULL;

    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_READWRITE);
    assert(status == SH_OK);
    assert(q != NULL);
    assert(shr_q_count(q) == 0);
    assert(shr_q_event(q) == SQ_EVNT_NONE);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_ERR_EMPTY);
    assert(item.buffer == NULL);
    assert(item.buf_size == 0);
    adds = 0;
    status = shr_q_call(q, SIGUSR1);
    assert(status == SH_OK);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_ERR_EMPTY);
    assert(adds == 1);
    item = shr_q_remove_timedwait(q, &item.buffer, &item.buf_size, &(struct timespec) {0, 10000000});
    assert(item.status == SH_ERR_EMPTY);
    assert(adds == 2);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
    free(item.buffer);
}


static void test_single_item_queue(void)
{
    sh_status_e status;
    sq_item_s item = {0};
    shr_q_s *q = NULL;

    adds = 0;
    events = 0;
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_READWRITE);
    assert(status == SH_OK);
    assert(q != NULL);
    assert(shr_q_subscribe(q, SQ_EVNT_ALL) == SH_OK);
    assert(shr_q_listen(q, SIGUSR1) == SH_OK);
    assert(shr_q_monitor(q, SIGUSR2) == SH_OK);
    assert(shr_q_add(q, "test", 4) == SH_OK);
    assert(shr_q_count(q) == 1);
    assert(shr_q_event(q) == SQ_EVNT_INIT);
    assert(shr_q_event(q) == SQ_EVNT_NONEMPTY);
    assert(shr_q_subscribe(q, SQ_EVNT_NONEMPTY) == SH_OK);
    assert(shr_q_add(q, "test", 4) == SH_ERR_LIMIT);
    assert(shr_q_event(q) == SQ_EVNT_LIMIT);
    assert(shr_q_subscribe(q, SQ_EVNT_LIMIT) == SH_OK);
    assert(shr_q_count(q) == 1);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 4);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test", item.length) == 0);
    assert(shr_q_count(q) == 0);
    assert(shr_q_event(q) == SQ_EVNT_EMPTY);
    assert(shr_q_subscribe(q, SQ_EVNT_EMPTY) == SH_OK);
    assert(shr_q_add(q, "test1", 5) == SH_OK);
    assert(shr_q_count(q) == 1);
    assert(shr_q_event(q) == SQ_EVNT_NONEMPTY);
    assert(shr_q_event(q) == SQ_EVNT_LIMIT);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test1", item.length) == 0);
    assert(shr_q_count(q) == 0);
    assert(shr_q_event(q) == SQ_EVNT_EMPTY);
    free(item.buffer);
    assert(events == 4);
    assert(adds == 2);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}

static void test_multi_item_queue(void)
{
    sh_status_e status;
    sq_item_s item = {0};
    shr_q_s *q = NULL;

    adds = 0;
    events = 0;
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 2, SQ_READWRITE);
    assert(status == SH_OK);
    assert(q != NULL);
    assert(shr_q_subscribe(q, SQ_EVNT_ALL) == SH_OK);
    assert(shr_q_listen(q, SIGUSR1) == SH_OK);
    assert(shr_q_monitor(q, SIGUSR2) == SH_OK);
    assert(shr_q_add(q, "test1", 5) == SH_OK);
    assert(shr_q_count(q) == 1);
    assert(shr_q_event(q) == SQ_EVNT_INIT);
    assert(shr_q_event(q) == SQ_EVNT_NONEMPTY);
    assert(shr_q_subscribe(q, SQ_EVNT_NONEMPTY) == SH_OK);
    assert(shr_q_add(q, "test2", 5) == SH_OK);
    assert(shr_q_count(q) == 2);
    assert(shr_q_add(q, "test", 4) == SH_ERR_LIMIT);
    assert(shr_q_event(q) == SQ_EVNT_LIMIT);
    assert(shr_q_count(q) == 2);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test1", item.length) == 0);
    assert(shr_q_count(q) == 1);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test2", item.length) == 0);
    assert(shr_q_count(q) == 0);
    assert(shr_q_event(q) == SQ_EVNT_EMPTY);
    assert(shr_q_subscribe(q, SQ_EVNT_EMPTY) == SH_OK);
    assert(shr_q_add(q, "test3", 5) == SH_OK);
    assert(shr_q_count(q) == 1);
    assert(shr_q_event(q) == SQ_EVNT_NONEMPTY);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test3", item.length) == 0);
    assert(shr_q_count(q) == 0);
    assert(shr_q_event(q) == SQ_EVNT_EMPTY);
    free(item.buffer);
    assert(events == 5);
    assert(adds == 2);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}


static void test_expiration_discard(void)
{
    sh_status_e status;
    sq_item_s item = {0};
    shr_q_s *q = NULL;
    struct timespec sleep = {0, 200000000};

    adds = 0;
    events = 0;
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 2, SQ_READWRITE);
    assert(status == SH_OK);
    assert(q != NULL);
    assert(shr_q_subscribe(q, SQ_EVNT_TIME) == SH_OK);
    assert(shr_q_monitor(q, SIGUSR2) == SH_OK);
    assert(shr_q_will_discard(q) == false);
    assert(shr_q_timelimit(q, 0, 50000000) == SH_OK);
    assert(shr_q_will_discard(q) == false);
    assert(shr_q_discard(q, true) == SH_OK);
    assert(shr_q_will_discard(q) == true);
    status = shr_q_add(q, "test", 4);
    assert(status == SH_OK);
    assert(shr_q_count(q) == 1);
    while (nanosleep(&sleep, &sleep) < 0) {
        if (errno != EINTR) {
            break;
        }
    }
    status = shr_q_add(q, "test1", 5);
    assert(status == SH_OK);
    assert(shr_q_count(q) == 2);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test1", item.length) == 0);
    assert(shr_q_count(q) == 0);
    assert(shr_q_event(q) == SQ_EVNT_TIME);
    status = shr_q_add(q, "test3", 5);
    assert(status == SH_OK);
    status = shr_q_add(q, "test4", 5);
    assert(status == SH_OK);
    assert(shr_q_count(q) == 2);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
    free(item.buffer);
}


static void test_codel_algorithm(void)
{
    sh_status_e status;
    sq_item_s item = {0};
    shr_q_s *q = NULL;
    struct timespec sleep = {0, 100000000};

    adds = 0;
    events = 0;
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 0, SQ_READWRITE);
    assert(status == SH_OK);
    assert(q != NULL);
    assert(shr_q_subscribe(q, SQ_EVNT_TIME) == SH_OK);
    assert(shr_q_monitor(q, SIGUSR2) == SH_OK);
    assert(shr_q_will_discard(q) == false);
    assert(shr_q_timelimit(q, 0, 100000000) == SH_OK);
    assert(shr_q_target_delay(q, 0, 5000000) == SH_OK);
    assert(shr_q_will_discard(q) == true);
    status = shr_q_add(q, "test", 4);
    assert(status == SH_OK);
    assert(shr_q_count(q) == 1);
    while (nanosleep(&sleep, &sleep) < 0) {
        if (errno != EINTR) {
            break;
        }
    }
    status = shr_q_add(q, "test1", 5);
    assert(status == SH_OK);
    assert(shr_q_count(q) == 2);
    sleep.tv_sec = 0;
    sleep.tv_nsec = 10000000;
    while (nanosleep(&sleep, &sleep) < 0) {
        if (errno != EINTR) {
            break;
        }
    }
    status = shr_q_add(q, "test2", 5);
    assert(status == SH_OK);
    assert(shr_q_count(q) == 3);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test2", item.length) == 0);
    assert(shr_q_count(q) == 0);
    assert(shr_q_event(q) == SQ_EVNT_TIME);
    assert(shr_q_event(q) == SQ_EVNT_NONE);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
    free(item.buffer);
}


static void test_vector_operations(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    sq_item_s item = {0};
    struct timespec ts = {0};
    sq_vec_s vector[2] = {{0}, {0}};

    vector[0].type = SH_ASCII_T;
    vector[1].type = SH_ASCII_T;
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 0, SQ_READWRITE);
    assert(status == SH_OK);
    vector[0].base = "token";
    vector[0].len = 5;
    status = shr_q_addv(q, vector, 1);
    assert(status == SH_OK);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "token", 5) == 0);
    assert(item.vector != NULL);
    assert(item.vcount == 1);
    assert(item.vector[0].type == SH_ASCII_T);
    assert(item.vector[0].len == 5);
    assert(memcmp(item.vector[0].base, "token", 5) == 0);
    vector[1].base = "test1";
    vector[1].len = 5;
    status = shr_q_addv(q, vector, 2);
    assert(status == SH_OK);
    vector[1].base = "test2";
    vector[1].len = 5;
    status = shr_q_addv_wait(q, vector, 2);
    assert(status == SH_OK);
    vector[1].base = "test3";
    vector[1].len = 5;
    status = shr_q_addv_timedwait(q, vector, 2, &ts);
    assert(status == SH_OK);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length != 0);
    assert(item.value != NULL);
    assert(item.vector != NULL);
    assert(item.vcount == 2);
    assert(item.vector[0].len == 5);
    assert(memcmp(item.vector[0].base, "token", 5) == 0);
    assert(item.vector[1].len == 5);
    assert(memcmp(item.vector[1].base, "test1", 5) == 0);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length != 0);
    assert(item.value != NULL);
    assert(item.vector != NULL);
    assert(item.vcount == 2);
    assert(item.vector[0].len == 5);
    assert(memcmp(item.vector[0].base, "token", 5) == 0);
    assert(item.vector[1].len == 5);
    assert(memcmp(item.vector[1].base, "test2", 5) == 0);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length != 0);
    assert(item.value != NULL);
    assert(item.vector != NULL);
    assert(item.vcount == 2);
    assert(item.vector[0].type == SH_ASCII_T);
    assert(item.vector[0].len == 5);
    assert(memcmp(item.vector[0].base, "token", 5) == 0);
    assert(item.vector[0].type == SH_ASCII_T);
    assert(item.vector[1].len == 5);
    assert(memcmp(item.vector[1].base, "test3", 5) == 0);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    free(item.buffer);
}


void test_adaptive_lifo()
{
    sh_status_e status;
    shr_q_s *q = NULL;
    sq_item_s item = {0};
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 0, SQ_READWRITE);
    assert(status == SH_OK);
    assert(shr_q_count(q) == 0);
    assert(!shr_q_will_lifo(q));
    assert(shr_q_limit_lifo(q, true) == SH_OK);
    assert(shr_q_will_lifo(q));
    assert(shr_q_limit_lifo(q, false) == SH_OK);
    assert(!shr_q_will_lifo(q));
    assert(shr_q_limit_lifo(q, true) == SH_OK);
    assert(shr_q_add(q, "test1", 5) == SH_OK);
    assert(shr_q_add(q, "test2", 5) == SH_OK);
    assert(shr_q_add(q, "test3", 5) == SH_OK);
    assert(shr_q_count(q) == 3);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test3", item.length) == 0);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test2", item.length) == 0);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test1", item.length) == 0);
    assert(shr_q_level(q, 2) == SH_OK);
    assert(shr_q_monitor(q, SIGUSR2) == SH_OK);
    assert(shr_q_subscribe(q, SQ_EVNT_LEVEL) == SH_OK);
    assert(shr_q_count(q) == 0);
    assert(shr_q_add(q, "test1", 5) == SH_OK);
    assert(shr_q_add(q, "test2", 5) == SH_OK);
    assert(shr_q_add(q, "test3", 5) == SH_OK);
    assert(shr_q_add(q, "test4", 5) == SH_OK);
    assert(shr_q_count(q) == 4);
    assert(shr_q_event(q) == SQ_EVNT_LEVEL);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test4", item.length) == 0);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test3", item.length) == 0);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test1", item.length) == 0);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test2", item.length) == 0);
    assert(shr_q_count(q) == 0);
    assert(shr_q_add(q, "test1", 5) == SH_OK);
    assert(shr_q_add(q, "test2", 5) == SH_OK);
    assert(shr_q_add(q, "test3", 5) == SH_OK);
    assert(shr_q_add(q, "test4", 5) == SH_OK);
    assert(shr_q_count(q) == 4);
    assert(shr_q_event(q) == SQ_EVNT_NONE);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test4", item.length) == 0);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test3", item.length) == 0);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test1", item.length) == 0);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test2", item.length) == 0);
    assert(shr_q_count(q) == 0);
    assert(shr_q_subscribe(q, SQ_EVNT_LEVEL) == SH_OK);
    assert(shr_q_add(q, "test1", 5) == SH_OK);
    assert(shr_q_add(q, "test2", 5) == SH_OK);
    assert(shr_q_add(q, "test3", 5) == SH_OK);
    assert(shr_q_add(q, "test4", 5) == SH_OK);
    assert(shr_q_count(q) == 4);
    assert(shr_q_event(q) == SQ_EVNT_LEVEL);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test4", item.length) == 0);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test3", item.length) == 0);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test1", item.length) == 0);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test2", item.length) == 0);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    free(item.buffer);
}

int main(void)
{
    set_signal_handlers();

    /*
        Test functions
    */
    test_create_error_paths();
    test_create_namedq();
    test_monitor();
    test_listen();
    test_call();
    test_add_errors();
    test_add_wait_errors();
    test_add_timedwait_errors();
    test_remove_errors();
    test_remove_wait_errors();
    test_remove_timedwait_errors();
    test_is_valid();
    test_addv_errors();
    test_addv_wait_errors();
    test_addv_timedwait_errors();

    // set up to test open and close

    shr_q_s *q;
    sh_status_e status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    test_open_close();
    test_add();
    status = shr_q_destroy(&q);
    assert(status == SH_OK);

    /*
        Test behaviors
    */
    test_subscription();
    test_empty_queue();
    test_single_item_queue();
    test_multi_item_queue();
    test_clean();
    test_vector_operations();
    test_expiration_discard();
    test_codel_algorithm();
    test_adaptive_lifo();

    return 0;
}
