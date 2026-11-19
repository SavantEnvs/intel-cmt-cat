/*
 * IRDT ACPI table (I/O RDT topology), read by the library from the table file the kernel would
 * expose, and walked the way iordt_init() walks it: acpi_print(), then every RMUD, every device of
 * each, and the channel mappings of every DSS.
 *
 * iordt_init() itself is not called: right after these walks, iordt_populate_mmio() looks up the
 * PCI device behind each DSS-to-RCS mapping with pci_dev_get(), which reads PCI config space with
 * the outl/inb instructions. Those need iopl(3) (CAP_SYS_RAWIO), which a fuzzing container does not
 * have; wrapping iopl() would only turn every such input into a SIGSEGV in the port access.
 */
#include "acpi.h"
#include "acpi_mock.h"
#include "acpi_table.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
        struct acpi_table *table;
        struct acpi_table_irdt_rmud **rmuds;
        size_t rmuds_num = 0;
        size_t r;

        if (acpi_mock_set(ACPI_TABLE_SIG_IRDT, data, size) != 0)
                return -1;

        if (acpi_init() != PQOS_RETVAL_OK)
                return 0;
        table = acpi_get_sig(ACPI_TABLE_SIG_IRDT);
        if (table == NULL)
                goto out;

        acpi_print(table);
        rmuds = acpi_get_irdt_rmud(table->irdt, &rmuds_num);
        for (r = 0; rmuds != NULL && r < rmuds_num; r++) {
                struct acpi_table_irdt_device **devs;
                size_t devs_num = 0;
                size_t d;

                devs = acpi_get_irdt_dev(rmuds[r], &devs_num);
                for (d = 0; devs != NULL && d < devs_num; d++) {
                        struct acpi_table_irdt_chms **chms = NULL;
                        size_t chms_num = 0;

                        if (devs[d]->type != ACPI_TABLE_IRDT_TYPE_DSS)
                                continue;
                        (void)acpi_get_irdt_chms(devs[d], &chms, &chms_num);
                        free(chms);
                }
                free(devs);
        }
        free(rmuds);
        acpi_free(table);
out:
        (void)acpi_fini();
        return 0;
}
