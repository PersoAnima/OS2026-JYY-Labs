#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_LINE 4096
#define MAX_HANDLES 128

static void *loaded_handles[MAX_HANDLES];
static size_t loaded_count;
static unsigned long expr_count;

static char *trim_left(char *s) {
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

static void chomp(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

static bool is_function_definition(const char *s) {
    return strncmp(s, "int", 3) == 0 && isspace((unsigned char)s[3]);
}

static bool make_temp_path(char path[64]) {
    char tmpl[] = "/tmp/crepl-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        return false;
    }
    close(fd);
    snprintf(path, 64, "%s", tmpl);
    return true;
}

static bool write_source(const char *path, const char *line, bool is_func,
                         const char *wrapper_name) {
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        return false;
    }

    int rc;
    if (is_func) {
        rc = fprintf(fp, "%s\n", line);
    } else {
        rc = fprintf(fp, "int %s(void) { return (%s); }\n", wrapper_name, line);
    }

    if (rc < 0 || fclose(fp) != 0) {
        return false;
    }
    return true;
}

static const char *runtime_compiler(void) {
    const char *cc = getenv("CREPL_CC");
    if (cc != NULL && cc[0] != '\0') {
        return cc;
    }

    cc = getenv("CC");
    if (cc != NULL && cc[0] != '\0') {
        return cc;
    }

    return "gcc";
}

static void redirect_compiler_output(void) {
    int fd = open("/dev/null", O_WRONLY);
    if (fd < 0) {
        return;
    }
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO) {
        close(fd);
    }
}

static bool compile_shared_object(const char *source, const char *output) {
    pid_t pid = fork();
    if (pid < 0) {
        return false;
    }

    if (pid == 0) {
        const char *cc = runtime_compiler();
        redirect_compiler_output();
#ifdef __APPLE__
        char *const argv[] = {
            (char *)cc,
            "-x",
            "c",
            "-std=gnu17",
            "-dynamiclib",
            "-undefined",
            "dynamic_lookup",
            "-Wno-implicit-function-declaration",
            (char *)source,
            "-o",
            (char *)output,
            NULL,
        };
#else
        char *const argv[] = {
            (char *)cc,
            "-x",
            "c",
            "-std=gnu17",
            "-fPIC",
            "-shared",
            "-Wno-implicit-function-declaration",
            (char *)source,
            "-o",
            (char *)output,
            NULL,
        };
#endif
        execvp(cc, argv);
        _exit(127);
    }

    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return false;
        }
    }

    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static void cleanup_files(const char *source, const char *library) {
    unlink(source);
    unlink(library);
}

static void print_error(void) {
    puts("ERROR.");
    fflush(stdout);
}

static void print_ok(void) {
    puts("OK.");
    fflush(stdout);
}

static bool load_function_line(const char *line) {
    char source[64];
    char library[64];

    if (!make_temp_path(source)) {
        return false;
    }
    if (!make_temp_path(library)) {
        unlink(source);
        return false;
    }

    if (!write_source(source, line, true, NULL)) {
        cleanup_files(source, library);
        return false;
    }

    if (!compile_shared_object(source, library)) {
        cleanup_files(source, library);
        return false;
    }

    void *handle = dlopen(library, RTLD_NOW | RTLD_GLOBAL);
    if (handle == NULL || loaded_count >= MAX_HANDLES) {
        if (handle != NULL) {
            dlclose(handle);
        }
        cleanup_files(source, library);
        return false;
    }

    loaded_handles[loaded_count++] = handle;
    cleanup_files(source, library);
    return true;
}

static bool eval_expression_line(const char *line, int *result) {
    char source[64];
    char library[64];
    char wrapper[64];

    snprintf(wrapper, sizeof(wrapper), "__crepl_expr_%lu", expr_count++);

    if (!make_temp_path(source)) {
        return false;
    }
    if (!make_temp_path(library)) {
        unlink(source);
        return false;
    }

    if (!write_source(source, line, false, wrapper)) {
        cleanup_files(source, library);
        return false;
    }

    if (!compile_shared_object(source, library)) {
        cleanup_files(source, library);
        return false;
    }

    void *handle = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        cleanup_files(source, library);
        return false;
    }

    dlerror();
    union {
        void *object;
        int (*function)(void);
    } symbol;
    symbol.object = dlsym(handle, wrapper);
    const char *err = dlerror();
    if (err != NULL || symbol.function == NULL) {
        dlclose(handle);
        cleanup_files(source, library);
        return false;
    }

    *result = symbol.function();
    dlclose(handle);
    cleanup_files(source, library);
    return true;
}

int main(void) {
    char line[MAX_LINE];

    while (fgets(line, sizeof(line), stdin) != NULL) {
        chomp(line);
        char *input = trim_left(line);
        if (input[0] == '\0') {
            continue;
        }

        if (is_function_definition(input)) {
            if (load_function_line(input)) {
                print_ok();
            } else {
                print_error();
            }
        } else {
            int result = 0;
            if (eval_expression_line(input, &result)) {
                printf("= %d.\n", result);
                fflush(stdout);
            } else {
                print_error();
            }
        }
    }

    for (size_t i = 0; i < loaded_count; i++) {
        dlclose(loaded_handles[i]);
    }

    return 0;
}
