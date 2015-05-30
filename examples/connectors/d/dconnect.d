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

import std.stdio;
import std.string;
import core.stdc.string;
import core.memory;

import shrq;

void main(string args[])
{
    if (args.length < 2 || args.length > 3) {
        writeln("dconnect <inqueue> [<outqueue>]");
        return;
    }

    shr_q_s *in_q = null;
    shr_q_s *out_q = null;


    sh_status_e status = shr_q_open(&in_q, args[1].toStringz(), SQ_READ_ONLY);
    if (status == SH_ERR_EXIST) {
        status = shr_q_create(&in_q, args[1].toStringz(), 0, SQ_READ_ONLY);
        if (status) {
            writefln("error input queue:  %s\n", fromStringz(shr_q_explain(status)));
            return;
        }
    } else if (status) {
        writefln("error input queue:  %s\n", fromStringz(shr_q_explain(status)));
        return;
    }

    if (args.length == 3) {
        status = shr_q_open(&out_q, args[2].toStringz(), SQ_WRITE_ONLY);
        if (status == SH_ERR_EXIST) {
            status = shr_q_create(&out_q, args[2].toStringz(), 0, SQ_WRITE_ONLY);
            if (status) {
                writefln("error output queue:  %s\n", fromStringz(shr_q_explain(status)));
                return;
            }
        } else if (status) {
            writefln("error output queue:  %s\n", fromStringz(shr_q_explain(status)));
            return;
        }
    }

    sq_item_s item;
    do {
        item = shr_q_remove_wait(in_q, &item.buffer, &item.buf_size);
        if (item.status) {
            writefln("error input queue:  %s\n", fromStringz(shr_q_explain(status)));
            break;
        }
        char* ptr = cast(char*)GC.calloc(item.length + 1);
        memcpy(ptr, item.value, item.length);
        char[] array = fromStringz(ptr);
        auto output = array ~ ", d connect";
        if (out_q == null) {
            writeln(output);
        } else {
            status = shr_q_add(out_q, cast(void*)output.toStringz(), output.length);
            if (status) {
                writefln("error output queue:  %s\n", fromStringz(shr_q_explain(status)));
                break;
            }
        }
    } while (item.status == SH_OK);

    if (in_q) {
        shr_q_close(&in_q);
    }
    if (out_q) {
        shr_q_close(&out_q);
    }
}
