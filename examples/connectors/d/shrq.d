extern (C):
struct shr_q {};
struct timespec {};
alias shr_q shr_q_s;

enum
{
    SH_OK,
    SH_RETRY,
    SH_ERR_EMPTY,
    SH_ERR_LIMIT,
    SH_ERR_ARG,
    SH_ERR_NOMEM,
    SH_ERR_ACCESS,
    SH_ERR_EXIST,
    SH_ERR_STATE,
    SH_ERR_PATH,
    SH_ERR_NOSUPPORT,
    SH_ERR_SYS,
}
alias int sh_status_e;

sh_status_e  shr_q_create(shr_q_s **q, char *name, uint max_depth, sq_mode_e mode);
sh_status_e shr_q_open(shr_q_s **q, char *name, sq_mode_e mode);
sh_status_e  shr_q_add(shr_q_s *q, void *value, long length);
sq_item_s  shr_q_remove_wait(shr_q_s *q, void **buffer, long *buff_size);
char * shr_q_explain(sh_status_e status);
sh_status_e  shr_q_close(shr_q_s **q);

enum
{
    SQ_EVNT_NONE,           // non-event
    SQ_EVNT_INIT,           // first item added to queue
    SQ_EVNT_DEPTH,          // max depth reached
    SQ_EVNT_TIME,           // max time limit reached
    SQ_EVNT_LEVEL           // depth level reached
}
alias int sq_event_e;

enum
{
    SQ_IMMUTABLE,           // queue instance unable to modify queue contents
    SQ_READ_ONLY,           // queue instance able to remove items from queue
    SQ_WRITE_ONLY,          // queue instance able to add items to queue
    SQ_READWRITE            // queue instance can add/remove items
}
alias int sq_mode_e;

struct sq_item
{
    sh_status_e status;
    long length;
    void *value;
    timespec *timestamp;
    void *buffer;
    long buf_size;
}
alias sq_item sq_item_s;
