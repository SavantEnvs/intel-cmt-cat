/* MRRM ACPI table, read by the library from the table file the kernel would expose. */
#include "acpi_mock.h"
#include "mrrm.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
        struct pqos_cap cap;
        struct pqos_mrrm_info *info = NULL;

        if (acpi_mock_set(ACPI_TABLE_SIG_MRRM, data, size) != 0)
                return -1;

        memset(&cap, 0, sizeof(cap));

        (void)mrrm_init(&cap, &info);
        mrrm_fini();
        return 0;
}
