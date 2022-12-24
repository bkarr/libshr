/*
The MIT License (MIT)

Copyright (c) 2022 Bryan Karr

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
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "shared_int.h"
#include <shared_map.h>

static void test_create_error_paths(void)
{
    sh_status_e status;
    shr_map_s *map = NULL;

    shm_unlink("testmap");
    status = shr_map_create(NULL, "testmap", 0);
    assert(status == SH_ERR_ARG);
    assert(map == NULL);
    status = shr_map_create(&map, NULL, 0);
    assert(status == SH_ERR_ARG);
    assert(map == NULL);
    status = shr_map_create(&map, NULL, 0);
    assert(status == SH_ERR_ARG);
    assert(map == NULL);
    status = shr_map_create(&map, "/fake/testmap", 0);
    assert(status == SH_ERR_PATH);
    assert(map == NULL);
    status = shr_map_create(&map, "fake/testmap", 0);
    assert(status == SH_ERR_PATH);
    assert(map == NULL);
    int fd = shm_open("/test",  O_RDWR | O_CREAT, FILE_MODE);
    assert(fd > 0);
    status = shr_map_create(&map, "/test", 0);
    assert(status == SH_ERR_EXIST);
    assert(map == NULL);
    assert(shm_unlink("/test") == 0);
}


static void test_create_map(void)
{
    sh_status_e status;
    shr_map_s *map = NULL;
    shr_map_s *pmap = NULL;

    shm_unlink("testmap");
    status = shr_map_create(&map, "testmap", 0);
    assert(status == SH_OK);
    assert(map != NULL);
    status = shr_map_destroy(NULL);
    assert(status == SH_ERR_ARG);
    assert(map != NULL);
    status = shr_map_destroy(&pmap);
    assert(status == SH_ERR_ARG);
    status = shr_map_destroy(&map);
    assert(status == SH_OK);
    assert(map == NULL);
}

static void test_open_close(void)
{
    sh_status_e status;
    shr_map_s *map = NULL;

    // close error conditions
    status = shr_map_close(NULL);
    assert(status == SH_ERR_ARG);
    status = shr_map_close(&map);
    assert(status == SH_ERR_ARG);

    // open error conditions
    status = shr_map_open(NULL, "testmap");
    assert(status == SH_ERR_ARG);
    status = shr_map_open(&map, NULL);
    assert(status == SH_ERR_ARG);
    status = shr_map_open(&map, "badmap");
    assert(status == SH_ERR_EXIST);

    // successful open and close
    status = shr_map_open(&map, "testmap");
    assert(status == SH_OK);
    assert(map != NULL);
    status = shr_map_close(&map);
    assert(status == SH_OK);
    assert(map == NULL);
    status = shr_map_open(&map, "testmap");
    assert(status == SH_OK);
    assert(map != NULL);
    status = shr_map_close(&map);
    assert(status == SH_OK);
    assert(map == NULL);
    status = shr_map_open(&map, "testmap");
    assert(status == SH_OK);
    assert(map != NULL);
    status = shr_map_close(&map);
    assert(status == SH_OK);
    assert(map == NULL);
}

static void test_add_single_bucket(void) {

    sh_status_e status;
    shr_map_s *map = NULL;
    void *buffer = NULL;
    size_t buff_size = 0;
    char *key = NULL;
    char *value = NULL;
    size_t klen = 0;
    size_t vlen = 0;
    sm_item_s result = {0};
 
    status = shr_map_open(&map, "testmap");
    assert(status == SH_OK);
    key = "one";
    klen = strlen(key);
    value = "test one";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);

}

int main(void)
{

    /*
        Test functions
    */
    test_create_error_paths();
    test_create_map();

    // set up to test open and close

    shr_map_s *map;
    sh_status_e status = shr_map_create(&map, "testmap", 0);
    assert(status == SH_OK);
    test_open_close();
    test_add_single_bucket();
    status = shr_map_destroy(&map);
    assert(status == SH_OK);


    return 0;
}