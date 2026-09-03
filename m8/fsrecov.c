#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fat32.h"

#define ENTRY_SIZE 32
#define MAX_NAME 256
#define MAX_FOUND 4096
#define MAX_LFN_PARTS 20

typedef struct {
    char name[MAX_NAME];
    u32 cluster;
    u32 size;
} candidate_t;

typedef struct {
    struct fat32hdr *hdr;
    size_t image_size;
    size_t cluster_size;
    size_t data_offset;
} fat_view_t;

static u16 read_u16(const unsigned char *p) {
    return (u16)p[0] | ((u16)p[1] << 8);
}

static u32 read_u32(const unsigned char *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static void *map_disk(const char *fname, size_t *size);
static bool init_fat_view(struct fat32hdr *hdr, size_t image_size, fat_view_t *fat);

static size_t fat_image_bytes(const struct fat32hdr *hdr) {
    u32 sectors = hdr->BPB_TotSec32;
    if (sectors == 0) {
        sectors = hdr->BPB_TotSec16;
    }
    return (size_t)sectors * hdr->BPB_BytsPerSec;
}

static bool init_fat_view(struct fat32hdr *hdr, size_t image_size, fat_view_t *fat) {
    if (image_size < sizeof(*hdr) || hdr->Signature_word != 0xaa55 ||
        hdr->BPB_BytsPerSec == 0 || hdr->BPB_SecPerClus == 0 ||
        hdr->BPB_NumFATs == 0 || hdr->BPB_FATSz32 == 0) {
        return false;
    }

    size_t declared_size = fat_image_bytes(hdr);
    if (declared_size == 0 || declared_size > image_size) {
        return false;
    }

    fat->hdr = hdr;
    fat->image_size = declared_size;
    fat->cluster_size = (size_t)hdr->BPB_BytsPerSec * hdr->BPB_SecPerClus;
    fat->data_offset = ((size_t)hdr->BPB_RsvdSecCnt +
                        (size_t)hdr->BPB_NumFATs * hdr->BPB_FATSz32) *
                       hdr->BPB_BytsPerSec;
    return fat->cluster_size > 0 && fat->data_offset < fat->image_size;
}

static size_t cluster_offset(const fat_view_t *fat, u32 cluster) {
    if (cluster < 2) {
        return fat->image_size;
    }
    return fat->data_offset + (size_t)(cluster - 2) * fat->cluster_size;
}

static bool safe_name_char(char c) {
    return isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.';
}

static bool valid_filename(const char *name) {
    size_t len = strlen(name);
    if (len == 0 || len >= MAX_NAME) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (!safe_name_char(name[i])) {
            return false;
        }
    }

    const char *dot = strrchr(name, '.');
    return dot != NULL &&
           (strcmp(dot, ".bmp") == 0 || strcmp(dot, ".BMP") == 0);
}

static void append_lfn_part(const unsigned char *entry, char *part, size_t cap) {
    static const int pos[] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    size_t n = 0;

    for (size_t i = 0; i < sizeof(pos) / sizeof(pos[0]) && n + 1 < cap; i++) {
        u16 ch = read_u16(entry + pos[i]);
        if (ch == 0x0000 || ch == 0xffff) {
            break;
        }
        if (ch < 128 && isprint((unsigned char)ch)) {
            part[n++] = (char)ch;
        }
    }
    part[n] = '\0';
}

static void short_name(const struct fat32dent *dent, char *out, size_t cap) {
    size_t n = 0;

    for (int i = 0; i < 8 && dent->DIR_Name[i] != ' ' && n + 1 < cap; i++) {
        unsigned char ch = dent->DIR_Name[i];
        if (ch == 0xe5) {
            ch = '?';
        }
        out[n++] = (char)tolower(ch);
    }
    if (dent->DIR_Name[8] != ' ' && n + 1 < cap) {
        out[n++] = '.';
    }
    for (int i = 8; i < 11 && dent->DIR_Name[i] != ' ' && n + 1 < cap; i++) {
        out[n++] = (char)tolower(dent->DIR_Name[i]);
    }
    out[n] = '\0';
}

static bool bmp_header_ok(const unsigned char *p, size_t available, u32 size) {
    if (available < 54 || size < 54 || available < size) {
        return false;
    }
    if (p[0] != 'B' || p[1] != 'M') {
        return false;
    }

    u32 file_size = read_u32(p + 2);
    u32 data_offset = read_u32(p + 10);
    u32 dib_size = read_u32(p + 14);
    u16 planes = read_u16(p + 26);
    u16 bpp = read_u16(p + 28);

    return file_size == size && data_offset >= 54 && data_offset < size &&
           dib_size >= 40 && planes == 1 && bpp == 24;
}

static bool seen_candidate(const candidate_t *items, size_t n, u32 cluster,
                           const char *name) {
    for (size_t i = 0; i < n; i++) {
        if (items[i].cluster == cluster || strcmp(items[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

static void collect_candidates(const unsigned char *img, const fat_view_t *fat,
                               candidate_t *items, size_t *count) {
    char lfn_parts[MAX_LFN_PARTS][14];
    int lfn_count = 0;

    for (size_t off = fat->data_offset; off + ENTRY_SIZE <= fat->image_size;
         off += ENTRY_SIZE) {
        const unsigned char *entry = img + off;
        const struct fat32dent *dent = (const struct fat32dent *)entry;

        if (entry[0] == 0x00) {
            lfn_count = 0;
            continue;
        }

        if (dent->DIR_Attr == ATTR_LFN) {
            if (lfn_count < MAX_LFN_PARTS) {
                append_lfn_part(entry, lfn_parts[lfn_count],
                                sizeof(lfn_parts[lfn_count]));
                lfn_count++;
            }
            continue;
        }

        if ((dent->DIR_Attr & ATTR_DIRECTORY) != 0 ||
            (dent->DIR_Attr & ATTR_ARCHIVE) == 0) {
            lfn_count = 0;
            continue;
        }

        char name[MAX_NAME] = "";
        if (lfn_count > 0) {
            for (int i = lfn_count - 1; i >= 0; i--) {
                strncat(name, lfn_parts[i], sizeof(name) - strlen(name) - 1);
            }
        } else {
            short_name(dent, name, sizeof(name));
        }

        u32 cluster = ((u32)dent->DIR_FstClusHI << 16) | dent->DIR_FstClusLO;
        u32 size = dent->DIR_FileSize;
        size_t data = cluster_offset(fat, cluster);

        if (*count < MAX_FOUND && valid_filename(name) && size > 0 &&
            data < fat->image_size && size <= fat->image_size - data &&
            bmp_header_ok(img + data, fat->image_size - data, size) &&
            !seen_candidate(items, *count, cluster, name)) {
            snprintf(items[*count].name, sizeof(items[*count].name), "%s", name);
            items[*count].cluster = cluster;
            items[*count].size = size;
            (*count)++;
        }
        lfn_count = 0;
    }
}

static bool write_all(int fd, const unsigned char *buf, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        buf += n;
        len -= (size_t)n;
    }
    return true;
}

static bool read_digest(int fd, char digest[41]) {
    char buf[128];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        return false;
    }
    buf[n] = '\0';

    for (int i = 0; i < 40; i++) {
        if (!isxdigit((unsigned char)buf[i])) {
            return false;
        }
        digest[i] = buf[i];
    }
    digest[40] = '\0';
    return true;
}

static bool run_checksum_tool(const char *tool, const char *arg1, const char *arg2,
                              const char *path, char digest[41]) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        if (arg1 == NULL) {
            execlp(tool, tool, path, (char *)NULL);
        } else if (arg2 == NULL) {
            execlp(tool, tool, arg1, path, (char *)NULL);
        } else {
            execlp(tool, tool, arg1, arg2, path, (char *)NULL);
        }
        _exit(127);
    }

    close(pipefd[1]);
    bool ok = read_digest(pipefd[0], digest);
    close(pipefd[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return false;
        }
    }
    return ok && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool sha1sum_file(const unsigned char *data, size_t len, char digest[41]) {
    char tmpl[] = "/tmp/fsrecov-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        return false;
    }

    bool ok = write_all(fd, data, len);
    if (close(fd) != 0) {
        ok = false;
    }
    if (ok) {
        ok = run_checksum_tool("sha1sum", NULL, NULL, tmpl, digest);
    }
    if (!ok) {
        ok = run_checksum_tool("shasum", "-a", "1", tmpl, digest);
    }
    unlink(tmpl);
    return ok;
}

static void *map_disk(const char *fname, size_t *size) {
    int fd = open(fname, O_RDONLY);
    if (fd < 0) {
        perror(fname);
        exit(1);
    }

    off_t n = lseek(fd, 0, SEEK_END);
    if (n < 0) {
        perror(fname);
        close(fd);
        exit(1);
    }

    void *mem = mmap(NULL, (size_t)n, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mem == MAP_FAILED) {
        perror(fname);
        close(fd);
        exit(1);
    }

    close(fd);
    *size = (size_t)n;
    return mem;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: fsrecov FILE\n");
        return 1;
    }

    setbuf(stdout, NULL);
    assert(sizeof(struct fat32hdr) == 512);
    assert(sizeof(struct fat32dent) == 32);

    size_t mapped_size = 0;
    struct fat32hdr *hdr = map_disk(argv[1], &mapped_size);

    fat_view_t fat;
    if (!init_fat_view(hdr, mapped_size, &fat)) {
        fprintf(stderr, "%s: Not a FAT32 file image\n", argv[1]);
        munmap(hdr, mapped_size);
        return 1;
    }

    candidate_t items[MAX_FOUND];
    size_t count = 0;
    collect_candidates((const unsigned char *)hdr, &fat, items, &count);

    for (size_t i = 0; i < count; i++) {
        size_t off = cluster_offset(&fat, items[i].cluster);
        char digest[41];
        if (sha1sum_file((const unsigned char *)hdr + off, items[i].size, digest)) {
            printf("%s  %s\n", digest, items[i].name);
        }
    }

    munmap(hdr, mapped_size);
    return 0;
}
