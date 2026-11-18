#define _GNU_SOURCE
#include "acpi_mock.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#define ACPI_HEADER_SIZE 36
#define ACPI_TABLES_DIR  "/sys/firmware/acpi/tables/"

int __real_access(const char *path, int mode);
int __real_open(const char *path, int flags, ...);
int __real___open_2(const char *path, int flags);

static char g_path[64];
static const uint8_t *g_data;
static size_t g_size;

int
acpi_mock_set(const char *sig, const uint8_t *data, size_t size)
{
        uint32_t length;

        if (size < ACPI_HEADER_SIZE)
                return -1;
        memcpy(&length, data + 4, sizeof(length));
        if (length != size)
                return -1;

        snprintf(g_path, sizeof(g_path), ACPI_TABLES_DIR "%s", sig);
        g_data = data;
        g_size = size;
        return 0;
}

static int
is_table(const char *path)
{
        return g_data != NULL && path != NULL && strcmp(path, g_path) == 0;
}

static int
table_fd(void)
{
        int fd = memfd_create("acpi-table", MFD_CLOEXEC);

        if (fd < 0)
                return -1;
        if (write(fd, g_data, g_size) != (ssize_t)g_size ||
            lseek(fd, 0, SEEK_SET) != 0) {
                close(fd);
                return -1;
        }
        return fd;
}

int
__wrap_access(const char *path, int mode)
{
        if (is_table(path))
                return 0;
        return __real_access(path, mode);
}

int
__wrap_open(const char *path, int flags, ...)
{
        mode_t mode = 0;

        if (flags & O_CREAT) {
                va_list ap;

                va_start(ap, flags);
                mode = va_arg(ap, mode_t);
                va_end(ap);
        }

        if (is_table(path))
                return table_fd();
        return __real_open(path, flags, mode);
}

/*
 * Keeps a mutated table a table: libFuzzer rarely produces the two compensating byte changes a
 * checksum needs, so without this nearly every input stops at the library's checksum test. The
 * length is always made the size, the checksum (byte 9) is made valid in 7 of 8 runs, so the
 * library's bad-checksum path is still exercised. This runs between inputs, not inside one.
 */
extern size_t LLVMFuzzerMutate(uint8_t *data, size_t size, size_t max_size)
    __attribute__((weak));

size_t
LLVMFuzzerCustomMutator(uint8_t *data,
                        size_t size,
                        size_t max_size,
                        unsigned int seed)
{
        uint32_t length;
        uint8_t sum = 0;
        size_t i;

        if (LLVMFuzzerMutate != NULL)
                size = LLVMFuzzerMutate(data, size, max_size);
        if (size < ACPI_HEADER_SIZE)
                return size;

        length = (uint32_t)size;
        memcpy(data + 4, &length, sizeof(length));
        if (seed % 8 != 0) {
                data[9] = 0;
                for (i = 0; i < size; i++)
                        sum = (uint8_t)(sum + data[i]);
                data[9] = (uint8_t)(0 - sum);
        }
        return size;
}

/* glibc's fortified open(), which the library's -D_FORTIFY_SOURCE build calls for an open without O_CREAT */
int
__wrap___open_2(const char *path, int flags)
{
        if (is_table(path))
                return table_fd();
        return __real___open_2(path, flags);
}
