#ifndef KVDB_H
#define KVDB_H

#include <pthread.h>
#include <stddef.h>

struct kvdb_t {
    char *path;
    int fd;
    pthread_mutex_t mutex;
};

int kvdb_open(struct kvdb_t *db, const char *path);
int kvdb_put(struct kvdb_t *db, const char *key, const char *value);
int kvdb_get(struct kvdb_t *db, const char *key, char *buf, size_t length);
int kvdb_close(struct kvdb_t *db);

#endif
