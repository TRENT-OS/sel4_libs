/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* defining _GNU_SOURCE to make certain constants appear in muslc. This is rather hacky */
#define _GNU_SOURCE

#include <autoconf.h>
#include <sel4muslcsys/gen_config.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <errno.h>
#include <assert.h>

#include <muslcsys/sys_morecore.h>

/* don't enable dynamic support by default of a static area is set up */
#if CONFIG_LIB_SEL4_MUSLC_SYS_MORECORE_BYTES > 0
#define ENABLE_SEL4MUSLCSYS_MORECORE_USING_VSPACE
#endif

#ifdef ENABLE_SEL4MUSLCSYS_MORECORE_USING_VSPACE
#include <vspace/vspace.h>
#endif

#include <sel4utils/util.h>
#include <sel4utils/mapping.h>


/*
 * A static morecore area take precedence over dynamic allocation from a vspace.
 * This implementation behavior is practical choice, because the current build
 * system builds the same library for each app, so it must support all usage
 * scenarios. Actually, static and dynamic morecore areas are not supposed to be
 * used together. However, checking for a fixed area first allows setting up
 * specific area for certain parts of the code (e.g. startup) and switch to
 * a dynamic handling later. But that's not an official feature.
 *
 * We can only hand out 4 KiB aligned chunks, there is no API to return them
 * again. The top and bottom of the buffer may remain unused, e.g. due to the
 * 4 KiB alignment requirement.
 *
 *             +-----------------+  morecore_get_end() = buffer + size
 *             |/////////////////|
 *             |/unusable space//|
 *             |/////////////////|
 *        ---  +-----------------+  morecore_get_top()
 *         ^   |                 |
 *    free |   | available space |
 *         v   |                 |
 *        ---  +-----------------+  morecore_get_base()
 *         ^   |/////////////////|
 *  offset |   |/unusable space//|
 *         v   |/////////////////|
 *        ---  +-----------------+  buffer
 *
 */

#if CONFIG_LIB_SEL4_MUSLC_SYS_MORECORE_BYTES > 0
static char __attribute__((aligned(PAGE_SIZE_4K))) morecore_area[CONFIG_LIB_SEL4_MUSLC_SYS_MORECORE_BYTES];
#endif

static struct {
    /* for static allocation from a buffer*/
    void       *buffer;
    size_t     size;
    size_t     offset;
    size_t     free;
#ifdef ENABLE_SEL4MUSLCSYS_MORECORE_USING_VSPACE
    /* Dynamic morecore can use a custom buffer or allocate dynamically from a
     * vspace that is defined somewhere - probably in
     * apps' the main function with a setup like:
     *
     *   sel4utils_reserve_range_no_alloc(&vspace, &muslc_brk_reservation_memory,
     *                                    BRK_VIRTUAL_SIZE, seL4_AllRights, 1,
     *                                    &muslc_brk_reservation_start);
     *   morecore.vspace = &vspace;
     *   muslc_brk_reservation.res = &muslc_brk_reservation_memory;
     *
     * In case a dynamic morecore is needed for some apps and a static for others,
     * a fixed morecore area can be defined that will take preference. It must be
     * set up before calling malloc() for the first time.
     *
     */
    vspace_t        *vspace;
    uintptr_t       brk_start;
    reservation_t   brk_reservation;
    void            *brk_reservation_start;
#endif /* ENABLE_SEL4MUSLCSYS_MORECORE_USING_VSPACE */
} morecore = {
#if CONFIG_LIB_SEL4_MUSLC_SYS_MORECORE_BYTES > 0
    .buffer = morecore_area,
    .size   = sizeof(morecore_area)
#else
    0
#endif
};

#ifdef ENABLE_SEL4MUSLCSYS_MORECORE_USING_VSPACE
static inline bool morecore_has_vspace(void)
{
    return (NULL != morecore.vspace) && morecore.brk_reservation.res &&
           (NULL != morecore.brk_reservation_start);
}
#endif

static inline bool morecore_has_fixed_area(void)
{
    return (NULL != morecore.buffer);
}

static inline uintptr_t morecore_get_start(void)
{
    return (uintptr_t)morecore.buffer;
}

static inline uintptr_t morecore_get_base(void)
{
    assert(morecore.offset <= morecore.size);
    return morecore_get_start() + morecore.offset;
}

static inline uintptr_t morecore_get_top(void)
{
    assert(morecore.offset + morecore.free <= morecore.size);
    return morecore_get_start() + morecore.free;
}

static inline uintptr_t morecore_get_end(void)
{
    return morecore_get_start() + morecore.size;
}

void sel4muslcsys_setup_morecore_region(void *area, size_t size)
{
    /* This will overwrite an older area or an internal aread that was set up.
     * For the internal area this is not an issue, because the are still exists
     * and so any allocations continue to exists. For a custom area, the caller
     * must ensure that the older ares is kept if any allocation have been made.
     */

    ZF_LOGW_IF(morecore.size > 0,
               "Warning: overwriting existing morecore area");

    ZF_LOGE_IF(NULL == area,
               "Warning: static morecore area is NULL");
    ZF_LOGE_IF(0 == size,
               "Warning: static morecore size is 0");

    // ToDo: align the buffer/size in case it is not aligned,
    ZF_LOGF_IF(((uintptr_t)area % 0x1000) != 0,
               "morecore buffer %p not 4 KiB aligned", area);
    ZF_LOGF_IF((size % 0x1000) != 0,
               "morecore buffer size %d not 4 KiB aligned", size);


    morecore = (typeof(morecore)){
        .buffer   = area,
        .size     = size,
        .offset   = 0,
        .free     = size,
        // ToDo: .vspace   = vspace
    };

    ZF_LOGD("morecore %p - %p (%#zd)",
            morecore_get_base(), morecore_get_top(), morecore.size);
}

void sel4muslcsys_get_morecore_region(void **p_area, size_t *p_size)
{
    bool has_fixed_area = morecore_has_fixed_area();
    if (p_area) {
        *p_area = has_fixed_area ? morecore.buffer : NULL;
    }

    if (p_size) {
        *p_size = has_fixed_area ? morecore.size : 0;
    }
}

long sys_brk(va_list ap)
{
    /* ToDo: can't we make this a dummy, as it seems to be deprecated? */

    uintptr_t newbrk = va_arg(ap, uintptr_t);

    if (morecore_has_fixed_area()) {

        /* Using 0 means querying the address. */
        if (0 == newbrk) {
            ZF_LOGI("caller queries fixed base address");
            return morecore_get_base();
        }

        if (newbrk < morecore_get_start() && newbrk >= morecore_get_top()) {
            ZF_LOGE("invalid newbrk %p", newbrk);
            return 0;
        }

        ZF_LOGF_IF(((uintptr_t)newbrk % 0x1000) != 0,
                "newbrk %p not 4 KiB aligned", newbrk);


        ZF_LOGI("morecore_base change: %p -> %p", morecore_get_base(), newbrk);

        size_t offs_top = morecore.offset + morecore.free;
        morecore.offset = newbrk - (uintptr_t)morecore.buffer;
        morecore.free = offs_top - morecore.offset;
        assert(newbrk == morecore_get_base());
        return morecore_get_base();
    }

#ifdef ENABLE_SEL4MUSLCSYS_MORECORE_USING_VSPACE
    if (morecore_has_vspace()) {

        /* Using 0 means querying the address. */
        if (0 == newbrk) {
            ZF_LOGI("caller queries dynamic base address");
            return morecore.brk_start;
        }

        /* try and map pages until this point */
        while (morecore.brk_start < newbrk) {
            int error = vspace_new_pages_at_vaddr(morecore.vspace,
                                                  (void *)morecore.brk_start, 1,
                                                  seL4_PageBits,
                                                  morecore.brk_reservation);
            if (error) {
                ZF_LOGE("Mapping new pages to extend brk region failed");
                return 0;
            }
            morecore.brk_start += PAGE_SIZE_4K;
        }

        return morecore.brk_start;

    }
#endif /* ENABLE_SEL4MUSLCSYS_MORECORE_USING_VSPACE */

    ZF_LOGE("using malloc requires setting up sel4muslcsys morecore");
    assert(0);

    return 0;
}

long sys_mremap(va_list ap)
{
    if (morecore_has_fixed_area()) {
        assert(!"not implemented");
        return -ENOMEM;
    }

#ifdef ENABLE_SEL4MUSLCSYS_MORECORE_USING_VSPACE
    if (morecore_has_vspace()) {
        void *old_address = va_arg(ap, void *);
        size_t old_size = va_arg(ap, size_t);
        size_t new_size = va_arg(ap, size_t);
        int flags = va_arg(ap, int);
        UNUSED void *new_address_arg;

        assert(flags == MREMAP_MAYMOVE);
        assert(IS_ALIGNED_4K(old_size));
        assert(IS_ALIGNED_4K((uintptr_t) old_address));
        assert(IS_ALIGNED_4K(new_size));
        /* we currently only support remaping to size >= original */
        assert(new_size >= old_size);

        if (flags & MREMAP_FIXED) {
            new_address_arg = va_arg(ap, void *);
        }

        /* first find all the old caps */
        int num_pages = old_size >> seL4_PageBits;
        seL4_CPtr caps[num_pages];
        uintptr_t cookies[num_pages];
        int i;
        for (i = 0; i < num_pages; i++) {
            void *vaddr = old_address + i * BIT(seL4_PageBits);
            caps[i] = vspace_get_cap(morecore.vspace, vaddr);
            cookies[i] = vspace_get_cookie(morecore.vspace, vaddr);
        }
        /* unmap the previous mapping */
        vspace_unmap_pages(morecore.vspace, old_address, num_pages, seL4_PageBits, VSPACE_PRESERVE);
        /* reserve a new region */
        int error;
        void *new_address;
        int new_pages = new_size >> seL4_PageBits;
        reservation_t reservation = vspace_reserve_range(morecore.vspace, new_pages * PAGE_SIZE_4K, seL4_AllRights, 1,
                                                         &new_address);
        if (!reservation.res) {
            ZF_LOGE("Failed to make reservation for remap\n");
            goto restore;
        }
        /* map all the existing pages into the reservation */
        error = vspace_map_pages_at_vaddr(morecore.vspace, caps, cookies, new_address, num_pages, seL4_PageBits, reservation);
        if (error) {
            ZF_LOGE("Mapping existing pages into new reservation failed");
            vspace_free_reservation(morecore.vspace, reservation);
            goto restore;
        }
        /* create any new pages */
        error = vspace_new_pages_at_vaddr(morecore.vspace, new_address + num_pages * PAGE_SIZE_4K, new_pages - num_pages,
                                          seL4_PageBits, reservation);
        if (error) {
            ZF_LOGE("Creating new pages for remap region failed");
            vspace_unmap_pages(morecore.vspace, new_address, num_pages, seL4_PageBits, VSPACE_PRESERVE);
            vspace_free_reservation(morecore.vspace, reservation);
            goto restore;
        }
        /* free the reservation book keeping */
        vspace_free_reservation(morecore.vspace, reservation);
        return (long)new_address;
    restore:
        /* try and recreate the original mapping */
        reservation = vspace_reserve_range_at(morecore.vspace, old_address, num_pages * PAGE_SIZE_4K, seL4_AllRights, 1);
        assert(reservation.res);
        error = vspace_map_pages_at_vaddr(morecore.vspace, caps, cookies, old_address,
                                          num_pages, seL4_PageBits, reservation);
        assert(!error);
        return -ENOMEM;
    }
#endif /* ENABLE_SEL4MUSLCSYS_MORECORE_USING_VSPACE */

    ZF_LOGE("mrepmap requires morecore_area or muslc* vars to be initialised");
    assert(0);

    return 0;
}

/* Large mallocs will result in muslc calling mmap, so we do a minimal implementation
   here to support that. We make a bunch of assumptions in the process */
static long sys_mmap_impl(void *addr, size_t length, int prot, int flags,
                          int fd, off_t offset)
{
    if (morecore_has_fixed_area()) {

        if (flags & MAP_ANONYMOUS) {

           /* Calculate number of pages needed */
           uintptr_t adjusted_length = PAGE_SIZE_4K * BYTES_TO_4K_PAGES(length);

            /* Steal from the top */
            if (length > morecore.free) {
                ZF_LOGE("out of memory, have %zu, need %zu (%zu)", morecore.free, adjusted_length, length);
                return -ENOMEM;
            }

            morecore.free -= length;
            long chunk = morecore_get_top();

            ZF_LOGF_IF(
                (morecore_get_base() % 0x1000) != 0,
                "morecore %p - %p, len %#zx, return address: 0x%"PRIxPTR" (not 4 KiB aligned)",
                morecore_get_start(), morecore_get_end(), length, chunk);

            // ZF_LOGI(
            //     "morecore [%p - %p], pool [%p - %p], alloc: len %#x at 0x%"PRIxPTR,
            //     morecore_get_start(), morecore_get_end(),
            //     morecore_get_base(), morecore_get_top(),
            //     length, chunk);

            return chunk;

        }

        assert(!"not implemented");
        return -ENOMEM;
    }

#ifdef ENABLE_SEL4MUSLCSYS_MORECORE_USING_VSPACE
    if (morecore_has_vspace()) {

        if (flags & MAP_ANONYMOUS) {
            /* determine how many pages we need */
            uint32_t pages = BYTES_TO_4K_PAGES(length);
            void *ret = vspace_new_pages(morecore.vspace, seL4_AllRights,
                                         pages, seL4_PageBits);
            ZF_LOGF_IF((((uintptr_t)ret) % 0x1000) != 0,
                       "return address: 0x%"PRIxPTR" requires alignment: 0x%x ",
                       (uintptr_t)ret, 0x1000);
            return (long)ret;
        }

        assert(!"not implemented");
        return -ENOMEM;
    }
#endif /* ENABLE_SEL4MUSLCSYS_MORECORE_USING_VSPACE */

    ZF_LOGE("using malloc requires setting up sel4muslcsys morecore");
    assert(0);

    return 0;
}

long sys_mmap(va_list ap)
{
    void *addr = va_arg(ap, void *);
    size_t length = va_arg(ap, size_t);
    int prot = va_arg(ap, int);
    int flags = va_arg(ap, int);
    int fd = va_arg(ap, int);
    off_t offset = va_arg(ap, off_t);
    return sys_mmap_impl(addr, length, prot, flags, fd, offset);
}

long sys_mmap2(va_list ap)
{
    void *addr = va_arg(ap, void *);
    size_t length = va_arg(ap, size_t);
    int prot = va_arg(ap, int);
    int flags = va_arg(ap, int);
    int fd = va_arg(ap, int);
    off_t offset = va_arg(ap, off_t);
    /* for now redirect to mmap. muslc always defines an off_t as being an int64 */
    /* so this will not overflow */
    return sys_mmap_impl(addr, length, prot, flags, fd, offset * 4096);
}

long sys_munmap(va_list ap)
{
    ZF_LOGE("%s is unsupported. This may have been called due to a "
            "large malloc'd region being free'd.", __func__);
    return 0;
}
