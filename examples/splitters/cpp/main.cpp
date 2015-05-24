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
