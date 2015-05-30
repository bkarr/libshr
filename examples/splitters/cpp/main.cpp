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

#include <shared_q.h>
#include <iostream>
#include <string>

using namespace std;

int main(int argc, char *argv[])
{
    if (argc < 4 || argc > 4) {
        cout << "cppsplitter <inqueue<outqueue1> <outqueue2>" << endl;
    }

    shr_q_s *in = NULL;
    shr_q_s *out1 = NULL;
    shr_q_s *out2 = NULL;

    sh_status_e status = shr_q_open(&in, argv[1], SQ_READ_ONLY);
    if (status == SH_ERR_EXIST) {
        status = shr_q_create(&in, argv[1], 0, SQ_READ_ONLY);
        if (status) {
            cout << "error input queue:  " << shr_q_explain(status) << endl;
            return 0;
        }
    } else if (status) {
        cout << "error input queue:  " << shr_q_explain(status) << endl;
        return 0;
    }

    status = shr_q_open(&out1, argv[2], SQ_WRITE_ONLY);
    if (status == SH_ERR_EXIST) {
        status = shr_q_create(&out1, argv[2], 0, SQ_WRITE_ONLY);
        if (status) {
            cout << "error output queue 1:  " << shr_q_explain(status) << endl;
            return 0;
        }
    } else if (status) {
        cout << "error output queue 1:  " << shr_q_explain(status) << endl;
        return 0;
    }

    status = shr_q_open(&out2, argv[3], SQ_WRITE_ONLY);
    if (status == SH_ERR_EXIST) {
        status = shr_q_create(&out2, argv[3], 0, SQ_WRITE_ONLY);
        if (status) {
            cout << "error output queue 2:  " << shr_q_explain(status) << endl;
            return 0;
        }
    } else if (status) {
        cout << "error output queue 2:  " << shr_q_explain(status) << endl;
        return 0;
    }

    sq_item_s item = {};
    do {
        item = shr_q_remove_wait(in, &item.buffer, &item.buf_size);
        if (item.status) {
            cout << "error input queue:  " << shr_q_explain(item.status) << endl;
            break;
        }
        string output = string((char*)item.value, item.length) + ", c++ splitter";
        status = shr_q_add(out1, (void*)output.c_str(), output.size());
        if (status) {
            cout << "error output queue 1:  " << shr_q_explain(status) << endl;
            break;
        }
        status = shr_q_add(out2, (void*)output.c_str(), output.size());
        if (status) {
            cout << "error output queue 2:  " << shr_q_explain(status) << endl;
            break;
        }
    } while (item.status == SH_OK);

    if (in) {
        shr_q_close(&in);
    }
    if (out1) {
        shr_q_close(&out1);
    }
    if (out2) {
        shr_q_close(&out2);
    }
    return 0;
}
