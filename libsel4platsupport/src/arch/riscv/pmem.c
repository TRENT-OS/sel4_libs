/*
 * Copyright 2017, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(DATA61_BSD)
 */

#include <stdint.h>
#include <sel4platsupport/pmem.h>
#include <utils/util.h>

#if 0
     { /*.start = */ 0x40000000, /* .end = */ 0x40100000},
     { /*.start = */ 0x40280000, /* .end = */ 0x40800000},
#endif

int sel4platsupport_get_num_pmem_regions(UNUSED simple_t *simple) {
    return 2;
}

int sel4platsupport_get_pmem_region_list(UNUSED simple_t *simple, UNUSED size_t max_length, UNUSED pmem_region_t *region_list) {
    size_t k = max_length;
    size_t i;
    if (k > 2)
    {
        k = 2;
    }

    for (i = 0; i < k; i++) {
        printf("seL4_test allocator - k: %d\n", i);
        if (i == 0) {
            region_list[i].type = PMEM_TYPE_DEVICE; //PMEM_TYPE_RAM;
            region_list[i].base_addr = 0x40000000;
            region_list[i].length  = 1024 * 1024;
        }
        if (i == 1) {
            region_list[i].type = PMEM_TYPE_DEVICE; //PMEM_TYPE_RAM;
            region_list[i].base_addr = 0x40400000;
            region_list[i].length  = 4 * 1024 * 1024;
        }
    }

    return i;
}
