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

#ifndef SHARED_H_
#define SHARED_H_

typedef enum
{
    SH_OK,                  // success
    SH_RETRY,               // retry previous
    SH_ERR_EMPTY,           // no items on queue
    SH_ERR_LIMIT,           // depth limit reached
    SH_ERR_ARG,             // invalid argument
    SH_ERR_NOMEM,           // not enough memory to satisfy request
    SH_ERR_ACCESS,          // permission error
    SH_ERR_EXIST,           // existence error
    SH_ERR_STATE,           // invalid state
    SH_ERR_PATH,            // problem with path name
    SH_ERR_NOSUPPORT,       // required operation not supported
    SH_ERR_SYS              // system error
} sh_status_e;


typedef enum
{
    SH_VECTOR_T = 0,        // vector of multiple types
    SH_STRM_T,              // unspecified byte stream
    SH_INTEGER_T,           // integer data type determined by length
    SH_FLOAT_T,             // floating point type determined by length
    SH_ASCII_T,             // ascii string (char values 0-127)
    SH_UTF8_T,              // utf-8 string
    SH_UTF16_T,             // utf-16 string
    SH_JSON_T,              // json string
    SH_XML_T,               // xml string
    SH_STRUCT_T,            // binary struct
} sh_type_e;


#endif // SHARED_H_
