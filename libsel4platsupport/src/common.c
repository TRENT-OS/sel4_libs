/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
/*
 * This file provides routines that can be called by other libraries to access
 * platform-specific devices such as the serial port. Perhaps some day this may
 * be refactored into a more structured userspace driver model, but for now we
 * just provide the bare minimum for userspace to access basic devices such as
 * the serial port on any platform.
 */

#include <autoconf.h>
#include <sel4platsupport/gen_config.h>
#include <sel4muslcsys/gen_config.h>
#include <assert.h>
#include <sel4/sel4.h>
#include <sel4/bootinfo.h>
#include <sel4/invocation.h>
#include <sel4platsupport/device.h>
#include <sel4platsupport/platsupport.h>
#include <vspace/page.h>
#include <simple/simple_helpers.h>
#include <vspace/vspace.h>
#include "plat_internal.h"
#include <stddef.h>
#include <stdio.h>
#include <vka/capops.h>
#include <string.h>
#include <sel4platsupport/arch/io.h>
#include <simple-default/simple-default.h>
#include <utils/util.h>

#if !(defined(CONFIG_LIB_SEL4_PLAT_SUPPORT_USE_SEL4_DEBUG_PUTCHAR) || defined(CONFIG_DEBUG_BUILD))
#define USE_DEBUG_HARDWARE
#endif

enum serial_setup_status {
    NOT_INITIALIZED = 0,
    START_REGULAR_SETUP,
    START_FAILSAFE_SETUP,
    SETUP_COMPLETE
};

typedef struct {
    enum serial_setup_status setup_status;
#if USE_DEBUG_HARDWARE
    ps_io_ops_t io_ops;
    seL4_CPtr device_cap;
    vka_t *vka;
    vspace_t *vspace;
    /* To keep failsafe setup we need actual memory for a simple and a vka */
    simple_t simple_mem;
    vka_t vka_mem;

#endif
} ctx_t;

/* Tracking initialisation variables. This is currently just to avoid passing
 * parameters down to the platform code for backwards compatibility reasons.
 * This is strictly to avoid refactoring all existing platform code
 */
static ctx_t ctx = {
    .setup_status = NOT_INITIALIZED
};

extern char __executable_start[];

#if USE_DEBUG_HARDWARE
static void *__map_device_page(void *cookie, uintptr_t paddr, size_t size,
                               int cached, ps_mem_flags_t flags)
{
    seL4_Error err;

    if (0 != ctx.device_cap)
        /* we only support a single page for the serial device. */
        abort();
        UNRECHABLE();
    }

    vka_object_t dest;
    int bits = CTZ(size);
    err = sel4platsupport_alloc_frame_at(vka, paddr, bits, &dest);
    if (err) {
        ZF_LOGE("Failed to get cap for serial device frame");
        abort();
        UNRECHABLE();
    }

    ctx.device_cap = dest.cptr;

    if ((setup_status == START_REGULAR_SETUP) && ctx.vspace)  {
        /* map device page regularly */
        void *vaddr = vspace_map_pages(ctx.vspace, &dest.cptr, NULL, seL4_AllRights, 1, bits, 0);
        if (!vaddr) {
            ZF_LOGE("Failed to map serial device");
            abort();
            UNRECHABLE();
        }
        return vaddr;
    }

    /* Try a last ditch attempt to get serial device going, so we can print out
     * an error. Find a properly aligned virtual address and try to map the
     * device cap there.
     */
    if ((setup_status == START_FAILSAFE_SETUP) || !ctx.vspace) {
        seL4_Word header_start = ((seL4_Word)__executable_start - PAGE_SIZE_4K;
        seL4_Word vaddr = (header_start - BIT(bits)) & ~(BIT(bits) - 1);
        err = seL4_ARCH_Page_Map(device_cap, seL4_CapInitThreadPD, vaddr, seL4_AllRights, 0);
        if (err) {
            abort();
            UNRECHABLE();
        }
        return (void *)vaddr;
    }

    ZF_LOGE("invalid setup state %d", setup_status);
    abort();
}

static int __setup_io_ops(
    simple_t *simple __attribute__((unused)))
{
    ctx.io_ops.io_map_fn = &__map_device_page;

#ifdef CONFIG_ARCH_X86
    sel4platsupport_get_io_port_ops(&ctx.io_ops.io_port_ops, simple, ctx.vka);
#endif
    return platsupport_serial_setup_io_ops(&ctx.io_ops);
}

#endif /* USE_DEBUG_HARDWARE */

/*
 * This function is designed to be called when creating a new cspace/vspace,
 * and the serial port needs to be hooked in there too.
 */
void platsupport_undo_serial_setup(void)
{
    /* Re-initialise some structures. */
    ctx.setup_status = NOT_INITIALIZED;
#if USE_DEBUG_HARDWARE
    if (ctx.device_cap) {
        cspacepath_t path;
        seL4_ARCH_Page_Unmap(ctx.device_cap);
        vka_cspace_make_path(ctx.vka, device_cap, &path);
        vka_cnode_delete(&path);
        vka_cspace_free(ctx.vka, ctx.device_cap);
        ctx.device_cap = 0;
    }
    ctx.vka = NULL;
#endif /* USE_DEBUG_HARDWARE */
}

/* Initialise serial input interrupt. */
void platsupport_serial_input_init_IRQ(void)
{
}

int platsupport_serial_setup_io_ops(ps_io_ops_t *io_ops)
{
    int err = 0;
    if (ctx.setup_status == SETUP_COMPLETE) {
        return 0;
    }
    err = __plat_serial_init(io_ops);
    if (!err) {
        ctx.setup_status = SETUP_COMPLETE;
    }
    return err;
}

int platsupport_serial_setup_bootinfo_failsafe(void)
{
    if (ctx.setup_status == SETUP_COMPLETE) {
        return 0;
    }

#if USE_DEBUG_HARDWARE
    ctx.setup_status = START_FAILSAFE_SETUP;

    simple_t *simple = &(ctx.simple_mem);
    memset(simple, 0, sizeof(*simple));

    vka_t *vka = &(ctx.vka_mem);
    memset(vka, 0, sizeof(*vka));

    simple_default_init_bootinfo(simple, platsupport_get_bootinfo());
    simple_make_vka(simple, vka);
    ctx.vka = vka;

    return __setup_io_ops(simple);
#else
    /* only support putchar on a debug kernel */
    ctx.setup_status = SETUP_COMPLETE;
    return 0;
#endif
}

int platsupport_serial_setup_simple(
    vspace_t *vspace __attribute__((unused)),
    simple_t *simple __attribute__((unused)),
    vka_t *vka __attribute__((unused)))
{
    int err = 0;
    if (ctx.setup_status == SETUP_COMPLETE) {
        return 0;
    }
    if (ctx.setup_status != NOT_INITIALIZED) {
        ZF_LOGE("Trying to initialise a partially initialised serial. Current setup status is %d\n", ctx.setup_status);
        assert(!"You cannot recover");
        return -1;
    }
#if USE_DEBUG_HARDWARE
    /* start setup */
    ctx.setup_status = START_REGULAR_SETUP;
    ctx.vspace = vspace;
    ctx.vka = vka;
    err = __setup_io_ops(simple); /* uses global variables simple and vka */
#else
    /* only support putchar on a debug kernel */
    ctx.setup_status = SETUP_COMPLETE;
#endif
    return err;
}

/* this function is called if we attempt to do serial and it isn't setup.
 * we now need to handle this somehow */
static void __serial_setup()
{
    switch (ctx.setup_status) {

    case SETUP_COMPLETE:
        return; /* Caller should not even call us in this state. */

    case NOT_INITIALIZED:
    case START_REGULAR_SETUP:
        break;

    default: /* includes START_FAILSAFE_SETUP */
        /* we're stuck. */
        abort();
    }

#ifdef CONFIG_LIB_SEL4_PLAT_SUPPORT_USE_SEL4_DEBUG_PUTCHAR
    ctx.setup_status = SETUP_COMPLETE;
    ZF_LOGI("skip serial setup and use kernel char I/O syscalls");
#else
    /* Attempt failsafe initialization to be able to print something. */
    int err = platsupport_serial_setup_bootinfo_failsafe();
    if (err || (START_REGULAR_SETUP != ctx.setup_status)) {
        /* this may not proint anything */
        ZF_LOGE("You attempted to print before initialising the"
                " libsel4platsupport serial device!");
        abort();
        UNRECHABLE();
    }

    /* Setup worked, so this warning will show up. */
    ZF_LOGW("Regular serial setup failed.\n"
            "This message coming to you courtesy of failsafe serial.\n"
            "Your vspace has been clobbered but we will keep running"
            " to get any more error output");
#endif
}

void NO_INLINE
#ifdef CONFIG_LIB_SEL4_MUSLC_SYS_ARCH_PUTCHAR_WEAK
WEAK
#endif
__arch_putchar(int c)
{
    if (ctx.setup_status != SETUP_COMPLETE) {
        __serial_setup();
    }
    __plat_putchar(c);
}

size_t NO_INLINE
#ifdef CONFIG_LIB_SEL4_MUSLC_SYS_ARCH_PUTCHAR_WEAK
WEAK
#endif
__arch_write(char *data, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        __arch_putchar(data[i]);
    }
    return count;
}

int __arch_getchar(void)
{
    if (ctx.setup_status != SETUP_COMPLETE) {
        __serial_setup();
    }
    return __plat_getchar();
}
