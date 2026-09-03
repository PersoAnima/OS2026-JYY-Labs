#include "kvdb.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    _Exit(1);
}

static void expect_value(struct kvdb_t *db, const char *key, const char *value) {
    char buf[128];
    int n = kvdb_get(db, key, buf, sizeof(buf));
    if (n < 0 || strcmp(buf, value) != 0) {
        fprintf(stderr, "key=%s expected=%s actual=%s n=%d\n", key, value,
                n < 0 ? "<missing>" : buf, n);
        fail("unexpected value");
    }
}

static void basic_test(const char *path) {
    struct kvdb_t db;
    if (kvdb_open(&db, path) != 0) {
        fail("open failed");
    }
    if (kvdb_put(&db, "name", "os") != 0 ||
        kvdb_put(&db, "name", "database") != 0 ||
        kvdb_put(&db, "lang", "c") != 0) {
        fail("put failed");
    }
    if (kvdb_get(&db, "name", NULL, 0) != 0) {
        fail("existing key probe failed");
    }
    expect_value(&db, "name", "database");
    expect_value(&db, "lang", "c");
    char buf[8];
    if (kvdb_get(&db, "missing", buf, sizeof(buf)) != -1) {
        fail("missing key should return -1");
    }
    if (kvdb_close(&db) != 0) {
        fail("close failed");
    }

    if (kvdb_open(&db, path) != 0) {
        fail("reopen failed");
    }
    expect_value(&db, "name", "database");
    kvdb_close(&db);
}

static void append_garbage(const char *path) {
    int fd = open(path, O_WRONLY | O_APPEND);
    if (fd < 0) {
        fail("open append failed");
    }
    const char garbage[] = "partial-record-after-crash";
    write(fd, garbage, sizeof(garbage));
    close(fd);
}

static void process_test(const char *path) {
    for (int i = 0; i < 4; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            fail("fork failed");
        }
        if (pid == 0) {
            struct kvdb_t db;
            char key[32];
            char value[32];
            if (kvdb_open(&db, path) != 0) {
                _Exit(2);
            }
            for (int j = 0; j < 25; j++) {
                snprintf(key, sizeof(key), "p%d-%d", i, j);
                snprintf(value, sizeof(value), "v%d-%d", i, j);
                if (kvdb_put(&db, key, value) != 0) {
                    _Exit(3);
                }
            }
            kvdb_close(&db);
            _Exit(0);
        }
    }

    for (int i = 0; i < 4; i++) {
        int status = 0;
        wait(&status);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            fail("child writer failed");
        }
    }

    struct kvdb_t db;
    if (kvdb_open(&db, path) != 0) {
        fail("open after process writes failed");
    }
    expect_value(&db, "p2-17", "v2-17");
    expect_value(&db, "p3-24", "v3-24");
    kvdb_close(&db);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        return 1;
    }

    basic_test(argv[1]);
    append_garbage(argv[1]);

    struct kvdb_t db;
    if (kvdb_open(&db, argv[1]) != 0) {
        fail("open after garbage failed");
    }
    expect_value(&db, "name", "database");
    kvdb_close(&db);

    process_test(argv[1]);
    puts("all tests passed");
    return 0;
}
