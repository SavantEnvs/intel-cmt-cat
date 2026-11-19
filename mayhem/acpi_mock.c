#define _GNU_SOURCE
#include "acpi_mock.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ACPI_HEADER_SIZE 36
#define ACPI_TABLES_DIR  "/sys/firmware/acpi/tables"
#define MAX_TABLES       8

int __real_access(const char *path, int mode);
int __real_open(const char *path, int flags, ...);
int __real___open_2(const char *path, int flags);
int __real_stat(const char *path, struct stat *st);

struct table {
        uint8_t sig[4];
        char path[64];
        const uint8_t *data;
        size_t size;
};

static struct table g_tables[MAX_TABLES];
static size_t g_num;

static uint32_t
header_length(const uint8_t *data)
{
        uint32_t length;

        memcpy(&length, data + 4, sizeof(length));
        return length;
}

static void
add_table(const uint8_t *sig, const uint8_t *data, size_t size)
{
        struct table *t = &g_tables[g_num++];

        memcpy(t->sig, sig, sizeof(t->sig));
        snprintf(t->path, sizeof(t->path), ACPI_TABLES_DIR "/%.4s", (const char *)sig);
        t->data = data;
        t->size = size;
}

int
acpi_mock_set(const char *sig, const uint8_t *data, size_t size)
{
        g_num = 0;
        if (size < ACPI_HEADER_SIZE || header_length(data) != size)
                return -1;

        add_table((const uint8_t *)sig, data, size);
        return 0;
}

int
acpi_mock_set_tables(const uint8_t *data, size_t size)
{
        size_t off = 0;

        g_num = 0;
        while (off < size) {
                size_t i;
                uint32_t length;

                if (size - off < ACPI_HEADER_SIZE || g_num == MAX_TABLES)
                        goto reject;
                length = header_length(data + off);
                if (length < ACPI_HEADER_SIZE || length > size - off)
                        goto reject;
                for (i = 0; i < g_num; i++)
                        if (memcmp(g_tables[i].sig, data + off, 4) == 0)
                                goto reject;
                add_table(data + off, data + off, length);
                off += length;
        }
        return g_num > 0 ? 0 : -1;

reject:
        g_num = 0;
        return -1;
}

static const struct table *
find_table(const char *path)
{
        size_t i;

        if (path == NULL)
                return NULL;
        for (i = 0; i < g_num; i++)
                if (strcmp(path, g_tables[i].path) == 0)
                        return &g_tables[i];
        return NULL;
}

static int
table_fd(const struct table *t)
{
        int fd = memfd_create("acpi-table", MFD_CLOEXEC);

        if (fd < 0)
                return -1;
        if (write(fd, t->data, t->size) != (ssize_t)t->size || lseek(fd, 0, SEEK_SET) != 0) {
                close(fd);
                return -1;
        }
        return fd;
}

int
__wrap_access(const char *path, int mode)
{
        if (find_table(path) != NULL)
                return 0;
        return __real_access(path, mode);
}

int
__wrap_open(const char *path, int flags, ...)
{
        const struct table *t = find_table(path);
        mode_t mode = 0;

        if (flags & O_CREAT) {
                va_list ap;

                va_start(ap, flags);
                mode = va_arg(ap, mode_t);
                va_end(ap);
        }

        if (t != NULL)
                return table_fd(t);
        return __real_open(path, flags, mode);
}

/* glibc's fortified open(), which the library's -D_FORTIFY_SOURCE build calls for an open without O_CREAT */
int
__wrap___open_2(const char *path, int flags)
{
        const struct table *t = find_table(path);

        if (t != NULL)
                return table_fd(t);
        return __real___open_2(path, flags);
}

/* the tables directory, which the library stats (pqos_dir_exists) to tell an absent table from one
 * it has to look for in physical memory */
int
__wrap_stat(const char *path, struct stat *st)
{
        if (g_num > 0 && path != NULL && strcmp(path, ACPI_TABLES_DIR) == 0) {
                memset(st, 0, sizeof(*st));
                st->st_mode = S_IFDIR | 0555;
                st->st_nlink = 2;
                return 0;
        }
        return __real_stat(path, st);
}

/*
 * Keeps a mutated table a table: libFuzzer rarely produces the two compensating byte changes a
 * checksum needs, so without this nearly every input stops at the library's checksum test. The
 * length is always made the size, the checksum (byte 9) is made valid in 7 of 8 runs, so the
 * library's bad-checksum path is still exercised. This runs between inputs, not inside one.
 *
 * With ACPI_MOCK_MULTI the input is a run of tables: each one whose length field does not fit what
 * is left is made the last, and bytes after the last that cannot hold a header are cut off.
 */
extern size_t LLVMFuzzerMutate(uint8_t *data, size_t size, size_t max_size) __attribute__((weak));

static void
fix_table(uint8_t *data, uint32_t length, unsigned int seed)
{
        uint8_t sum = 0;
        size_t i;

        memcpy(data + 4, &length, sizeof(length));
        if (seed % 8 != 0) {
                data[9] = 0;
                for (i = 0; i < length; i++)
                        sum = (uint8_t)(sum + data[i]);
                data[9] = (uint8_t)(0 - sum);
        }
}

size_t
LLVMFuzzerCustomMutator(uint8_t *data, size_t size, size_t max_size, unsigned int seed)
{
        if (LLVMFuzzerMutate != NULL)
                size = LLVMFuzzerMutate(data, size, max_size);
        if (size < ACPI_HEADER_SIZE)
                return size;

#ifdef ACPI_MOCK_MULTI
        {
                size_t off = 0;

                while (size - off >= ACPI_HEADER_SIZE) {
                        uint32_t length = header_length(data + off);

                        if (length < ACPI_HEADER_SIZE || length > size - off)
                                length = (uint32_t)(size - off);
                        fix_table(data + off, length, seed);
                        seed = seed * 1103515245u + 12345u;
                        off += length;
                }
                return off;
        }
#else
        fix_table(data, (uint32_t)size, seed);
        return size;
#endif
}
