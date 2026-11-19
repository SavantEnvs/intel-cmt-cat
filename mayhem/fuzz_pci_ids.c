/*
 * pci.ids database, as read to name a PCI device. The first 8 bytes pick the device (class code
 * 24 bits, vendor ID and device ID 16 bits each, little endian); the rest is the database.
 */
#include "pci.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HEADER_SIZE 8

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
        struct pqos_pci_info info;
        uint32_t class_code;
        uint16_t vendor_id;
        uint16_t device_id;
        FILE *fd;

        /* fmemopen() fails on an empty buffer: a database needs at least a byte */
        if (size <= HEADER_SIZE)
                return -1;

        memcpy(&class_code, data, sizeof(class_code));
        memcpy(&vendor_id, data + 4, sizeof(vendor_id));
        memcpy(&device_id, data + 6, sizeof(device_id));
        class_code &= 0xFFFFFF;

        fd = fmemopen((void *)(data + HEADER_SIZE), size - HEADER_SIZE, "r");
        if (fd == NULL)
                return -1;

        memset(&info, 0, sizeof(info));
        pci_ids_read(fd, &info, class_code, vendor_id, device_id);
        fclose(fd);
        return 0;
}
