#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdbool.h>
#include <sys/types.h>
#include <linux/types.h>
#include "columns.h"
#include "xcapture.h"
#include "xcapture_user.h"

#define UNUSED_COLUMNS_ARGS() do { XCAP_UNUSED(event); XCAP_UNUSED(ctx); } while (0)

// Column selection state
bool active_columns[NUM_COLUMNS];
int active_column_indices[NUM_COLUMNS];
int num_active_columns = 0;

// Predefined column sets
const char *narrow_columns = "tid,tgid,state,username,exe,comm,syscall,filename";
const char *normal_columns = "timestamp,tid,tgid,state,username,exe,comm,syscall,filename";
const char *wide_columns = "timestamp,weight_us,off_us,tid,tgid,pidns,cgroup_id,state,username,exe,comm,"
                           "syscall,syscall_active,sysc_seq_num,sysc_us_so_far,sysc_arg1,filename,aiofilename,uringfilename,"
                           "sysc_entry_time,connection,conn_state,extra_info";

static int add_column_index(int column_index) {
    if (column_index < 0 || column_index >= NUM_COLUMNS)
        return -1;

    if (!active_columns[column_index]) {
        if (num_active_columns >= NUM_COLUMNS) {
            fprintf(stderr, "Error: column limit exceeded\n");
            return -1;
        }
        active_columns[column_index] = true;
        active_column_indices[num_active_columns++] = column_index;
    }

    return 0;
}

static int process_column_list(const char *column_list, bool reset_selection) {
    if (!column_list || !*column_list) {
        fprintf(stderr, "Error: empty column list\n");
        return -1;
    }

    if (reset_selection) {
        memset(active_columns, 0, sizeof(active_columns));
        num_active_columns = 0;
    }

    if (strcasecmp(column_list, "all") == 0) {
        for (int i = 0; i < NUM_COLUMNS; i++) {
            if (add_column_index(i) < 0)
                return -1;
        }

        if (reset_selection && num_active_columns == 0) {
            fprintf(stderr, "Error: no valid columns selected\n");
            return -1;
        }

        return 0;
    }

    char *list_copy = strdup(column_list);
    if (!list_copy) {
        fprintf(stderr, "Error: out of memory\n");
        return -1;
    }

    for (char *token = strtok(list_copy, ","); token; token = strtok(NULL, ",")) {
        while (*token && isspace((unsigned char)*token)) token++;
        char *end = token + strlen(token) - 1;
        while (end > token && isspace((unsigned char)*end)) *end-- = '\0';

        bool found = false;
        for (int i = 0; i < NUM_COLUMNS; i++) {
            if (strcasecmp(token, column_definitions[i].name) == 0) {
                if (add_column_index(i) < 0) {
                    free(list_copy);
                    return -1;
                }
                found = true;
                break;
            }
        }

        if (!found) {
            fprintf(stderr, "Error: invalid column name '%s'\n", token);
            fprintf(stderr, "Use -l/--list to see available columns\n");
            free(list_copy);
            return -1;
        }
    }

    free(list_copy);

    if (reset_selection && num_active_columns == 0) {
        fprintf(stderr, "Error: no valid columns selected\n");
        return -1;
    }

    return 0;
}

// Individual column formatters
static void format_timestamp(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%s", ctx->timestamp ? ctx->timestamp : "-");
}

static void format_weight_us(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%'ld", ctx->sample_weight_us);
}

static void format_off_us(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%'lld", ctx->off_us);
}

static void format_tid(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%d", event->pid);
}

static void format_tgid(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%d", event->tgid);
}

static void format_state(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%s", format_task_state(event->state, event->on_rq, event->on_cpu, event->migration_pending));
}

static void format_username(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%s", getusername(event->euid));
}

static void format_exe(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%s", (event->flags & PF_KTHREAD) ? "[kernel]" : event->exe_file);
}

static void format_comm(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%s", event->comm);
}

static void format_cmdline(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();

    __u32 clen = event->cmdline_len;

    if (clen == 0 || event->cmdline[0] == '\0') {
        snprintf(buf, len, "-");
        return;
    }

    if (clen >= MAX_CMDLINE_LEN)
        clen = MAX_CMDLINE_LEN - 1;

    char tmp[MAX_CMDLINE_LEN];
    __builtin_memset(tmp, 0, sizeof(tmp));
    __builtin_memcpy(tmp, event->cmdline, clen);

    for (unsigned int i = 0; i < clen; i++) {
        if (tmp[i] == '\0')
            tmp[i] = ' ';
    }

    int end = (int)clen - 1;
    while (end >= 0 && isspace((unsigned char)tmp[end]))
        end--;

    if (end < 0) {
        snprintf(buf, len, "-");
        return;
    }

    tmp[end + 1] = '\0';
    snprintf(buf, len, "%s", tmp);
}

static void format_syscall(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%s", (event->flags & PF_KTHREAD) ? "-" : safe_syscall_name(event->syscall_nr));
}

static void format_syscall_active(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    if (event->flags & PF_KTHREAD) {
        snprintf(buf, len, "-");
    //} else if (event->storage.sc_enter_time > 0) { // TODO remove
    } else if (event->storage.in_syscall_nr >= 0) {
        snprintf(buf, len, "%s", safe_syscall_name(event->storage.in_syscall_nr));
    } else {
        snprintf(buf, len, "?");
    }
}

static void format_sysc_us_so_far(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%'lld", ctx->sysc_us_so_far);
}

static void format_sysc_arg1(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%llx", event->syscall_args[0]);
}

static void format_sysc_arg2(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%llx", event->syscall_args[1]);
}

static void format_sysc_arg3(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%llx", event->syscall_args[2]);
}

static void format_sysc_arg4(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%llx", event->syscall_args[3]);
}

static void format_sysc_arg5(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%llx", event->syscall_args[4]);
}

static void format_sysc_arg6(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%llx", event->syscall_args[5]);
}

static void format_filename(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%s", event->filename[0] ? event->filename : "-");
}

static void format_aio_filename(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%s", event->aio_filename[0] ? event->aio_filename : "-");
}

static void format_uring_filename(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    if (event->ur_sq_filename[0]) {
        snprintf(buf, len, "%s", event->ur_sq_filename);
    } else if (event->ur_filename[0]) {
        snprintf(buf, len, "%s", event->ur_filename);
    } else {
        snprintf(buf, len, "-");
    }
}

static void format_sysc_entry_time(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%s", ctx->sysc_entry_time_str ? ctx->sysc_entry_time_str : "-");
}

static void format_sysc_seq_num(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%lld", event->storage.sc_sequence_num);
}

static void format_iorq_seq_num(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%lld", event->storage.iorq_sequence_num);
}

static void format_connection_col(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%s", ctx->conn_buf && ctx->conn_buf[0] ? ctx->conn_buf : "-");
}

static void format_conn_state(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%s", ctx->conn_state_str && ctx->conn_state_str[0] ? ctx->conn_state_str : "-");
}

static void format_extra_info(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    snprintf(buf, len, "%s", ctx->extra_info && ctx->extra_info[0] ? ctx->extra_info : "-");
}

static void format_kstack_hash(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    if (event->kstack_hash != 0) {
        snprintf(buf, len, "%016llx", event->kstack_hash);
    } else {
        snprintf(buf, len, "-");
    }
}

static void format_ustack_hash(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    if (event->ustack_hash != 0) {
        snprintf(buf, len, "%016llx", event->ustack_hash);
    } else {
        snprintf(buf, len, "-");
    }
}

static void format_pidns(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    if (event->storage.pid_ns_id != 0) {
        snprintf(buf, len, "%u", event->storage.pid_ns_id);
    } else {
        snprintf(buf, len, "-");
    }
}

static void format_cgroup_id(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    if (event->storage.cgroup_id != 0) {
        snprintf(buf, len, "%llu", event->storage.cgroup_id);
    } else {
        snprintf(buf, len, "-");
    }
}

static void format_trace_payload(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    __u32 plen = event->storage.trace_payload_len;
    if (plen == 0 || plen > TRACE_PAYLOAD_LEN) {
        snprintf(buf, len, "-");
        return;
    }

    char hex[TRACE_PAYLOAD_LEN * 2 + 1];
    bytes_to_hex(event->storage.trace_payload, plen, hex, sizeof(hex));
    snprintf(buf, len, "%s", hex);
}

static void format_trace_payload_len(char *buf, size_t len, const struct task_output_event *event, const column_context_t *ctx) {
    UNUSED_COLUMNS_ARGS();
    __u32 plen = event->storage.trace_payload_len;
    if (plen == 0 || plen > TRACE_PAYLOAD_LEN) {
        snprintf(buf, len, "-");
        return;
    }

    snprintf(buf, len, "%u", plen);
}

// Column definitions table
const column_def_t column_definitions[NUM_COLUMNS] = {
    [COL_TIMESTAMP]       = {"timestamp",       "TIMESTAMP",       -26, format_timestamp},
    [COL_WEIGHT_US]       = {"weight_us",       "WEIGHT_US",        9, format_weight_us},
    [COL_OFF_US]          = {"off_us",          "OFF_US",           6, format_off_us},
    [COL_TID]             = {"tid",             "TID",              7, format_tid},
    [COL_TGID]            = {"tgid",            "TGID",             7, format_tgid},
    [COL_STATE]           = {"state",           "STATE",          -10, format_state},
    [COL_USERNAME]        = {"username",        "USERNAME",       -16, format_username},
    [COL_EXE]             = {"exe",             "EXE",            -20, format_exe},
    [COL_COMM]            = {"comm",            "COMM",           -16, format_comm},
    [COL_CMDLINE]         = {"cmdline",         "CMDLINE",        -64, format_cmdline},
    [COL_SYSCALL]         = {"syscall",         "SYSCALL",        -20, format_syscall},
    [COL_SYSCALL_ACTIVE]  = {"syscall_active",  "SYSCALL_ACTIVE", -20, format_syscall_active},
    [COL_SYSC_US_SO_FAR]  = {"sysc_us_so_far",  "SYSC_US_SO_FAR",  16, format_sysc_us_so_far},
    [COL_SYSC_ARG1]       = {"sysc_arg1",       "SYSC_ARG1",       16, format_sysc_arg1},
    [COL_SYSC_ARG2]       = {"sysc_arg2",       "SYSC_ARG2",       16, format_sysc_arg2},
    [COL_SYSC_ARG3]       = {"sysc_arg3",       "SYSC_ARG3",       16, format_sysc_arg3},
    [COL_SYSC_ARG4]       = {"sysc_arg4",       "SYSC_ARG4",       16, format_sysc_arg4},
    [COL_SYSC_ARG5]       = {"sysc_arg5",       "SYSC_ARG5",       16, format_sysc_arg5},
    [COL_SYSC_ARG6]       = {"sysc_arg6",       "SYSC_ARG6",       16, format_sysc_arg6},
    [COL_FILENAME]        = {"filename",        "FILENAME",       -20, format_filename},
    [COL_AIO_FILENAME]    = {"aiofilename",     "AIOFILENAME",    -20, format_aio_filename},
    [COL_URING_FILENAME]  = {"uringfilename",   "URINGFILENAME",  -20, format_uring_filename},
    [COL_SYSC_ENTRY_TIME] = {"sysc_entry_time", "SYSC_ENTRY_TIME", -26, format_sysc_entry_time},
    [COL_SYSC_SEQ_NUM]    = {"sysc_seq_num",    "SYSC_SEQ_NUM",    12, format_sysc_seq_num},
    [COL_IORQ_SEQ_NUM]    = {"iorq_seq_num",    "IORQ_SEQ_NUM",    12, format_iorq_seq_num},
    [COL_CONNECTION]      = {"connection",      "CONNECTION",     -30, format_connection_col},
    [COL_CONN_STATE]      = {"conn_state",      "CONN_STATE",     -15, format_conn_state},
    [COL_EXTRA_INFO]      = {"extra_info",      "EXTRA_INFO",       0, format_extra_info},
    [COL_KSTACK_HASH]     = {"kstack_hash",     "KSTACK_HASH",    -16, format_kstack_hash},
    [COL_USTACK_HASH]     = {"ustack_hash",     "USTACK_HASH",    -16, format_ustack_hash},
    [COL_PIDNS]           = {"pidns",           "PIDNS",           10, format_pidns},
    [COL_CGROUP_ID]       = {"cgroup_id",       "CGROUP_ID",       18, format_cgroup_id},
    [COL_TRACE_PAYLOAD]   = {"trace_payload",   "TRACE_PAYLOAD",  -80, format_trace_payload},
    [COL_TRACE_PAYLOAD_LEN] = {"trace_payload_len", "TRACE_PAYLOAD_LEN", 12, format_trace_payload_len},
};

// Parse comma-separated column list
int parse_column_list(const char *column_list) {
    return process_column_list(column_list, true);
}

int append_column_list(const char *column_list) {
    return process_column_list(column_list, false);
}

bool column_is_active(column_id_t column)
{
    if (column < 0 || column >= NUM_COLUMNS)
        return false;

    return active_columns[column];
}

// List all available columns
void list_available_columns(void) {
    printf("Available columns for -g/--get-columns and -G/--append-columns options:\n\n");
    printf("%-20s  %-20s  %s\n", "Column Name", "Header", "Width");
    printf("%-20s  %-20s  %s\n", "-----------", "------", "-----");

    for (int i = 0; i < NUM_COLUMNS; i++) {
        printf("%-20s  %-20s  %d\n",
               column_definitions[i].name,
               column_definitions[i].header,
               column_definitions[i].width);
    }

    printf("\nPredefined column sets:\n");
    printf("  narrow:  %s\n", narrow_columns);
    printf("  normal:  %s\n", normal_columns);
    printf("  wide:    %s\n", wide_columns);
    printf("  all:     All available columns\n");
    printf("\nExample usage:\n");
    printf("  xcapture -g tid,comm,state,syscall\n");
    printf("  xcapture -G connection,extra_info\n");
}

// Print column headers for stdout
void print_column_headers(void) {
    for (int i = 0; i < num_active_columns; i++) {
        int col_id = active_column_indices[i];
        const column_def_t *col = &column_definitions[col_id];

        if (col->width == 0) {
            fputs(col->header, stdout);
        } else if (col->width < 0) {
            printf("%-*s", -col->width, col->header);
        } else {
            printf("%*s", col->width, col->header);
        }

        if (i < num_active_columns - 1) {
            printf("  ");
        }
    }
    printf("\n");
}

// Format a single output line
void format_stdout_line(const struct task_output_event *event, const column_context_t *ctx) {
    for (int i = 0; i < num_active_columns; i++) {
        int col_id = active_column_indices[i];
        const column_def_t *col = &column_definitions[col_id];

        char value[512];
        col->format_fn(value, sizeof(value), event, ctx);

        if (col->width == 0) {
            fputs(value, stdout);
        } else if (col->width < 0) {
            printf("%-*s", -col->width, value);
        } else {
            printf("%*s", col->width, value);
        }

        if (i < num_active_columns - 1) {
            printf("  ");
        }
    }
    printf("\n");
}
