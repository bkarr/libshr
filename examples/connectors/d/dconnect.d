
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

    char[256] qname;
    memcpy(&qname[0], args[1].toStringz(), args[1].length);
    qname[args[1].length] = '\0';

    sh_status_e status = shr_q_open(&in_q, cast(char*)qname, SQ_READ_ONLY);
    if (status == SH_ERR_EXIST) {
        status = shr_q_create(&in_q, cast(char*)qname, 0, SQ_READ_ONLY);
        if (status) {
            writefln("error input queue:  %s\n", fromStringz(shr_q_explain(status)));
            return;
        }
    } else if (status) {
        writefln("error input queue:  %s\n", fromStringz(shr_q_explain(status)));
        return;
    }

    if (args.length == 3) {
        memcpy(&qname[0], args[2].toStringz(), args[2].length);
        qname[args[2].length] = '\0';
        status = shr_q_open(&out_q, cast(char*)qname, SQ_WRITE_ONLY);
        if (status == SH_ERR_EXIST) {
            status = shr_q_create(&out_q, cast(char*)qname, 0, SQ_WRITE_ONLY);
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
