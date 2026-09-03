#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "kvdb.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define KVDB_MAGIC 0x4b564442U
#define KVDB_COMMITTED 0xc01117edU
#define KVDB_MAX_KEY (64U * 1024U)
#define KVDB_MAX_VALUE (16U * 1024U * 1024U)

typedef struct {
    uint32_t magic;
    uint32_t committed;
    uint32_t key_len;
    uint32_t value_len;
    uint32_t checksum;
    uint32_t reserved;
} record_t;

static pthread_mutex_t global_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint32_t fnv1a(const void *data, size_t len, uint32_t h) {
    const unsigned char *p = data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 16777619U;
    }
    return h;
}

static uint32_t record_checksum(uint32_t key_len, uint32_t value_len,
                                const char *key, const char *value) {
    uint32_t h = 2166136261U;
    h = fnv1a(&key_len, sizeof(key_len), h);
    h = fnv1a(&value_len, sizeof(value_len), h);
    h = fnv1a(key, key_len, h);
    h = fnv1a(value, value_len, h);
    return h;
}

static int write_all(int fd, const void *data, size_t len) {
    const char *p = data;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int read_all_or_eof(int fd, void *data, size_t len) {
    char *p = data;
    size_t done = 0;
    while (done < len) {
        ssize_t n = read(fd, p + done, len - done);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return done == 0 ? 0 : -1;
        }
        done += (size_t)n;
    }
    return 1;
}

static off_t valid_prefix_end(int fd) {
    if (lseek(fd, 0, SEEK_SET) < 0) {
        return -1;
    }

    off_t valid_end = 0;
    for (;;) {
        off_t rec_off = lseek(fd, 0, SEEK_CUR);
        if (rec_off < 0) {
            return -1;
        }

        record_t rec;
        int r = read_all_or_eof(fd, &rec, sizeof(rec));
        if (r == 0) {
            return valid_end;
        }
        if (r < 0 || rec.magic != KVDB_MAGIC ||
            rec.committed != KVDB_COMMITTED || rec.key_len == 0 ||
            rec.key_len > KVDB_MAX_KEY || rec.value_len > KVDB_MAX_VALUE) {
            return valid_end;
        }

        char *k = malloc(rec.key_len);
        char *v = malloc(rec.value_len == 0 ? 1 : rec.value_len);
        if (k == NULL || v == NULL) {
            free(k);
            free(v);
            return -1;
        }

        if (read_all_or_eof(fd, k, rec.key_len) != 1 ||
            read_all_or_eof(fd, v, rec.value_len) != 1) {
            free(k);
            free(v);
            return valid_end;
        }

        uint32_t checksum = record_checksum(rec.key_len, rec.value_len, k, v);
        free(k);
        free(v);
        if (checksum != rec.checksum) {
            return valid_end;
        }

        valid_end = rec_off + (off_t)sizeof(rec) + rec.key_len + rec.value_len;
    }
}

static int lock_db(struct kvdb_t *db) {
    pthread_mutex_lock(&global_mutex);
    pthread_mutex_lock(&db->mutex);
    while (flock(db->fd, LOCK_EX) != 0) {
        if (errno != EINTR) {
            pthread_mutex_unlock(&db->mutex);
            pthread_mutex_unlock(&global_mutex);
            return -1;
        }
    }
    return 0;
}

static void unlock_db(struct kvdb_t *db) {
    flock(db->fd, LOCK_UN);
    pthread_mutex_unlock(&db->mutex);
    pthread_mutex_unlock(&global_mutex);
}

int kvdb_open(struct kvdb_t *db, const char *path) {
    if (db == NULL || path == NULL) {
        return -1;
    }

    int fd = open(path, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        return -1;
    }

    char *path_copy = strdup(path);
    if (path_copy == NULL) {
        close(fd);
        return -1;
    }

    db->path = path_copy;
    db->fd = fd;
    if (pthread_mutex_init(&db->mutex, NULL) != 0) {
        close(fd);
        free(db->path);
        db->path = NULL;
        db->fd = -1;
        return -1;
    }
    return 0;
}

int kvdb_put(struct kvdb_t *db, const char *key, const char *value) {
    if (db == NULL || db->fd < 0 || key == NULL || value == NULL) {
        return -1;
    }

    size_t key_len_sz = strlen(key);
    size_t value_len_sz = strlen(value);
    if (key_len_sz == 0 || key_len_sz > KVDB_MAX_KEY ||
        value_len_sz > KVDB_MAX_VALUE) {
        return -1;
    }

    uint32_t key_len = (uint32_t)key_len_sz;
    uint32_t value_len = (uint32_t)value_len_sz;
    record_t rec = {
        .magic = KVDB_MAGIC,
        .committed = 0,
        .key_len = key_len,
        .value_len = value_len,
        .checksum = record_checksum(key_len, value_len, key, value),
        .reserved = 0,
    };

    if (lock_db(db) != 0) {
        return -1;
    }

    off_t off = valid_prefix_end(db->fd);
    int ok = 0;
    if (off < 0 || ftruncate(db->fd, off) != 0 ||
        lseek(db->fd, off, SEEK_SET) < 0 ||
        write_all(db->fd, &rec, sizeof(rec)) != 0 ||
        write_all(db->fd, key, key_len) != 0 ||
        write_all(db->fd, value, value_len) != 0 || fsync(db->fd) != 0) {
        ok = -1;
    } else {
        uint32_t committed = KVDB_COMMITTED;
        if (pwrite(db->fd, &committed, sizeof(committed),
                   off + (off_t)offsetof(record_t, committed)) !=
                (ssize_t)sizeof(committed) ||
            fsync(db->fd) != 0) {
            ok = -1;
        }
    }

    unlock_db(db);
    return ok;
}

int kvdb_get(struct kvdb_t *db, const char *key, char *buf, size_t length) {
    if (db == NULL || db->fd < 0 || key == NULL ||
        (buf == NULL && length != 0)) {
        return -1;
    }

    size_t want_len = strlen(key);
    if (want_len == 0 || want_len > KVDB_MAX_KEY) {
        return -1;
    }

    if (lock_db(db) != 0) {
        return -1;
    }

    if (lseek(db->fd, 0, SEEK_SET) < 0) {
        unlock_db(db);
        return -1;
    }

    char *best = NULL;
    size_t best_len = 0;
    int rc = -1;

    for (;;) {
        record_t rec;
        int r = read_all_or_eof(db->fd, &rec, sizeof(rec));
        if (r == 0) {
            break;
        }
        if (r < 0 || rec.magic != KVDB_MAGIC || rec.key_len == 0 ||
            rec.key_len > KVDB_MAX_KEY || rec.value_len > KVDB_MAX_VALUE) {
            break;
        }

        char *k = malloc(rec.key_len + 1);
        char *v = malloc(rec.value_len + 1);
        if (k == NULL || v == NULL) {
            free(k);
            free(v);
            rc = -1;
            goto out;
        }

        if (read_all_or_eof(db->fd, k, rec.key_len) != 1 ||
            read_all_or_eof(db->fd, v, rec.value_len) != 1) {
            free(k);
            free(v);
            break;
        }
        k[rec.key_len] = '\0';
        v[rec.value_len] = '\0';

        uint32_t checksum = record_checksum(rec.key_len, rec.value_len, k, v);
        if (rec.committed == KVDB_COMMITTED && rec.checksum == checksum &&
            rec.key_len == want_len && memcmp(k, key, want_len) == 0) {
            char *copy = malloc(rec.value_len + 1);
            if (copy == NULL) {
                free(k);
                free(v);
                rc = -1;
                goto out;
            }
            memcpy(copy, v, rec.value_len + 1);
            free(best);
            best = copy;
            best_len = rec.value_len;
        }

        free(k);
        free(v);
    }

    if (best != NULL) {
        if (buf != NULL && length > 0) {
            size_t n = best_len;
            if (n >= length) {
                n = length - 1;
            }
            memcpy(buf, best, n);
            buf[n] = '\0';
            rc = (int)n;
        } else {
            rc = 0;
        }
    }

out:
    free(best);
    unlock_db(db);
    return rc;
}

int kvdb_close(struct kvdb_t *db) {
    if (db == NULL || db->fd < 0) {
        return -1;
    }

    int fd = db->fd;
    db->fd = -1;
    pthread_mutex_destroy(&db->mutex);
    free(db->path);
    db->path = NULL;
    return close(fd);
}
