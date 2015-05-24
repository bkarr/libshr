
import std.stdio;
import std.string;
import core.stdc.string;
import core.memory;

import shrq;

void main(string args[])
{
    if (args.length < 4 || args.length > 4) {
        writeln("dsplitter <inqueue> <outqueue1> <outqueue2>");
        return;
    }

    shr_q_s *in_q = null;
    shr_q_s *out_q_1 = null;
    shr_q_s *out_q_2 = null;


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

    status = shr_q_open(&out_q_1, args[2].toStringz(), SQ_WRITE_ONLY);
    if (status == SH_ERR_EXIST) {
        status = shr_q_create(&out_q_1, cast(immutable(char)*)args[2].toStringz(), 0, SQ_WRITE_ONLY);
        if (status) {
            writefln("error output queue 1:  %s\n", fromStringz(shr_q_explain(status)));
            return;
        }
    } else if (status) {
        writefln("error output queue 1:  %s\n", fromStringz(shr_q_explain(status)));
        return;
    }

    status = shr_q_open(&out_q_2, args[3].toStringz(), SQ_WRITE_ONLY);
    if (status == SH_ERR_EXIST) {
        status = shr_q_create(&out_q_2, cast(immutable(char)*)args[3].toStringz(), 0, SQ_WRITE_ONLY);
        if (status) {
            writefln("error output queue 2:  %s\n", fromStringz(shr_q_explain(status)));
            return;
        }
    } else if (status) {
        writefln("error output queue 2:  %s\n", fromStringz(shr_q_explain(status)));
        return;
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
        auto output = array ~ ", d splitter";
        status = shr_q_add(out_q_1, cast(void*)output.toStringz(), output.length);
        if (status) {
            writefln("error output queue 1:  %s\n", fromStringz(shr_q_explain(status)));
            break;
        }
        status = shr_q_add(out_q_2, cast(void*)output.toStringz(), output.length);
        if (status) {
            writefln("error output queue 2:  %s\n", fromStringz(shr_q_explain(status)));
            break;
        }
    } while (item.status == SH_OK);

    if (in_q) {
        shr_q_close(&in_q);
    }
    if (out_q_1) {
        shr_q_close(&out_q_1);
    }
    if (out_q_2) {
        shr_q_close(&out_q_2);
    }
}
