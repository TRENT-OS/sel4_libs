/*
 * Copyright 2022, HENSOLDT Cyber
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <autoconf.h>
#include <sel4muslcsys/gen_config.h>
#include <stddef.h>

void sel4muslcsys_setup_morecore_region(void *area, size_t size);
void sel4muslcsys_get_morecore_region(void **p_area, size_t *p_size);
