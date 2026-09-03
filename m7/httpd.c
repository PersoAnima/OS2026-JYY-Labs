#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BUFFER_SIZE 8192
#define WORKERS 4

typedef struct request {
    int client_fd;
    unsigned long seq;
    struct request *next;
} request_t;

static pthread_mutex_t queue_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cv = PTHREAD_COND_INITIALIZER;
static request_t *queue_head;
static request_t *queue_tail;

static pthread_mutex_t log_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t log_cv = PTHREAD_COND_INITIALIZER;
static unsigned long next_seq;
static unsigned long next_log_seq;

static void send_all(int fd, const char *buf, size_t len) {
    while (len > 0) {
        ssize_t n = send(fd, buf, len, 0);
        if (n <= 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        buf += n;
        len -= (size_t)n;
    }
}

static const char *reason_phrase(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 500:
        return "Internal Server Error";
    default:
        return "OK";
    }
}

static void send_simple_response(int fd, int status, const char *body) {
    char header[256];
    int body_len = (int)strlen(body);
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     status, reason_phrase(status), body_len);
    send_all(fd, header, (size_t)n);
    send_all(fd, body, (size_t)body_len);
}

static void log_request(const char *method, const char *path, int status_code) {
    time_t now = time(NULL);
    struct tm tm;
    char timestamp[64];
    localtime_r(&now, &tm);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm);
    printf("[%s] [%s] [%s] [%d]\n", timestamp, method, path, status_code);
    fflush(stdout);
}

static void ordered_log(unsigned long seq, const char *method, const char *path,
                        int status) {
    pthread_mutex_lock(&log_mu);
    while (seq != next_log_seq) {
        pthread_cond_wait(&log_cv, &log_mu);
    }
    log_request(method, path, status);
    next_log_seq++;
    pthread_cond_broadcast(&log_cv);
    pthread_mutex_unlock(&log_mu);
}

static bool parse_request_line(const char *buf, char *method, size_t method_cap,
                               char *target, size_t target_cap) {
    char version[32];
    if (sscanf(buf, "%15s %1023s %31s", method, target, version) != 3) {
        return false;
    }
    method[method_cap - 1] = '\0';
    target[target_cap - 1] = '\0';
    return strncmp(version, "HTTP/", 5) == 0;
}

static bool valid_cgi_path(const char *path) {
    if (strncmp(path, "/cgi-bin/", 9) != 0) {
        return false;
    }
    if (strstr(path, "..") != NULL) {
        return false;
    }
    for (const char *p = path; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (!(isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.')) {
            return false;
        }
    }
    return true;
}

static int parse_cgi_status(const char *out) {
    if (strncmp(out, "Status:", 7) == 0) {
        while (*out != '\0' && !isdigit((unsigned char)*out)) {
            out++;
        }
        int status = atoi(out);
        if (status >= 100 && status <= 599) {
            return status;
        }
    }
    if (strncmp(out, "HTTP/1.", 7) == 0) {
        const char *p = strchr(out, ' ');
        if (p != NULL) {
            int status = atoi(p + 1);
            if (status >= 100 && status <= 599) {
                return status;
            }
        }
    }
    return 200;
}

static char *read_pipe_all(int fd, size_t *out_len) {
    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);
    if (buf == NULL) {
        return NULL;
    }

    for (;;) {
        if (len + 1024 + 1 > cap) {
            cap *= 2;
            char *next = realloc(buf, cap);
            if (next == NULL) {
                free(buf);
                return NULL;
            }
            buf = next;
        }
        ssize_t n = read(fd, buf + len, cap - len - 1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(buf);
            return NULL;
        }
        if (n == 0) {
            break;
        }
        len += (size_t)n;
    }
    buf[len] = '\0';
    *out_len = len;
    return buf;
}

static int run_cgi(const char *method, const char *path, const char *query,
                   char **out, size_t *out_len) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return 500;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return 500;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        setenv("REQUEST_METHOD", method, 1);
        setenv("QUERY_STRING", query, 1);
        execl(path, path, (char *)NULL);
        _exit(127);
    }

    close(pipefd[1]);
    *out = read_pipe_all(pipefd[0], out_len);
    close(pipefd[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            free(*out);
            *out = NULL;
            return 500;
        }
    }

    if (*out == NULL || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        free(*out);
        *out = NULL;
        return 500;
    }

    return parse_cgi_status(*out);
}

static int handle_request(int client_fd, char *method, size_t method_cap,
                          char *target, size_t target_cap) {
    char buffer[BUFFER_SIZE];
    ssize_t n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        snprintf(method, method_cap, "-");
        snprintf(target, target_cap, "-");
        return 500;
    }
    buffer[n] = '\0';

    if (!parse_request_line(buffer, method, method_cap, target, target_cap)) {
        send_simple_response(client_fd, 500, "bad request\n");
        return 500;
    }

    char path[1024];
    char query[1024];
    char *qmark = strchr(target, '?');
    if (qmark != NULL) {
        *qmark = '\0';
        snprintf(query, sizeof(query), "%s", qmark + 1);
    } else {
        query[0] = '\0';
    }
    snprintf(path, sizeof(path), "%s", target);

    if (strcmp(method, "GET") != 0 || !valid_cgi_path(path)) {
        send_simple_response(client_fd, 404, "not found\n");
        snprintf(target, target_cap, "%s", path);
        return 404;
    }

    char fs_path[1024];
    snprintf(fs_path, sizeof(fs_path), ".%s", path);
    if (access(fs_path, X_OK) != 0) {
        send_simple_response(client_fd, 404, "not found\n");
        snprintf(target, target_cap, "%s", path);
        return 404;
    }

    char *cgi_out = NULL;
    size_t cgi_len = 0;
    int status = run_cgi(method, fs_path, query, &cgi_out, &cgi_len);
    if (status == 500) {
        send_simple_response(client_fd, 500, "cgi failed\n");
    } else if (strncmp(cgi_out, "HTTP/1.", 7) == 0) {
        send_all(client_fd, cgi_out, cgi_len);
    } else {
        char status_line[128];
        int line_len = snprintf(status_line, sizeof(status_line),
                                "HTTP/1.1 %d %s\r\n", status,
                                reason_phrase(status));
        send_all(client_fd, status_line, (size_t)line_len);
        send_all(client_fd, cgi_out, cgi_len);
    }
    free(cgi_out);
    snprintf(target, target_cap, "%s", path);
    return status;
}

static void enqueue(int client_fd) {
    request_t *req = malloc(sizeof(*req));
    if (req == NULL) {
        close(client_fd);
        return;
    }
    req->client_fd = client_fd;
    req->next = NULL;

    pthread_mutex_lock(&queue_mu);
    req->seq = next_seq++;
    if (queue_tail == NULL) {
        queue_head = req;
    } else {
        queue_tail->next = req;
    }
    queue_tail = req;
    pthread_cond_signal(&queue_cv);
    pthread_mutex_unlock(&queue_mu);
}

static request_t *dequeue(void) {
    pthread_mutex_lock(&queue_mu);
    while (queue_head == NULL) {
        pthread_cond_wait(&queue_cv, &queue_mu);
    }
    request_t *req = queue_head;
    queue_head = req->next;
    if (queue_head == NULL) {
        queue_tail = NULL;
    }
    pthread_mutex_unlock(&queue_mu);
    return req;
}

static void *worker_main(void *arg) {
    (void)arg;
    for (;;) {
        request_t *req = dequeue();
        char method[16];
        char path[1024];
        int status = handle_request(req->client_fd, method, sizeof(method), path,
                                    sizeof(path));
        close(req->client_fd);
        ordered_log(req->seq, method, path, status);
        free(req);
    }
}

static int parse_port(int argc, char **argv) {
    if (argc < 2) {
        return 8080;
    }
    char *end = NULL;
    long port = strtol(argv[1], &end, 10);
    if (*end != '\0' || port <= 0 || port > 65535) {
        return -1;
    }
    return (int)port;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);

    int port = parse_port(argc, argv);
    if (port < 0) {
        fprintf(stderr, "usage: httpd [port]\n");
        return 1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(server_fd, 128) != 0) {
        perror("bind/listen");
        close(server_fd);
        return 1;
    }

    pthread_t tids[WORKERS];
    for (int i = 0; i < WORKERS; i++) {
        if (pthread_create(&tids[i], NULL, worker_main, NULL) != 0) {
            perror("pthread_create");
            close(server_fd);
            return 1;
        }
        pthread_detach(tids[i]);
    }

    for (;;) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            break;
        }
        enqueue(client_fd);
    }

    close(server_fd);
    return 1;
}
