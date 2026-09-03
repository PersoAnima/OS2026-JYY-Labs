#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR 512
#define CLUSTER_SECTORS 1
#define RESERVED 1
#define FATS 2
#define FAT_SECTORS 1
#define DATA_OFFSET ((RESERVED + FATS * FAT_SECTORS) * SECTOR)
#define CLUSTER_SIZE (SECTOR * CLUSTER_SECTORS)

static void le16(unsigned char *p, uint16_t v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

static void le32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static unsigned char *cluster(unsigned char *img, uint32_t n) {
    return img + DATA_OFFSET + (n - 2) * CLUSTER_SIZE;
}

static void put_lfn_char(unsigned char *entry, int pos, char ch) {
    le16(entry + pos, (unsigned char)ch);
}

static void write_lfn(unsigned char *entry, const char *name) {
    memset(entry, 0xff, 32);
    entry[0] = 0x41;
    entry[11] = 0x0f;
    entry[13] = 0;
    static const int pos[] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    size_t len = strlen(name);
    for (size_t i = 0; i < 13; i++) {
        if (i < len) {
            put_lfn_char(entry, pos[i], name[i]);
        } else if (i == len) {
            le16(entry + pos[i], 0);
        }
    }
}

static void write_bmp(unsigned char *p, uint32_t size) {
    memset(p, 0, size);
    p[0] = 'B';
    p[1] = 'M';
    le32(p + 2, size);
    le32(p + 10, 54);
    le32(p + 14, 40);
    le32(p + 18, 4);
    le32(p + 22, 4);
    le16(p + 26, 1);
    le16(p + 28, 24);
    for (uint32_t i = 54; i < size; i++) {
        p[i] = (unsigned char)(i * 7 + 3);
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        return 1;
    }

    const size_t image_size = 64 * 1024;
    unsigned char *img = calloc(1, image_size);
    if (img == NULL) {
        return 1;
    }

    img[0] = 0xeb;
    img[1] = 0x58;
    img[2] = 0x90;
    memcpy(img + 3, "MSDOS5.0", 8);
    le16(img + 11, SECTOR);
    img[13] = CLUSTER_SECTORS;
    le16(img + 14, RESERVED);
    img[16] = FATS;
    le32(img + 32, image_size / SECTOR);
    le32(img + 36, FAT_SECTORS);
    le32(img + 44, 2);
    img[510] = 0x55;
    img[511] = 0xaa;

    const char *name = "Art_0001.bmp";
    uint32_t file_cluster = 4;
    uint32_t file_size = 128;

    unsigned char *dir = cluster(img, 3);
    write_lfn(dir, name);
    memcpy(dir + 32, "ART_00~1BMP", 11);
    dir[32 + 11] = 0x20;
    le16(dir + 32 + 20, 0);
    le16(dir + 32 + 26, file_cluster);
    le32(dir + 32 + 28, file_size);

    write_bmp(cluster(img, file_cluster), file_size);

    FILE *fp = fopen(argv[1], "wb");
    if (fp == NULL) {
        free(img);
        return 1;
    }
    fwrite(img, 1, image_size, fp);
    fclose(fp);
    free(img);
    return 0;
}
