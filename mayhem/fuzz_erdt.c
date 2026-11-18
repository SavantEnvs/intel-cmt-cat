/* ERDT ACPI table, read by the library from the table file the kernel would expose. */
#include "acpi_mock.h"
#include "erdt.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
        struct pqos_cap cap;
        struct pqos_cpuinfo cpu;
        struct pqos_erdt_info *info = NULL;

        if (acpi_mock_set(ACPI_TABLE_SIG_ERDT, data, size) != 0)
                return -1;

        memset(&cap, 0, sizeof(cap));
        memset(&cpu, 0, sizeof(cpu));
        (void)erdt_init(&cap, &cpu, &info);
        erdt_fini();
        return 0;
}
