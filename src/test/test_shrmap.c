/*
The MIT License (MIT)

Copyright (c) 2022-2023 Bryan Karr

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
#include <sys/types.h>
#include <unistd.h>

#include "shared_int.h"
#include <shared_map.h>


static void test_create_error_paths( void ) {

    sh_status_e status;
    shr_map_s *map = NULL;

    shm_unlink( "testmap" );
    status = shr_map_create( NULL, "testmap", 0 );
    assert( status == SH_ERR_ARG );
    assert( map == NULL );
    status = shr_map_create( &map, NULL, 0 );
    assert( status == SH_ERR_ARG );
    assert( map == NULL );
    status = shr_map_create( &map, NULL, 0 );
    assert( status == SH_ERR_ARG );
    assert( map == NULL );
    status = shr_map_create( &map, "/fake/testmap", 0 );
    assert( status == SH_ERR_PATH );
    assert( map == NULL );
    status = shr_map_create( &map, "fake/testmap", 0 );
    assert( status == SH_ERR_PATH );
    assert( map == NULL );
    int fd = shm_open( "/test",  O_RDWR | O_CREAT, FILE_MODE );
    assert( fd > 0 );
    status = shr_map_create( &map, "/test", 0 );
    assert( status == SH_ERR_EXIST );
    assert( map == NULL );
    assert( shm_unlink( "/test" ) == 0 );

}


static void test_create_map( void ) {

    sh_status_e status;
    shr_map_s *map = NULL;
    shr_map_s *pmap = NULL;

    shm_unlink( "testmap" );
    status = shr_map_create( &map, "testmap", 0 );
    assert( status == SH_OK );
    assert( map != NULL );
    status = shr_map_destroy( NULL );
    assert( status == SH_ERR_ARG );
    assert( map != NULL );
    status = shr_map_destroy( &pmap );
    assert( status == SH_ERR_ARG );
    status = shr_map_destroy( &map );
    assert( status == SH_OK );
    assert( map == NULL );

}


static void test_open_close( void ) {

    sh_status_e status;
    shr_map_s *map = NULL;

    // close error conditions
    status = shr_map_close( NULL );
    assert( status == SH_ERR_ARG );
    status = shr_map_close( &map );
    assert( status == SH_ERR_ARG );

    // open error conditions
    status = shr_map_open( NULL, "testmap" );
    assert( status == SH_ERR_ARG );
    status = shr_map_open( &map, NULL );
    assert( status == SH_ERR_ARG );
    status = shr_map_open( &map, "badmap" );
    assert( status == SH_ERR_EXIST );

    // successful open and close
    status = shr_map_open( &map, "testmap" );
    assert( status == SH_OK );
    assert( map != NULL );
    status = shr_map_close( &map );
    assert( status == SH_OK );
    assert( map == NULL );
    status = shr_map_open( &map, "testmap" );
    assert( status == SH_OK );
    assert( map != NULL );
    status = shr_map_close( &map );
    assert( status == SH_OK );
    assert( map == NULL );
    status = shr_map_open( &map, "testmap" );
    assert( status == SH_OK );
    assert( map != NULL );
    status = shr_map_close( &map );
    assert( status == SH_OK );
    assert( map == NULL );

}


static void test_add_bucket_overflow( void ) {

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
    int prev_count = shr_map_count( map );

    key = "one";
    klen = strlen(key);
    value = "test one";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "two";
    klen = strlen(key);
    value = "test two";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "three";
    klen = strlen(key);
    value = "test three";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "four";
    klen = strlen(key);
    value = "test four";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "five";
    klen = strlen(key);
    value = "test five";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "six";
    klen = strlen(key);
    value = "test six";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "seven";
    klen = strlen(key);
    value = "test seven";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "eight";
    klen = strlen(key);
    value = "test eight";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "nine";
    klen = strlen(key);
    value = "test nine";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "ten";
    klen = strlen(key);
    value = "test ten";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "eleven";
    klen = strlen(key);
    value = "test eleven";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "twelve";
    klen = strlen(key);
    value = "test twelve";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "thirteen";
    klen = strlen(key);
    value = "test thirteen";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "fourteen";
    klen = strlen(key);
    value = "test fourteen";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "fifteen";
    klen = strlen(key);
    value = "test fifteen";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "sixteen";
    klen = strlen(key);
    value = "test sixteen";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );

    key = "one";
    klen = strlen(key);
    value = "test one";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "two";
    klen = strlen(key);
    value = "test two";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "three";
    klen = strlen(key);
    value = "test three";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "four";
    klen = strlen(key);
    value = "test four";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "five";
    klen = strlen(key);
    value = "test five";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "six";
    klen = strlen(key);
    value = "test six";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "seven";
    klen = strlen(key);
    value = "test seven";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "eight";
    klen = strlen(key);
    value = "test eight";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "nine";
    klen = strlen(key);
    value = "test nine";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "ten";
    klen = strlen(key);
    value = "test ten";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "eleven";
    klen = strlen(key);
    value = "test eleven";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "twelve";
    klen = strlen(key);
    value = "test twelve";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "thirteen";
    klen = strlen(key);
    value = "test thirteen";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "fourteen";
    klen = strlen(key);
    value = "test fourteen";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "fifteen";
    klen = strlen(key);
    value = "test fifteen";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "sixteen";
    klen = strlen(key);
    value = "test sixteen";
    vlen = strlen(value);
    result = shr_map_add(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );

    status = shr_map_close( &map );
    assert( status == SH_OK );
    assert( map == NULL );
    free( buffer );
}


static void test_add_single_bucket( void ) {

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
    int prev_count = shr_map_count( map );

    key = "one";
    klen = strlen(key);
    value = "test one";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "two";
    klen = strlen(key);
    value = "test two";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "three";
    klen = strlen(key);
    value = "test three";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "four";
    klen = strlen(key);
    value = "test four";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "five";
    klen = strlen(key);
    value = "test five";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "six";
    klen = strlen(key);
    value = "test six";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "seven";
    klen = strlen(key);
    value = "test seven";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "eight";
    klen = strlen(key);
    value = "test eight";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "nine";
    klen = strlen(key);
    value = "test nine";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "ten";
    klen = strlen(key);
    value = "test ten";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "eleven";
    klen = strlen(key);
    value = "test eleven";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "twelve";
    klen = strlen(key);
    value = "test twelve";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "thirteen";
    klen = strlen(key);
    value = "test thirteen";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "fourteen";
    klen = strlen(key);
    value = "test fourteen";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );
    key = "fifteen";
    klen = strlen(key);
    value = "test fifteen";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( shr_map_count( map ) == ++prev_count );

    key = "one";
    klen = strlen(key);
    value = "test one";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "two";
    klen = strlen(key);
    value = "test two";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "three";
    klen = strlen(key);
    value = "test three";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "four";
    klen = strlen(key);
    value = "test four";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "five";
    klen = strlen(key);
    value = "test five";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "six";
    klen = strlen(key);
    value = "test six";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "seven";
    klen = strlen(key);
    value = "test seven";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "eight";
    klen = strlen(key);
    value = "test eight";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "nine";
    klen = strlen(key);
    value = "test nine";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "ten";
    klen = strlen(key);
    value = "test ten";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "eleven";
    klen = strlen(key);
    value = "test eleven";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "twelve";
    klen = strlen(key);
    value = "test twelve";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "thirteen";
    klen = strlen(key);
    value = "test thirteen";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "fourteen";
    klen = strlen(key);
    value = "test fourteen";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );
    key = "fifteen";
    klen = strlen(key);
    value = "test fifteen";
    vlen = strlen(value);
    result = shr_map_add(map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_ERR_CONFLICT);
    assert( shr_map_count( map ) == prev_count );

    status = shr_map_close( &map );
    assert( status == SH_OK );
    assert( map == NULL );
    free( buffer );
}


static void test_get_single_bucket( void ) {

    sh_status_e status;
    shr_map_s *map = NULL;
    void *buffer = NULL;
    size_t buff_size = 0;
    char *key = NULL;
    size_t klen = 0;
    sm_item_s result = {0};
 
    status = shr_map_open( &map, "testmap" );
    assert( status == SH_OK );

    key = "one";
    klen = strlen( key );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( memcmp( "test one", result.value, result.vlength ) == 0 );
    key = "two";
    klen = strlen( key );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( memcmp( "test two", result.value, result.vlength ) == 0 );
    key = "three";
    klen = strlen( key );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( memcmp( "test three", result.value, result.vlength ) == 0 );
    key = "four";
    klen = strlen( key );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( memcmp( "test four", result.value, result.vlength ) == 0 );
    key = "five";
    klen = strlen( key );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( memcmp( "test five", result.value, result.vlength ) == 0 );
    key = "six";
    klen = strlen( key );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( memcmp( "test six", result.value, result.vlength ) == 0 );
    key = "seven";
    klen = strlen( key );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( memcmp( "test seven", result.value, result.vlength ) == 0 );
    key = "eight";
    klen = strlen( key );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( memcmp( "test eight", result.value, result.vlength ) == 0 );
    key = "nine";
    klen = strlen( key );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( memcmp( "test nine", result.value, result.vlength ) == 0 );
    key = "ten";
    klen = strlen( key );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( memcmp( "test ten", result.value, result.vlength ) == 0 );
    key = "eleven";
    klen = strlen( key );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( memcmp( "test eleven", result.value, result.vlength ) == 0 );
    key = "twelve";
    klen = strlen( key );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( memcmp( "test twelve", result.value, result.vlength ) == 0 );
    key = "thirteen";
    klen = strlen( key );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( memcmp( "test thirteen", result.value, result.vlength ) == 0 );
    key = "fourteen";
    klen = strlen( key );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( memcmp( "test fourteen", result.value, result.vlength ) == 0 );
    key = "fifteen";
    klen = strlen( key );
    result = shr_map_get( map, ( uint8_t * ) key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( memcmp( "test fifteen", result.value, result.vlength ) == 0 );

    status = shr_map_close( &map );
    assert( status == SH_OK );
    assert( map == NULL );
    free( buffer );
}


static void test_remove_single_bucket( void ) {

    sh_status_e status;
    shr_map_s *map = NULL;
    void *buffer = NULL;
    size_t buff_size = 0;
    char *key = NULL;
    size_t klen = 0;
    sm_item_s result = {0};
 
    status = shr_map_open( &map, "testmap" );
    assert( status == SH_OK );
    int prev_count = shr_map_count( map );

    key = "one";
    klen = strlen( key );
    result = shr_map_remove( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( shr_map_count( map) == --prev_count );
    assert( memcmp( "test one", result.value, result.vlength ) == 0 );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_ERR_NO_MATCH );
    key = "two";
    klen = strlen( key );
    result = shr_map_remove( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( shr_map_count( map) == --prev_count );
    assert( memcmp( "test two", result.value, result.vlength ) == 0 );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_ERR_NO_MATCH );
    key = "three";
    klen = strlen( key );
    result = shr_map_remove( map, ( uint8_t * ) key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( shr_map_count( map) == --prev_count );
    assert( memcmp( "test three", result.value, result.vlength ) == 0 );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_ERR_NO_MATCH );
    key = "four";
    klen = strlen( key );
    result = shr_map_remove( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( shr_map_count( map) == --prev_count );
    assert( memcmp( "test four", result.value, result.vlength ) == 0 );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_ERR_NO_MATCH );
    key = "five";
    klen = strlen( key );
    result = shr_map_remove( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( shr_map_count( map) == --prev_count );
    assert( memcmp( "test five", result.value, result.vlength ) == 0 );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_ERR_NO_MATCH );
    key = "six";
    klen = strlen( key );
    result = shr_map_remove( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( shr_map_count( map) == --prev_count );
    assert( memcmp( "test six", result.value, result.vlength ) == 0 );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_ERR_NO_MATCH );
    key = "seven";
    klen = strlen( key );
    result = shr_map_remove( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( shr_map_count( map) == --prev_count );
    assert( memcmp( "test seven", result.value, result.vlength ) == 0 );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_ERR_NO_MATCH );
    key = "eight";
    klen = strlen( key );
    result = shr_map_remove( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( shr_map_count( map) == --prev_count );
    assert( memcmp( "test eight", result.value, result.vlength ) == 0 );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_ERR_NO_MATCH );
    key = "nine";
    klen = strlen( key );
    result = shr_map_remove( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( shr_map_count( map) == --prev_count );
    assert( memcmp( "test nine", result.value, result.vlength ) == 0 );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_ERR_NO_MATCH );
    key = "ten";
    klen = strlen( key );
    result = shr_map_remove( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( shr_map_count( map) == --prev_count );
    assert( memcmp( "test ten", result.value, result.vlength ) == 0 );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_ERR_NO_MATCH );
    key = "eleven";
    klen = strlen( key );
    result = shr_map_remove( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( shr_map_count( map) == --prev_count );
    assert( memcmp( "test eleven", result.value, result.vlength ) == 0 );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_ERR_NO_MATCH );
    key = "twelve";
    klen = strlen( key );
    result = shr_map_remove( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( shr_map_count( map) == --prev_count );
    assert( memcmp( "test twelve", result.value, result.vlength ) == 0 );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_ERR_NO_MATCH );
    key = "thirteen";
    klen = strlen( key );
    result = shr_map_remove( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( shr_map_count( map) == --prev_count );
    assert( memcmp( "test thirteen", result.value, result.vlength ) == 0 );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_ERR_NO_MATCH );
    key = "fourteen";
    klen = strlen( key );
    result = shr_map_remove( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( shr_map_count( map) == --prev_count );
    assert( memcmp( "test fourteen", result.value, result.vlength ) == 0 );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_ERR_NO_MATCH );
    key = "fifteen";
    klen = strlen( key );
    result = shr_map_remove( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( shr_map_count( map) == --prev_count );
    assert( memcmp( "test fifteen", result.value, result.vlength ) == 0 );
    result = shr_map_get( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_ERR_EMPTY );

    status = shr_map_close( &map );
    assert( status == SH_OK );
    assert( map == NULL );
    free( buffer );
}


static void test_addv_operation( void ) {

    sh_status_e status;
    shr_map_s *map = NULL;
    void *buffer = NULL;
    size_t buff_size = 0;
    char *key = NULL;
    size_t klen = 0;
    sm_item_s result = {0};
    sh_vec_s vector[2] = {{0}, {0}};
 
    vector[ 0 ].type = SH_ASCII_T;
    vector[ 0 ].base = "token";
    vector[ 0 ].len = 5;
    vector[ 1 ].type = SH_ASCII_T;
    status = shr_map_open( &map, "testmap" );
    assert( status == SH_OK );
    int prev_count = shr_map_count( map );

    key = "one";
    klen = strlen( key );
    result = shr_map_addv( map, ( uint8_t * ) key, klen, vector, 1, SH_TUPLE_T, &buffer, &buff_size );
    assert(result.status == SH_OK);
    assert( prev_count == shr_map_count( map )  - 1 );
    result = shr_map_remove( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( prev_count == shr_map_count( map ) );
    assert( result.vcount == 1 );
    assert( result.vector[ 0 ].len == vector[ 0 ].len );
    assert( result.vlength == vector[ 0 ].len );
    assert( memcmp( vector[ 0 ].base, result.value, result.vlength ) == 0 );
    assert( memcmp( vector[ 0 ].base, result.vector[ 0 ].base, result.vlength ) == 0 );
    vector[ 1 ].base = "test one";
    vector[ 1 ].len = strlen( vector[ 1 ].base );
    result = shr_map_addv(map, ( uint8_t * ) key, klen, vector, 2, SH_TUPLE_T, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( prev_count == shr_map_count( map )  - 1 );
    result = shr_map_remove( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( prev_count == shr_map_count( map ) );
    assert( result.type == SH_TUPLE_T );
    assert( result.vcount == 2 );
    assert( result.vector[ 0 ].type == vector[ 0 ].type );
    assert( result.vector[ 1 ].type == vector[ 1 ].type );
    assert( result.vector[ 0 ].len == vector[ 0 ].len );
    assert( result.vector[ 1 ].len == vector[ 1 ].len );
    assert( memcmp( vector[ 0 ].base, result.vector[ 0 ].base, result.vector[ 0 ].len ) == 0 );
    assert( memcmp( vector[ 1 ].base, result.vector[ 1 ].base, result.vector[ 1 ].len ) == 0 );
    
    status = shr_map_close( &map );
    assert( status == SH_OK );
    assert( map == NULL );
    free( buffer );
}


static void test_get_attr_operation( void ) {

    sh_status_e status;
    shr_map_s *map = NULL;
    void *buffer = NULL;
    size_t buff_size = 0;
    char *key = NULL;
    size_t klen = 0;
    sm_item_s result = {0};
    sh_vec_s vector[2] = {{0}, {0}};
 
    vector[ 0 ].type = SH_ASCII_T;
    vector[ 0 ].base = "token";
    vector[ 0 ].len = 5;
    vector[ 1 ].type = SH_ASCII_T;
    status = shr_map_open( &map, "testmap" );
    assert( status == SH_OK );

    key = "one";
    klen = strlen( key );
    result = shr_map_addv(map, (uint8_t*)key, klen, vector, 1, SH_TUPLE_T, &buffer, &buff_size);
    assert(result.status == SH_OK);
    result = shr_map_get_attr( map, (uint8_t*)key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( result.vcount == 1 );
    assert( result.vector[ 0 ].len == vector[ 0 ].len );
    assert( result.vlength == vector[ 0 ].len );
    result = shr_map_remove( map, (uint8_t*)key, klen, &buffer, &buff_size );
    vector[ 1 ].base = "test one";
    vector[ 1 ].len = strlen( vector[ 1 ].base );
    assert( result.status == SH_OK );
    result = shr_map_addv(map, (uint8_t*)key, klen, vector, 2, SH_TUPLE_T, &buffer, &buff_size);
    assert(result.status == SH_OK);
    result = shr_map_get_attr( map, (uint8_t*)key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( result.type == SH_TUPLE_T );
    assert( result.vcount == 2 );
    assert( result.vector[ 0 ].type == vector[ 0 ].type );
    assert( result.vector[ 1 ].type == vector[ 1 ].type );
    assert( result.vector[ 0 ].len == vector[ 0 ].len );
    assert( result.vector[ 1 ].len == vector[ 1 ].len );
    result = shr_map_remove( map, (uint8_t*)key, klen, &buffer, &buff_size );
    
    status = shr_map_close( &map );
    assert( status == SH_OK );
    assert( map == NULL );
    free( buffer );
}


static void test_get_select( void ) {

    sh_status_e status;
    shr_map_s *map = NULL;
    void *buffer = NULL;
    size_t buff_size = 0;
    char *key = NULL;
    size_t klen = 0;
    char *value = NULL;
    size_t vlen = 0;
    sm_item_s result = {0};
    sh_vec_s vector[2] = {{0}, {0}};

    vector[ 0 ].type = SH_ASCII_T;
    vector[ 0 ].base = "token";
    vector[ 0 ].len = 5;
    vector[ 1 ].type = SH_ASCII_T;

    status = shr_map_open( &map, "testmap" );
    assert( status == SH_OK );
    key = "first value";
    klen = strlen( key );
    value = "abcdefghijklmnopqrstuvwxyz0123456789";
    vlen = strlen( value );
    result = shr_map_add( map, ( uint8_t * )key, klen, value, vlen, &buffer, &buff_size );
    assert( result.status == SH_OK );   
    result = shr_map_get_select( map, ( uint8_t * )key, klen, 0, 0, 36, &buffer, &buff_size );
    assert( result.status == SH_OK );   
    assert( memcmp( buffer, value, vlen ) == 0 );
    result = shr_map_get_select( map, ( uint8_t * )key, klen, 0, 26, 10, &buffer, &buff_size );
    assert( result.status == SH_OK );   
    assert( memcmp( buffer, value + 26, vlen - 26 ) == 0 );
    result = shr_map_get_select( map, ( uint8_t * )key, klen, 0, 27, 10, &buffer, &buff_size );
    assert( result.status == SH_OK );   
    assert( memcmp( buffer, value + 27, vlen - 27 ) == 0 );
    
    key = "second value";
    klen = strlen( key );
    vector[ 1 ].base = "abcdefghijklmnopqrstuvwxyz0123456789";
    vector[ 1 ].len = strlen( vector[ 1 ].base );
    result = shr_map_addv(map, (uint8_t*)key, klen, vector, 2, SH_TUPLE_T, &buffer, &buff_size);
    assert( result.status == SH_OK );   
    result = shr_map_get_select( map, ( uint8_t * )key, klen, 1, 0, 36, &buffer, &buff_size );
    assert( result.status == SH_OK );   
    assert( memcmp( buffer, value, vlen ) == 0 );
    result = shr_map_get_select( map, ( uint8_t * )key, klen, 1, 26, 10, &buffer, &buff_size );
    assert( result.status == SH_OK );   
    assert( memcmp( buffer, value + 26, vlen - 26 ) == 0 );
    result = shr_map_get_select( map, ( uint8_t * )key, klen, 1, 27, 10, &buffer, &buff_size );
    assert( result.status == SH_OK );   
    assert( memcmp( buffer, value + 27, vlen - 27 ) == 0 );

    status = shr_map_close( &map );
    assert( status == SH_OK );
    assert( map == NULL );
    free( buffer );
}


static void test_put_bucket_overflow( void ) {

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
    int prev_count = shr_map_count( map );
    key = "one";
    klen = strlen(key);
    value = "test one";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "two";
    klen = strlen(key);
    value = "test two";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "three";
    klen = strlen(key);
    value = "test three";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "four";
    klen = strlen(key);
    value = "test four";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "five";
    klen = strlen(key);
    value = "test five";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "six";
    klen = strlen(key);
    value = "test six";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "seven";
    klen = strlen(key);
    value = "test seven";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "eight";
    klen = strlen(key);
    value = "test eight";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "nine";
    klen = strlen(key);
    value = "test nine";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "ten";
    klen = strlen(key);
    value = "test ten";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "eleven";
    klen = strlen(key);
    value = "test eleven";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "twelve";
    klen = strlen(key);
    value = "test twelve";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "thirteen";
    klen = strlen(key);
    value = "test thirteen";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "fourteen";
    klen = strlen(key);
    value = "test fourteen";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "fifteen";
    klen = strlen(key);
    value = "test fifteen";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "sixteen";
    klen = strlen(key);
    value = "test sixteen";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "seventeen";
    klen = strlen( key );
    value = "test seventeen";
    vlen = strlen( value );
    result = shr_map_put( map, (uint8_t*) key, klen, value, vlen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( result.value == NULL );
    result = shr_map_get( map, (uint8_t*) key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) - 1);


    prev_count = shr_map_count( map );
    key = "one";
    klen = strlen( key );
    value = "test put one";
    vlen = strlen( value );
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert( result.status == SH_OK );
    assert( result.value != NULL );
    assert( memcmp( value, result.value, vlen ) != 0 );
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    assert( prev_count == shr_map_count( map ) );
    key = "two";
    klen = strlen(key);
    value = "test put two";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( result.value != NULL );
    assert( memcmp( value, result.value, vlen ) != 0 );
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "three";
    klen = strlen(key);
    value = "test put three";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( result.value != NULL );
    assert( memcmp( value, result.value, vlen ) != 0 );
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "four";
    klen = strlen(key);
    value = "test put four";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( result.value != NULL );
    assert( memcmp( value, result.value, vlen ) != 0 );
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "five";
    klen = strlen(key);
    value = "test put five";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( result.value != NULL );
    assert( memcmp( value, result.value, vlen ) != 0 );
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "six";
    klen = strlen(key);
    value = "test put six";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( result.value != NULL );
    assert( memcmp( value, result.value, vlen ) != 0 );
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "seven";
    klen = strlen(key);
    value = "test put seven";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( result.value != NULL );
    assert( memcmp( value, result.value, vlen ) != 0 );
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "eight";
    klen = strlen(key);
    value = "test put eight";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( result.value != NULL );
    assert( memcmp( value, result.value, vlen ) != 0 );
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "nine";
    klen = strlen(key);
    value = "test put nine";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( result.value != NULL );
    assert( memcmp( value, result.value, vlen ) != 0 );
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "ten";
    klen = strlen(key);
    value = "test put ten";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( result.value != NULL );
    assert( memcmp( value, result.value, vlen ) != 0 );
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "eleven";
    klen = strlen(key);
    value = "test put eleven";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( result.value != NULL );
    assert( memcmp( value, result.value, vlen ) != 0 );
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "twelve";
    klen = strlen(key);
    value = "test put twelve";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( result.value != NULL );
    assert( memcmp( value, result.value, vlen ) != 0 );
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "thirteen";
    klen = strlen(key);
    value = "test put thirteen";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( result.value != NULL );
    assert( memcmp( value, result.value, vlen ) != 0 );
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "fourteen";
    klen = strlen(key);
    value = "test put fourteen";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( result.value != NULL );
    assert( memcmp( value, result.value, vlen ) != 0 );
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "fifteen";
    klen = strlen(key);
    value = "test put fifteen";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( result.value != NULL );
    assert( memcmp( value, result.value, vlen ) != 0 );
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "sixteen";
    klen = strlen(key);
    value = "test put sixteen";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( result.value != NULL );
    assert( memcmp( value, result.value, vlen ) != 0 );
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );
    key = "seventeen";
    klen = strlen(key);
    value = "test put seventeen";
    vlen = strlen(value);
    result = shr_map_put(map, (uint8_t*)key, klen, value, vlen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( result.value != NULL );
    assert( memcmp( value, result.value, vlen ) != 0 );
    result = shr_map_get( map, (uint8_t*)key, klen, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( memcmp( value, result.value, vlen ) == 0 );
    assert( prev_count == shr_map_count( map ) );

    status = shr_map_close( &map );
    assert( status == SH_OK );
    assert( map == NULL );
    free( buffer );
}


static void test_putv_operation( void ) {

    sh_status_e status;
    shr_map_s *map = NULL;
    void *buffer = NULL;
    size_t buff_size = 0;
    char *key = NULL;
    size_t klen = 0;
    sm_item_s result = {0};
    sh_vec_s vector[2] = {{0}, {0}};
 
    vector[ 0 ].type = SH_ASCII_T;
    vector[ 0 ].base = "putv token";
    vector[ 0 ].len = 5;
    vector[ 1 ].type = SH_ASCII_T;
    status = shr_map_open( &map, "testmap" );
    assert( status == SH_OK );
    int prev_count = shr_map_count( map );

    key = "one putv";
    klen = strlen( key );
    result = shr_map_putv(map, ( uint8_t * )key, klen, vector, 1, SH_TUPLE_T, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( prev_count == shr_map_count( map )  - 1 );
    result = shr_map_remove( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( prev_count == shr_map_count( map ) );
    assert( result.vcount == 1 );
    assert( result.vector[ 0 ].len == vector[ 0 ].len );
    assert( result.vlength == vector[ 0 ].len );
    assert( memcmp( vector[ 0 ].base, result.value, result.vlength ) == 0 );
    assert( memcmp( vector[ 0 ].base, result.vector[ 0 ].base, result.vlength ) == 0 );
    vector[ 1 ].base = "test putv one";
    vector[ 1 ].len = strlen( vector[ 1 ].base );
    result = shr_map_putv(map, ( uint8_t * ) key, klen, vector, 2, SH_TUPLE_T, &buffer, &buff_size);
    assert(result.status == SH_OK);
    assert( prev_count == shr_map_count( map )  - 1 );
    result = shr_map_remove( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( prev_count == shr_map_count( map ) );
    assert( result.type == SH_TUPLE_T );
    assert( result.vcount == 2 );
    assert( result.vector[ 0 ].type == vector[ 0 ].type );
    assert( result.vector[ 1 ].type == vector[ 1 ].type );
    assert( result.vector[ 0 ].len == vector[ 0 ].len );
    assert( result.vector[ 1 ].len == vector[ 1 ].len );
    assert( memcmp( vector[ 0 ].base, result.vector[ 0 ].base, result.vector[ 0 ].len ) == 0 );
    assert( memcmp( vector[ 1 ].base, result.vector[ 1 ].base, result.vector[ 1 ].len ) == 0 );
    
    status = shr_map_close( &map );
    assert( status == SH_OK );
    assert( map == NULL );
    free( buffer );
}


static void test_is_valid(void)
{
    sh_status_e status;
    shr_map_s *map = NULL;
    shm_unlink("testmap");
    assert(!shr_map_is_valid(NULL));
    assert(!shr_map_is_valid(""));
    assert(!shr_map_is_valid("testmap"));
    int fd = shm_open("testmap", O_RDWR | O_CREAT | O_EXCL, FILE_MODE);
    assert(fd > 0);
    int rc = ftruncate(fd, PAGE_SIZE >> 1);
    assert(rc == 0);
    assert(!shr_map_is_valid("testmap"));
    rc = ftruncate(fd, PAGE_SIZE);
    assert(rc == 0);
    assert(!shr_map_is_valid("testmap"));
    shm_unlink("testmap");
    status = shr_map_create(&map, "testmap", 0);
    assert(status == SH_OK);
    assert(shr_map_is_valid("testmap"));
    status = shr_map_destroy(&map);
    assert(status == SH_OK);
}


static void test_update_operation( void ) {

    sh_status_e status;
    shr_map_s *map = NULL;
    void *buffer = NULL;
    size_t buff_size = 0;
    char *key = NULL;
    size_t klen = 0;
    char *value = NULL;
    size_t vlen = 0;
    sm_item_s result = {0};
 
    status = shr_map_open( &map, "testmap" );
    assert( status == SH_OK );
    int prev_count = shr_map_count( map );

    key = "one update";
    klen = strlen( key );
    value = "test one";
    vlen = strlen( value );

    result = shr_map_add(map, ( uint8_t * ) key, klen, value, vlen, &buffer, &buff_size);
    assert( result.status == SH_OK );
    assert( prev_count == shr_map_count( map )  - 1 );
    value = "test one update";
    vlen = strlen( value );
    result = shr_map_update( map, ( uint8_t * ) key, klen, value, vlen, &buffer, &buff_size, 0 );
    assert( result.status == SH_ERR_ARG );
    result = shr_map_update( map, ( uint8_t * ) key, klen, value, vlen, &buffer, &buff_size, result.token - 1 );
    assert( result.status == SH_ERR_CONFLICT );
    result = shr_map_update( map, ( uint8_t * ) key, klen, value, vlen, &buffer, &buff_size, result.token );
    assert( result.status == SH_OK );
    assert( result.vlength == strlen( "test one") );
    assert( memcmp( "test one", result.value, result.vlength ) == 0 );
    result = shr_map_remove( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( prev_count == shr_map_count( map ) );
    assert( result.vlength == vlen );
    assert( memcmp( value, result.value, result.vlength ) == 0 );
    
    status = shr_map_close( &map );
    assert( status == SH_OK );
    assert( map == NULL );
    free( buffer );
}


static void test_updatev_operation( void ) {

    sh_status_e status;
    shr_map_s *map = NULL;
    void *buffer = NULL;
    size_t buff_size = 0;
    char *key = NULL;
    size_t klen = 0;
    sm_item_s result = {0};
    sh_vec_s vector[2] = {{0}, {0}};
 
    vector[ 0 ].type = SH_ASCII_T;
    vector[ 0 ].base = "token";
    vector[ 0 ].len = 5;
    vector[ 1 ].type = SH_ASCII_T;
    vector[ 1 ].base = "test one";
    vector[ 1 ].len = strlen( vector[ 1 ].base );
    status = shr_map_open( &map, "testmap" );
    assert( status == SH_OK );
    int prev_count = shr_map_count( map );

    key = "one updatev";
    klen = strlen( key );

    result = shr_map_addv(map, ( uint8_t * ) key, klen, vector, 2, SH_TUPLE_T, &buffer, &buff_size);
    assert( result.status == SH_OK );
    assert( prev_count == shr_map_count( map )  - 1 );
    vector[ 1 ].base = "test one update";
    vector[ 1 ].len = strlen( vector[ 1 ].base );
    result = shr_map_updatev( map, ( uint8_t * ) key, klen, vector, 2, SH_TUPLE_T, &buffer, &buff_size, 0 );
    assert( result.status == SH_ERR_ARG );
    result = shr_map_updatev( map, ( uint8_t * ) key, klen, vector, 2, SH_TUPLE_T, &buffer, &buff_size, result.token - 1 );
    assert( result.status == SH_ERR_CONFLICT );
    result = shr_map_updatev( map, ( uint8_t * ) key, klen, vector, 2, SH_TUPLE_T, &buffer, &buff_size, result.token );
    assert( result.status == SH_OK );
    assert( result.vcount == 2 );
    assert( result.vector[ 0 ].type == vector[ 0 ].type );
    assert( result.vector[ 0 ].len == vector[ 0 ].len );
    assert( memcmp( vector[ 0 ].base, result.vector[ 0 ].base, result.vector[ 0 ].len ) == 0 );
    assert( result.vector[ 1 ].type == vector[ 1 ].type );
    assert( result.vector[ 1 ].len == strlen("test one") );
    assert( memcmp( "test one", result.vector[ 1 ].base, result.vector[ 1 ].len ) == 0 );
    result = shr_map_remove( map, ( uint8_t * )key, klen, &buffer, &buff_size );
    assert( result.status == SH_OK );
    assert( prev_count == shr_map_count( map ) );
    assert( result.type == SH_TUPLE_T );
    assert( result.vcount == 2 );
    assert( result.vector[ 0 ].type == vector[ 0 ].type );
    assert( result.vector[ 1 ].type == vector[ 1 ].type );
    assert( result.vector[ 0 ].len == vector[ 0 ].len );
    assert( result.vector[ 1 ].len == vector[ 1 ].len );
    assert( memcmp( vector[ 0 ].base, result.vector[ 0 ].base, result.vector[ 0 ].len ) == 0 );
    assert( memcmp( vector[ 1 ].base, result.vector[ 1 ].base, result.vector[ 1 ].len ) == 0 );
    
    status = shr_map_close( &map );
    assert( status == SH_OK );
    assert( map == NULL );
    free( buffer );
}


int main( void ) {

    /*
        Test functions
    */
    test_create_error_paths();
    test_create_map();
    test_is_valid();

    // set up to test open and close

    shr_map_s *map;
    sh_status_e status = shr_map_create( &map, "testmap", 0 );
    assert( status == SH_OK );
    test_open_close();
    test_add_single_bucket();
    test_get_single_bucket();
    test_remove_single_bucket();
    test_addv_operation();
    test_get_attr_operation();
    test_add_bucket_overflow();
    test_get_select();
    test_put_bucket_overflow();
    test_putv_operation();
    test_update_operation();
    test_updatev_operation();
    status = shr_map_destroy( &map );
    assert( status == SH_OK );

    return 0;
}

