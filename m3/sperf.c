#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MAX_SYSCALLS 512
#define NAME_LEN 64
#define REFRESH_MS 100
#define CLEAR_BYTES 80

extern char **environ;

struct syscall_stat {
    char name[NAME_LEN];
    double seconds;
};

struct stats {
    struct syscall_stat calls[MAX_SYSCALLS];
    size_t count;
    double total_seconds;
    unsigned long generation;
};

static long long now_ms(void) {
    struct timeval tv;

    if (gettimeofday(&tv, NULL) != 0) {
        return 0;
    }

    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static char *xstrdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);

    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, s, len);
    return copy;
}

static char *join_path(const char *dir, const char *name) {
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    int need_slash = dir_len > 0 && dir[dir_len - 1] != '/';
    char *path = malloc(dir_len + (size_t)need_slash + name_len + 1);

    if (path == NULL) {
        return NULL;
    }

    memcpy(path, dir, dir_len);
    if (need_slash) {
        path[dir_len++] = '/';
    }
    memcpy(path + dir_len, name, name_len + 1);
    return path;
}

static char *resolve_executable(const char *command) {
    char *path_copy;
    char *saveptr = NULL;
    char *part;
    const char *path_env;

    if (command[0] == '/' || strchr(command, '/') != NULL) {
        if (access(command, X_OK) == 0) {
            return xstrdup(command);
        }
        return NULL;
    }

    path_env = getenv("PATH");
    if (path_env == NULL || path_env[0] == '\0') {
        path_env = "/bin:/usr/bin";
    }

    path_copy = xstrdup(path_env);
    if (path_copy == NULL) {
        return NULL;
    }

    for (part = strtok_r(path_copy, ":", &saveptr);
         part != NULL;
         part = strtok_r(NULL, ":", &saveptr)) {
        char *candidate = join_path(part[0] == '\0' ? "." : part, command);

        if (candidate == NULL) {
            free(path_copy);
            return NULL;
        }

        if (access(candidate, X_OK) == 0) {
            free(path_copy);
            return candidate;
        }

        free(candidate);
    }

    free(path_copy);
    return NULL;
}

static int parse_elapsed_seconds(const char *line, double *seconds) {
    size_t end = strlen(line);
    size_t start;
    char *parse_end;

    while (end > 0 && isspace((unsigned char)line[end - 1])) {
        end--;
    }
    if (end == 0 || line[end - 1] != '>') {
        return 0;
    }

    start = end - 1;
    while (start > 0 && line[start] != '<') {
        start--;
    }
    if (line[start] != '<') {
        return 0;
    }

    errno = 0;
    *seconds = strtod(line + start + 1, &parse_end);
    while (parse_end < line + end - 1 && isspace((unsigned char)*parse_end)) {
        parse_end++;
    }

    return errno == 0 && parse_end == line + end - 1 && *seconds >= 0.0;
}

static int parse_syscall_name(const char *line, char *name, size_t size) {
    size_t i = 0;
    size_t out = 0;

    if (strncmp(line, "<... ", 5) == 0) {
        i = 5;
        while (line[i] != '\0' && !isspace((unsigned char)line[i]) &&
               line[i] != '>') {
            if (out + 1 < size) {
                name[out++] = line[i];
            }
            i++;
        }
        name[out] = '\0';
        return out > 0;
    }

    if (!(isalpha((unsigned char)line[0]) || line[0] == '_')) {
        return 0;
    }

    while (isalnum((unsigned char)line[i]) || line[i] == '_') {
        if (out + 1 < size) {
            name[out++] = line[i];
        }
        i++;
    }
    if (line[i] != '(' || out == 0) {
        return 0;
    }

    name[out] = '\0';
    return 1;
}

static void add_stat(struct stats *stats, const char *name, double seconds) {
    size_t i;

    for (i = 0; i < stats->count; i++) {
        if (strcmp(stats->calls[i].name, name) == 0) {
            stats->calls[i].seconds += seconds;
            stats->total_seconds += seconds;
            stats->generation++;
            return;
        }
    }

    if (stats->count < MAX_SYSCALLS) {
        snprintf(stats->calls[stats->count].name,
                 sizeof(stats->calls[stats->count].name), "%s", name);
        stats->calls[stats->count].seconds = seconds;
        stats->count++;
    }

    stats->total_seconds += seconds;
    stats->generation++;
}

static size_t pick_top(const struct stats *stats, size_t *indices, size_t max) {
    size_t used[MAX_SYSCALLS] = {0};
    size_t out = 0;

    while (out < max && out < stats->count) {
        size_t best = MAX_SYSCALLS;
        size_t i;

        for (i = 0; i < stats->count; i++) {
            if (used[i] || stats->calls[i].seconds <= 0.0) {
                continue;
            }
            if (best == MAX_SYSCALLS ||
                stats->calls[i].seconds > stats->calls[best].seconds ||
                (stats->calls[i].seconds == stats->calls[best].seconds &&
                 strcmp(stats->calls[i].name, stats->calls[best].name) < 0)) {
                best = i;
            }
        }

        if (best == MAX_SYSCALLS) {
            break;
        }

        used[best] = 1;
        indices[out++] = best;
    }

    return out;
}

static void print_report(const struct stats *stats) {
    static const char clear[CLEAR_BYTES] = {0};
    size_t indices[5];
    size_t n;
    size_t i;

    if (stats->total_seconds <= 0.0) {
        return;
    }

    n = pick_top(stats, indices, 5);
    for (i = 0; i < n; i++) {
        const struct syscall_stat *call = &stats->calls[indices[i]];
        int percent = (int)(call->seconds * 100.0 / stats->total_seconds + 1e-9);

        printf("%s (%d%%)\n", call->name, percent);
    }
    fwrite(clear, 1, sizeof(clear), stdout);
    fflush(stdout);
}

static int make_strace_argv(char **argv, int argc, const char *strace_path,
                            const char *target_path, const char *fd_path,
                            char ***out_argv) {
    char **trace_argv = calloc((size_t)argc + 6, sizeof(*trace_argv));
    int j = 0;
    int i;

    if (trace_argv == NULL) {
        return -1;
    }

    trace_argv[j++] = (char *)strace_path;
    trace_argv[j++] = "-T";
    trace_argv[j++] = "-qq";
    trace_argv[j++] = "-o";
    trace_argv[j++] = (char *)fd_path;
    trace_argv[j++] = "--";
    trace_argv[j++] = (char *)target_path;

    for (i = 2; i < argc; i++) {
        trace_argv[j++] = argv[i];
    }
    trace_argv[j] = NULL;

    *out_argv = trace_argv;
    return 0;
}

static void child_exec(char **argv, int argc, int read_fd, int write_fd) {
    char fd_path[PATH_MAX];
    char *strace_path = resolve_executable("strace");
    char *target_path = resolve_executable(argv[1]);
    char **trace_argv = NULL;
    int devnull;

    close(read_fd);

    devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO) {
            close(devnull);
        }
    }

    if (strace_path == NULL || target_path == NULL) {
        _exit(127);
    }

    snprintf(fd_path, sizeof(fd_path), "/dev/fd/%d", write_fd);
    if (make_strace_argv(argv, argc, strace_path, target_path, fd_path,
                         &trace_argv) != 0) {
        _exit(127);
    }

    execve(strace_path, trace_argv, environ);
    _exit(127);
}

static int run_profiler(char **argv, int argc) {
    int pipe_fd[2];
    pid_t child;
    FILE *trace_stream;
    char *line = NULL;
    size_t cap = 0;
    struct stats stats = {0};
    long long last_report = 0;
    unsigned long printed_generation = 0;
    int status = 0;

    if (pipe(pipe_fd) != 0) {
        perror("pipe");
        return 1;
    }

    child = fork();
    if (child < 0) {
        perror("fork");
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return 1;
    }

    if (child == 0) {
        child_exec(argv, argc, pipe_fd[0], pipe_fd[1]);
    }

    close(pipe_fd[1]);
    trace_stream = fdopen(pipe_fd[0], "r");
    if (trace_stream == NULL) {
        perror("fdopen");
        close(pipe_fd[0]);
        waitpid(child, &status, 0);
        return 1;
    }

    while (getline(&line, &cap, trace_stream) != -1) {
        char name[NAME_LEN];
        double seconds;

        if (parse_elapsed_seconds(line, &seconds) &&
            parse_syscall_name(line, name, sizeof(name))) {
            add_stat(&stats, name, seconds);
            if (last_report == 0) {
                last_report = now_ms();
            }
        }

        if (last_report != 0 && now_ms() - last_report >= REFRESH_MS &&
            stats.generation != printed_generation) {
            print_report(&stats);
            printed_generation = stats.generation;
            last_report = now_ms();
        }
    }

    free(line);
    fclose(trace_stream);

    if (waitpid(child, &status, 0) < 0) {
        perror("waitpid");
        return 1;
    }

    if (stats.generation != printed_generation) {
        print_report(&stats);
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s COMMAND [ARG]...\n", argv[0]);
        return 1;
    }

    return run_profiler(argv, argc);
}
