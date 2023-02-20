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

#ifndef SHARED_H_
#define SHARED_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

// define useful time related macros
#define timespecadd(a, b, result)                           \
    do {                                                    \
        (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;       \
        (result)->tv_nsec = (a)->tv_nsec + (b)->tv_nsec;    \
        if ((result)->tv_nsec >= 1000000000L) {             \
            (result)->tv_sec++;                             \
            (result)->tv_nsec -= 1000000000L;               \
        }                                                   \
    } while(0)

#define timespecsub(a, b, result)                           \
  do {                                                      \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;           \
    (result)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec;        \
    if ((result)->tv_nsec < 0) {                            \
      --(result)->tv_sec;                                   \
      (result)->tv_nsec += 1000000000;                      \
    }                                                       \
  } while (0)

#define timespeccmp(a, b, cmp)                              \
    (((a)->tv_sec == (b)->tv_sec) ?                         \
    ((a)->tv_nsec cmp (b)->tv_nsec) :                       \
    ((a)->tv_sec cmp (b)->tv_sec))


typedef enum
{
    SH_OK,                  // success
    SH_RETRY,               // retry previous
    SH_ERR_EMPTY,           // no items available
    SH_ERR_LIMIT,           // depth limit reached
    SH_ERR_ARG,             // invalid argument
    SH_ERR_NOMEM,           // not enough memory to satisfy request
    SH_ERR_ACCESS,          // permission error
    SH_ERR_EXIST,           // existence error
    SH_ERR_STATE,           // invalid state
    SH_ERR_PATH,            // problem with path name
    SH_ERR_NOSUPPORT,       // required operation not supported
    SH_ERR_SYS,             // system error
    SH_ERR_CONFLICT,        // update conflict
    SH_ERR_NO_MATCH,        // no match found for key
    SH_ERR_MAX
} sh_status_e;


typedef enum
{
    SH_TUPLE_T = 0,         // tuple of multiple types
    SH_OBJ_T,               // unspecified byte object
    SH_INTEGER_T,           // integer data type determined by length
    SH_FLOAT_T,             // floating point type determined by length
    SH_ASCII_T,             // ascii string (char values 0-127)
    SH_UTF8_T,              // utf-8 string
    SH_DICT_T,              // key/value pairs
    SH_JSON_T,              // json string
    SH_XML_T,               // xml string
} sh_type_e;


/*
    shr_explain -- return a null-terminated string explanation of status code

    returns non-NULL null-terminated string error explanation
*/
extern char *shr_explain(
    sh_status_e status          // status code
);

typedef struct sh_vec
{
#ifdef __x86_64__
    uint32_t _zeroes_;      // pad for alignment
    sh_type_e type;         // type of data in vector
#else
    sh_type_e type;         // type of data in vector
#endif
    size_t len;             // length of data
    void *base;             // pointer to vector data
} sh_vec_s;

#ifdef __cplusplus
}

#endif

#endif // SHARED_H_
