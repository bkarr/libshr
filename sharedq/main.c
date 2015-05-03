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
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/limits.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>


#define PERMIT "bhvx"
#define HEX_LINE_LEN 16
#define HEX_HDR_SPAN 256
#define SHR_OBJ_DIR "/dev/shm/"
#define FILE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

typedef void (*cmd_f)(int argc, char *argv[], int index);

typedef struct modifiers {
    bool      block;
    bool      help;
    bool      hex;
    bool      verbose;
} modifiers_s;

char *cmd_str[] = {
    "help",
    "create",
    "destroy",
    "list",
    "add",
    "remove",
    "drain",
    "watch",
    "monitor",
};

typedef enum cmd_codes {
    HELP,
    CREATE,
    DESTROY,
    LIST,
    ADD,
    REMOVE,
    DRAIN,
    WATCH,
    MONITOR,
    CODE_MAX
} cmd_code_e;

static int running = 1;
static sem_t adds;
static sem_t events;

void sharedq_help_create()
{
    printf("sharedq [modifiers] create <name> [<maxdepth>]\n");
    printf("\n  --creates a named queue in shared memory\n");
    printf("\n  where:\n");
    printf("  <name>\t\tname of queue\n");
    printf("  <maxdepth>\t\toptional maximum depth, defaults to largest possible value\n");
    printf("\n   modifiers\t\t effects\n");
    printf("  -----------\t\t---------\n");
    printf("  -h\t\t\tprints help for the specified command\n");
}

void sharedq_help_destroy()
{
    printf("sharedq [modifiers] destroy <name>\n");
    printf("\n  --destroys a named queue in shared memory\n");
    printf("\n  where <name> is name of an existing queue\n\n");
    printf("\n   modifiers\t\t effects\n");
    printf("  -----------\t\t---------\n");
    printf("  -h\t\t\tprints help for the specified command\n");
}

void sharedq_help_list()
{
    printf("sharedq [modifiers] list\n");
    printf("\n  --list of queues in shared memory\n\n");
    printf("\n   modifiers\t\t effects\n");
    printf("  -----------\t\t---------\n");
    printf("  -h\t\t\tprints help for the specified command\n");
    printf("  -v\t\t\tprints output with headers\n");
}

void sharedq_help_remove()
{
    printf("sharedq [modifiers] remove <name>\n");
    printf("\n  --remove an item from the specified queue\n");
    printf("\n  where <name> is name of an existing queue\n\n");
    printf("\n   modifiers\t\t effects\n");
    printf("  -----------\t\t---------\n");
    printf("  -b\t\t\tblocks waiting for an item to arrive\n");
    printf("  -h\t\t\tprints help for the specified command\n");
    printf("  -x\t\t\tprints output as hex dump\n");
}

void sharedq_help_add()
{
    printf("sharedq [modifiers] add <name> [<file>]\n");
    printf("\n  --add an item to the specified queue\n");
    printf("\n  where:\n");
    printf("  <name>\t\tname of queue\n");
    printf("  <file>  \t\tname of file whose contents to queue,\n");
    printf("  \t\t\tif omitted queue lines from stdin\n");
    printf("\n   modifiers\t\t effects\n");
    printf("  -----------\t\t---------\n");
    printf("  -h\t\t\tprints help for the specified command\n");
}

void sharedq_help_drain()
{
    printf("sharedq [modifiers] drain <name>\n");
    printf("\n  --drains all items in specified queue in hex format\n");
    printf("\n  where <name> is name of an existing queue\n\n");
    printf("\n   modifiers\t\t effects\n");
    printf("  -----------\t\t---------\n");
    printf("  -b\t\t\tblocks waiting for an item to arrive\n");
    printf("  -h\t\t\tprints help for the specified command\n");
    printf("  -x\t\t\tprints output as hex dump\n");
}


void sharedq_help_watch()
{
    printf("sharedq [modifiers] watch <name>\n");
    printf("\n  --listens for an item being added to the specified queue when empty\n");
    printf("\n   modifiers\t\t effects\n");
    printf("  -----------\t\t---------\n");
    printf("  -h\t\t\tprints help for the specified command\n");
}


void sharedq_help_monitor()
{
    printf("sharedq [modifiers] monitor <name>\n");
    printf("\n  --monitors queue for events\n");
    printf("\n   modifiers\t\t effects\n");
    printf("  -----------\t\t---------\n");
    printf("  -h\t\t\tprints help for the specified command\n");
}


void sharedq_help(int argc, char *argv[], int index)
{
    printf("sharedq [modifiers] <cmd>\n");
    printf("\n   cmds\t\t\t actions\n");
    printf("  ------\t\t----------\n");
    printf("  add\t\t\tadd item to queue\n");
    printf("  create\t\tcreate queue\n");
    printf("  destroy\t\tdestroy queue\n");
    printf("  drain\t\t\tdrains items in queue\n");
    printf("  help\t\t\tprint list of commands\n");
    printf("  list\t\t\tlist of queues\n");
    printf("  monitor\t\tmonitors queue for events\n");
    printf("  remove\t\tremove item from queue\n");
    printf("  watch\t\t\tlisten for add to empty queue\n");
    printf("\n   modifiers\t\t effects\n");
    printf("  -----------\t\t---------\n");
    printf("  -b\t\t\tblocks waiting for an item to arrive\n");
    printf("  -h\t\t\tprints help for the specified command\n");
    printf("  -x\t\t\tprints output as hex dump\n");
    printf("  -v\t\t\tprints output with headers\n");
}


int64_t sharedq_atol(
    char *array,
    int length
)   {
    int64_t result = 0;
    int i = 0;
    while (array[i] == ' ' && i < length) {
        i++;
    }
    while (array[i] == '0' && i < length) {
        i++;
    }
    if (i < length && array[i] >= '0' && array[i] <= '9') {
        result += array[i++] - '0';
        for (; i < length && array[i] >= '0' && array[i] <= '9'; i++) {
            result *= 10;
            result += array[i] - '0';
        }
    }
    if (i < length) {
        return -1;
    }
    return result;
}


bool contains_param(
    char *string,
    char c
)   {
    int i = strlen(string) - 1;
    for (; i >= 0; i--) {
        if (string[i] == c) {
            return true;
        }
    }
    return false;
}


modifiers_s parse_modifiers(
    int argc,
    char **argv,
    int index,
    char *pattern
)   {
    modifiers_s result = {0};
    int i;
    char *param;

    for (i = 1; i < index; i++) {
        param = argv[i];
        if (param[0] != '-') {
            printf("invalid modifier %s\n", argv[i]);
            exit(1);
        }
        if (!contains_param(PERMIT, param[1])) {
            printf("error: unrecognized modifier %s\n", argv[i]);
            exit(1);
        }
        if (!contains_param(pattern, param[1])) {
            printf("warning: invalid modifier %s will be ignored\n\n", argv[i]);
            continue;
        }
        switch (param[1]) {
            case 'b' :
                result.block = true;
                break;
            case 'h' :
                result.help = true;
                break;
            case 'v' :
                result.verbose = true;
                break;
            case 'x' :
                result.hex = true;
                break;
            default :
                printf("unrecognized modifier or parameter\n");
                exit(1);
        }
    }

    return result;
}

void sharedq_create(int argc, char *argv[], int index)
{
    if ((argc - index + 1) < 3 || (argc - index + 1) > 4) {
        sharedq_help_create();
        return;
    }

    modifiers_s param = parse_modifiers(argc, argv, index, "h");

    if (param.help) {
        sharedq_help_create();
        return;
    }

    int64_t maxsize = 0;
    if ((argc - index + 1) == 4) {
        maxsize = sharedq_atol(argv[index + 2], strlen(argv[index + 2]));
        if (maxsize < 0) {
            printf("sharedq:  invalid queue maxsize argument\n");
            return;
        }
    }
    shr_q_s *q = NULL;
    sh_status_e status = shr_q_create(&q, argv[index + 1], maxsize, SQ_IMMUTABLE);
    if (status == SH_ERR_ARG) {
        printf("sharedq:  invalid argument for create function\n");
        return;
    }
    if (status == SH_ERR_ACCESS) {
        printf("sharedq:  permission error for queue name\n");
        return;
    }
    if (status == SH_ERR_EXIST) {
        printf("sharedq:  queue name already exists\n");
        return;
    }
    if (status == SH_ERR_PATH) {
        printf("sharedq:  error in queue name path\n");
        return;
    }
    if (status == SH_ERR_SYS) {
        printf("sharedq:  system call error\n");
        return;
    }
}

void sharedq_destroy(int argc, char *argv[], int index)
{
    if ((argc - index + 1) < 3 || (argc - index + 1) > 3) {
        sharedq_help_destroy();
        return;
    }

    modifiers_s param = parse_modifiers(argc, argv, index, "h");

    if (param.help) {
        sharedq_help_destroy();
        return;
    }

    shr_q_s *q = NULL;
    sh_status_e status = shr_q_open(&q, argv[index + 1], SQ_IMMUTABLE);
    if (status == SH_ERR_ARG) {
        printf("sharedq:  invalid argument for open function\n");
        return;
    }
    if (status == SH_ERR_ACCESS) {
        printf("sharedq:  permission error for queue name\n");
        return;
    }
    if (status == SH_ERR_EXIST) {
        printf("sharedq:  queue name does not exist\n");
        return;
    }
    if (status == SH_ERR_PATH) {
        printf("sharedq:  error in queue name path\n");
        return;
    }
    if (status == SH_ERR_SYS) {
        printf("sharedq:  system call error\n");
        return;
    }
    status = shr_q_destroy(&q);
    if (status == SH_ERR_ARG) {
        printf("sharedq:  invalid argument for destroy function\n");
        return;
    }
    if (status == SH_ERR_SYS) {
        printf("sharedq:  system call error\n");
        return;
    }
}

void sharedq_list(int argc, char *argv[], int index)
{
    if ((argc - index + 1) < 2 || (argc - index + 1) > 2) {
        sharedq_help_list();
        return;
    }

    modifiers_s param = parse_modifiers(argc, argv, index, "hv");

    if (param.help) {
        sharedq_help_list();
        return;
    }

    struct dirent *entry;
    DIR *dir;

    dir = opendir(SHR_OBJ_DIR);
    if (dir == NULL) {
        printf("sharedq: path does not exist to shared memory directory\n");
        exit(1);
    }

    if (param.verbose) {
        printf("\n\t queues \t\t depth \t\t size \n");
        printf("\t--------\t\t-------\t\t------\n");
    }
    char path[PATH_MAX];
    int offset = strlen(SHR_OBJ_DIR);
    memcpy(&path[0], SHR_OBJ_DIR, offset);
    shr_q_s *q = NULL;
    struct stat st;
    while((entry = readdir(dir))) {
        memcpy(&path[0] + offset, entry->d_name, strlen(entry->d_name) + 1);
        int rc = stat(path, &st);
        if (rc == 0 && S_ISREG(st.st_mode)) {
            if (shr_q_open(&q, entry->d_name, SQ_IMMUTABLE) == SH_OK) {
                if (param.verbose) {
                    printf("\t%-16s\t%7li\t\t%li\n",
                        entry->d_name, shr_q_count(q), st.st_size);
                } else {
                    printf("%s\n", entry->d_name);
                }
                shr_q_close(&q);
            }
        }
    }

    if (param.verbose) {
        putchar('\n');
    }

    closedir(dir);
}


void queue_from_file(
    shr_q_s *q,
    char *fname
)   {
    int fd = open(fname, O_RDONLY, FILE_MODE);
    if (fd < 0) {
        printf("sharedq: unable to open file\n");
        return;
    }

    struct stat st;
    int rc = fstat(fd, &st);
    if (rc < 0) {
        printf("sharedq: invalid file\n");
        close(fd);
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        printf("sharedq: not a regular file\n");
        close(fd);
        return;
    }

    void *data = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        printf("sharedq: unable to map file\n");
        close(fd);
        return;
    }

    sh_status_e status = shr_q_add(q, data, st.st_size);
    if (status == SH_ERR_ARG) {
        printf("sharedq:  invalid argument for add function\n");
    }
    if (status == SH_ERR_LIMIT) {
        printf("sharedq:  queue at depth limit\n");
    }
    if (status == SH_ERR_NOMEM) {
        printf("sharedq:  not enough memory to complete add\n");
    }
    munmap(data, st.st_size);
    close(fd);
}

#define BUFFSIZE 256
char *readline(
    FILE *stream,
    char *str
)   {
    char buffer[BUFFSIZE];
    char *line = NULL;
    bool nobreak = true;

    if (str == NULL) {
        str = calloc(1, BUFFSIZE);
    }
    memset(str, 0, strlen(str));

    while (nobreak && (line = fgets(buffer, BUFFSIZE, stream)) != NULL) {
        int len = strlen(line);
        if (line[len - 1] == '\n') {
            len--;
            line[len] = 0;
            nobreak = false;
        }
        if (len == 0) {
            return str;
        }
        str = realloc(str, strlen(str) + len + 1);
        strcat(str, line);
    }

    if (line == NULL) {
        free(str);
        str = NULL;
    }
    return str;
}


void queue_from_stdin(
    shr_q_s *q
)   {
    char *line = NULL;
    while((line = readline(stdin, line)) != NULL) {
        if (strlen(line) == 0) {
            free(line);
            break;
        }
        sh_status_e status = shr_q_add(q, line, strlen(line));
        if (status == SH_ERR_ARG) {
            printf("sharedq:  invalid argument for add function\n");
            free(line);
            return;
        }
        if (status == SH_ERR_LIMIT) {
            printf("sharedq:  queue at depth limit\n");
            free(line);
            return;
        }
        if (status == SH_ERR_NOMEM) {
            printf("sharedq:  not enough memory to complete add\n");
            free(line);
            return;
        }
    }
}


void sharedq_add(int argc, char *argv[], int index)
{
    if ((argc - index + 1) < 3 || (argc - index + 1) > 4) {
        sharedq_help_add();
        return;
    }

    modifiers_s param = parse_modifiers(argc, argv, index, "h");


    if (param.help) {
        sharedq_help_add();
        return;
    }

    shr_q_s *q = NULL;
    sh_status_e status = shr_q_open(&q, argv[index + 1], SQ_WRITE_ONLY);
    if (status == SH_ERR_ARG) {
        printf("sharedq:  invalid argument for open function\n");
        return;
    }
    if (status == SH_ERR_ACCESS) {
        printf("sharedq:  permission error for queue name\n");
        return;
    }
    if (status == SH_ERR_EXIST) {
        printf("sharedq:  queue name does not exist\n");
        return;
    }
    if (status == SH_ERR_PATH) {
        printf("sharedq:  error in queue name path\n");
        return;
    }
    if (status == SH_ERR_SYS) {
        printf("sharedq:  system call error\n");
        return;
    }

    if ((argc - index + 1) == 4) {
        queue_from_file(q, argv[index + 2]);
    } else {
        queue_from_stdin(q);
    }

    shr_q_close(&q);
}


void hex_format(
    uint8_t *data,
    int64_t length
)   {
    int32_t output = 0;

    while (output < length) {
        // calculate number of bytes to output
        int32_t num_bytes = HEX_LINE_LEN;
        if (output + HEX_LINE_LEN > length) {
            num_bytes = length - output;
        }

        // print header if span interval is reached
        if (output % HEX_HDR_SPAN == 0) {
            printf("\n     0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F");
            printf("\n     -----------------------------------------------");
        }

        // print displacement column
        printf("\n%04X ", output);

        // print hex columns
        int32_t i;
        for (i = 0; i < num_bytes; ++i) {
           printf("%02X ", data[i + output] & 0xff);
        }

        // fill columns with spaces if not a complete line
        if (num_bytes != HEX_LINE_LEN) {
           for(i = 0; i < (HEX_LINE_LEN - num_bytes); ++i) {
               printf("   ");
           }
        }

        // print character columns
        printf("   ");
        for (i = 0; i < num_bytes; i++) {
            if (isprint(data[i + output])) {
                putchar(data[i + output]);
            } else {
                putchar('.');
            }
        }
       output += num_bytes;
    }

    putchar('\n');
}


void sharedq_remove(int argc, char *argv[], int index)
{
    if ((argc - index + 1) < 3 || (argc - index + 1) > 3)
    {
        sharedq_help_remove();
        return;
    }

    modifiers_s param = parse_modifiers(argc, argv, index, "bhx");

    if (param.help) {
        sharedq_help_remove();
        return;
    }

    shr_q_s *q = NULL;
    sh_status_e status = shr_q_open(&q, argv[index + 1], SQ_READ_ONLY);
    if (status == SH_ERR_ARG) {
        printf("sharedq:  invalid argument for open function\n");
        return;
    }
    if (status == SH_ERR_ACCESS) {
        printf("sharedq:  permission error for queue name\n");
        return;
    }
    if (status == SH_ERR_EXIST) {
        printf("sharedq:  queue name does not exist\n");
        return;
    }
    if (status == SH_ERR_PATH) {
        printf("sharedq:  error in queue name path\n");
        return;
    }
    if (status == SH_ERR_SYS) {
        printf("sharedq:  system call error\n");
        return;
    }

    sq_item_s item = {0};

    if (param.block) {
        item = shr_q_remove_wait(q, &item.buffer, &item.buf_size);
    } else {
        item = shr_q_remove(q, &item.buffer, &item.buf_size);
    }

    if (item.status == SH_ERR_ARG) {
        printf("sharedq:  invalid argument for remove function\n");
    } else if (item.status == SH_ERR_EMPTY) {
        printf("sharedq:  queue is empty\n");
    } else if (item.status == SH_ERR_NOMEM) {
        printf("sharedq:  not enough memory to complete remove\n");
    } else {
        if (param.hex) {
            hex_format(item.value, item.length);
        } else {
            printf("%s\n", (char *)item.value);
        }
    }

    shr_q_close(&q);
}


void sharedq_drain(int argc, char *argv[], int index)
{
    if ((argc - index + 1) < 3 || (argc - index + 1) > 3)
    {
        sharedq_help_drain();
        return;
    }

    modifiers_s param = parse_modifiers(argc, argv, index, "bhx");

    if (param.help)
    {
        sharedq_help_drain();
        return;
    }
    shr_q_s *q = NULL;
    sh_status_e status = shr_q_open(&q, argv[index + 1], SQ_READ_ONLY);
    if (status == SH_ERR_ARG) {
        printf("sharedq:  invalid argument for open function\n");
        return;
    }
    if (status == SH_ERR_ACCESS) {
        printf("sharedq:  permission error for queue name\n");
        return;
    }
    if (status == SH_ERR_EXIST) {
        printf("sharedq:  queue name does not exist\n");
        return;
    }
    if (status == SH_ERR_PATH) {
        printf("sharedq:  error in queue name path\n");
        return;
    }
    if (status == SH_ERR_SYS) {
        printf("sharedq:  system call error\n");
        return;
    }

    sq_item_s item = {0};

    do {
        if (param.block) {
            item = shr_q_remove_wait(q, &item.buffer, &item.buf_size);
        } else {
            item = shr_q_remove(q, &item.buffer, &item.buf_size);
        }
        if (item.status == SH_ERR_ARG) {
            printf("sharedq:  invalid argument for remove function\n");
        } else if (item.status == SH_ERR_EMPTY) {
            printf("sharedq:  queue is empty\n");
        } else if (item.status == SH_ERR_NOMEM) {
            printf("sharedq:  not enough memory to complete remove\n");
        } else {
            if (param.hex) {
                hex_format(item.value, item.length);
            } else {
                printf("%s\n", (char *)item.value);
            }
        }
    } while (item.status == SH_OK);


    shr_q_close(&q);
}


void sharedq_monitor(int argc, char *argv[], int index)
{
    if ((argc - index + 1) < 3 || (argc - index + 1) > 3)
    {
        sharedq_help_monitor();
        return;
    }

    modifiers_s param = parse_modifiers(argc, argv, index, "h");

    if (param.help)
    {
        sharedq_help_monitor();
        return;
    }

    shr_q_s *q = NULL;
    sh_status_e status = shr_q_open(&q, argv[index + 1], SQ_READ_ONLY);
    if (status == SH_ERR_ARG) {
        printf("sharedq:  invalid argument for open function\n");
        return;
    }
    if (status == SH_ERR_ACCESS) {
        printf("sharedq:  permission error for queue name\n");
        return;
    }
    if (status == SH_ERR_EXIST) {
        printf("sharedq:  queue name does not exist\n");
        return;
    }
    if (status == SH_ERR_PATH) {
        printf("sharedq:  error in queue name path\n");
        return;
    }
    if (status == SH_ERR_SYS) {
        printf("sharedq:  system call error\n");
        return;
    }

    shr_q_monitor(q, SIGUSR2);

    while(running) {
        int rc = sem_wait(&events);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        sq_event_e event = SQ_EVNT_NONE;
        do {
            event = shr_q_event(q);
            switch(event) {
                case SQ_EVNT_INIT :
                    printf("Event: initial add of item to queue\n");
                    break;
                case SQ_EVNT_DEPTH :
                    printf("Event: max depth reached\n");
                    break;
                case SQ_EVNT_LEVEL :
                    printf("Event: depth limit reached\n");
                    break;
                case SQ_EVNT_TIME :
                    printf("Event: time limit on queue reached\n");
                    break;
                default :
                    break;
            }
        } while (event != SQ_EVNT_NONE);
    }

    shr_q_close(&q);
}


void sharedq_watch(int argc, char *argv[], int index)
{
    if ((argc - index + 1) < 3 || (argc - index + 1) > 3)
    {
        sharedq_help_watch();
        return;
    }

    modifiers_s param = parse_modifiers(argc, argv, index, "h");

    if (param.help)
    {
        sharedq_help_watch();
        return;
    }

    shr_q_s *q = NULL;
    sh_status_e status = shr_q_open(&q, argv[index + 1], SQ_READ_ONLY);
    if (status == SH_ERR_ARG) {
        printf("sharedq:  invalid argument for open function\n");
        return;
    }
    if (status == SH_ERR_ACCESS) {
        printf("sharedq:  permission error for queue name\n");
        return;
    }
    if (status == SH_ERR_EXIST) {
        printf("sharedq:  queue name does not exist\n");
        return;
    }
    if (status == SH_ERR_PATH) {
        printf("sharedq:  error in queue name path\n");
        return;
    }
    if (status == SH_ERR_SYS) {
        printf("sharedq:  system call error\n");
        return;
    }

    shr_q_listen(q, SIGUSR1);

    while(running) {
        int rc = sem_wait(&adds);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        printf("Item added to empty queue %s\n", argv[index + 1]);
    }

    shr_q_close(&q);
}


cmd_f cmds[] = {
    sharedq_help,
    sharedq_create,
    sharedq_destroy,
    sharedq_list,
    sharedq_add,
    sharedq_remove,
    sharedq_drain,
    sharedq_watch,
    sharedq_monitor,
};


static void sig_usr(int signo)
{
    if (signo == SIGUSR1) {
        sem_post(&adds);
    } else if (signo == SIGUSR2) {
        sem_post(&events);
    } else if (signo == SIGTERM) {
        running = 0;
    }
}


static void set_signal_handlers(void)
{
    if (signal(SIGUSR1, sig_usr) == SIG_ERR) {
        printf("cannot catch SIGUSR1\n");
    }
    if (signal(SIGUSR2, sig_usr) == SIG_ERR) {
        printf("cannot catch SIGUSR2\n");
    }
    if (signal(SIGTERM, sig_usr) == SIG_ERR) {
        printf("cannot catch SIGTERM\n");
    }
}


int main(
    int argc,
    char *argv[]
)   {
    int index = 1;
    if (argc < 2) {
        sharedq_help(argc, argv, index);
        return 0;
    }

    while (index < argc) {
        if (argv[index][0] != '-') {
            break;
        }
        index++;
    }

    if (argc == index) {
        sharedq_help(argc, argv, index);
        return 0;
    }

    int rc = sem_init(&adds, 0, 0);
    if (rc < 0) {
        printf("unable to initialize add semapahore\n");
        return 0;
    }

    rc = sem_init(&events, 0, 1);
    if (rc < 0) {
        printf("unable to initialize event semapahore\n");
        return 0;
    }

    set_signal_handlers();

    int length = strlen(argv[index]);
    int i;
    for (i = 0; i < CODE_MAX; i++) {
        if ((length == strlen(cmd_str[i])) && (memcmp(argv[index],
                cmd_str[i],
                length) == 0)) {
            (*cmds[i])(argc, argv, index);
            break;
        }
    }

    if (i == CODE_MAX) {
        sharedq_help(argc, argv, index);
    }
    return 0;
}
