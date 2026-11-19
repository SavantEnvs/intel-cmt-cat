#ifndef ACPI_MOCK_H
#define ACPI_MOCK_H

#include <stddef.h>
#include <stdint.h>

/*
 * Makes /sys/firmware/acpi/tables/<sig> a file holding the fuzz input, the way the kernel exposes a
 * firmware table, so the library's own acpi_get_sig() reads it. The wrap of access(), open() and
 * stat() is the only substitution: the library code is the real one.
 *
 * While a table is set, /sys/firmware/acpi/tables is also a directory, as it is on every kernel
 * that exposes tables at all, so a table the input does not hold is "absent" (acpi_get_sig() then
 * returns NULL) rather than looked for by scanning physical memory through /dev/mem.
 *
 * Returns 0 when the input can be a table file and -1 when it cannot. The kernel serves a table as
 * exactly header.length bytes, so an input shorter than the 36-byte ACPI header, or whose length
 * field (offset 4) is not its own size, is not one (ACPI spec 5.2.6: Length is the length of the
 * whole table, header included). The library sizes its buffer from that field before reading.
 */
int acpi_mock_set(const char *sig, const uint8_t *data, size_t size);

/*
 * The same for a whole tables directory: the input is one or more ACPI tables back to back, each
 * header.length bytes long and served under its own signature (bytes 0-3), so
 * /sys/firmware/acpi/tables/SRAT is whichever table in the input says "SRAT". Returns -1 when the
 * input does not split exactly into tables, or names one signature twice (a directory holds one
 * file per name).
 *
 * Built with -DACPI_MOCK_MULTI, the custom mutator below keeps every table of the input a table.
 */
int acpi_mock_set_tables(const uint8_t *data, size_t size);

#endif
