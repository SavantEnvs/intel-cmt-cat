/*
 * The memory region description pqos_init() builds from four ACPI tables: MRRM, read by
 * mrrm_init(), and then SRAT, HMAT and CEDT, read and correlated with it by mem_regions_init(). The
 * input is the tables directory: tables back to back, each served under its own signature (see
 * acpi_mock_set_tables()); a table the input does not hold is absent, which the library handles.
 *
 * mem_regions_init() runs only when mrrm_init() succeeded, as in pqos_init() (lib/cap.c).
 */
#include "acpi_mock.h"
#include "mem_regions.h"
#include "mrrm.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
        struct pqos_cap cap;
        struct pqos_mrrm_info *mrrm = NULL;
        struct pqos_mem_regions *regions = NULL;

        if (acpi_mock_set_tables(data, size) != 0)
                return -1;

        memset(&cap, 0, sizeof(cap));
        if (mrrm_init(&cap, &mrrm) == PQOS_RETVAL_OK) {
                (void)mem_regions_init(mrrm, &regions);
                (void)mem_regions_fini();
        }
        mrrm_fini();
        return 0;
}
