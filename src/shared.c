/*
The MIT License (MIT)

Copyright (c) 2017-2023 Bryan Karr

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

#include <shared.h>

static char *status_str[] = {
	"success",
	"retry",
	"no items available",
	"max limit reached",
	"invalid argument",
	"not enough memory to satisfy remapuest",
	"permission error",
	"existence error",
	"invalid state",
	"problem with path name",
	"required operation not supported",
	"system error",
	"unable to update due to conflict",
	"no match found for key",
    "request exceeds acceptable bounds",
	"invalid status code for explain"
};


/*
    shr_explain -- return a null-terminated string explanation of status code

    returns non-NULL null-terminated string error explanation
*/
extern char *shr_explain(
    sh_status_e status          // status code
)   {
    if (status >= SH_ERR_MAX) {
        return status_str[SH_ERR_MAX];
    }
    return status_str[status];
}
