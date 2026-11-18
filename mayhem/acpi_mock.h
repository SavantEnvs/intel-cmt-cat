#ifndef ACPI_MOCK_H
#define ACPI_MOCK_H

#include <stddef.h>
#include <stdint.h>

/*
 * Makes /sys/firmware/acpi/tables/<sig> a file holding the fuzz input, the way the kernel exposes a
 * firmware table, so the library's own acpi_get_sig() reads it. The wrap of access() and open() is
 * the only substitution: the library code is the real one.
 *
 * Returns 0 when the input can be a table file and -1 when it cannot. The kernel serves a table as
 * exactly header.length bytes, so an input shorter than the 36-byte ACPI header, or whose length
 * field (offset 4) is not its own size, is not one (ACPI spec 5.2.6: Length is the length of the
 * whole table, header included). The library sizes its buffer from that field before reading.
 */
int acpi_mock_set(const char *sig, const uint8_t *data, size_t size);

#endif
