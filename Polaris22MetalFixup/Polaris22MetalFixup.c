//
//  Polaris22MetalFixup.c
//  Polaris22MetalFixup
//
//  Copyright © 2019 osy86. All rights reserved.
//

#include <mach/mach_types.h>
#include <IOKit/IOLib.h>
#include <i386/proc_reg.h>
#include <sys/vnode.h>

#define UNLIKELY(x) __builtin_expect(!!(x), 0)

extern boolean_t cs_validate_range(vnode_t vp,
                                   memory_object_t pager,
                                   memory_object_offset_t offset,
                                   const void *data,
                                   vm_size_t size,
                                   unsigned *result);

typedef boolean_t (*cs_validate_range_t)(vnode_t, memory_object_t, memory_object_offset_t, const void *, vm_size_t, unsigned *);

#pragma mark - Store original function and trampoline

static const int kFuncPrefixLength = 20;
static const int kPathMaxLen = 1024;

static const struct {
    uint8_t origCode[kFuncPrefixLength];
    uint8_t jmpInst[8];
    uintptr_t origAddr;
} __attribute__((packed)) gTrampolineToOrig __attribute__((section("__TEXT,__text"))) = {
    {0}, // original code prefix
    {0xFF,0x25,0x02,0x00,0x00,0x00,0x00,0x00}, // JMP [RIP+2]
    0 // addr
};

static const cs_validate_range_t orig_cs_validate_range = (cs_validate_range_t)&gTrampolineToOrig;

#pragma mark - Metal patches

static const uint8_t kAmdBronzeMtlAddrLibGetBaseArrayModeReturnOriginal[] = {
    0xb8, 0x02, 0x00, 0x00, 0x00, 0x0f, 0x43, 0xc1, 0xeb, 0x0f,
};

static const uint8_t kAmdBronzeMtlAddrLibGetBaseArrayModeReturnPatched[] = {
    0xb8, 0x02, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90, 0xeb, 0x0f,
};

static const char kAmdBronzeMtlDriverPath[] = "/System/Library/Extensions/AMDMTLBronzeDriver.bundle/Contents/MacOS/AMDMTLBronzeDriver";

static const char kDyldCachePath[] = "/private/var/db/dyld/dyld_shared_cache_x86_64h";

extern void *memmem(const void *h0, size_t k, const void *n0, size_t l);

#pragma mark - Kernel patching code

static inline int cli(void) {
    unsigned long flags;
    asm volatile ("pushf; pop %0; cli;" : "=r" (flags));
    return !!(flags & EFL_IF);
}

static inline void sti(void) {
    asm volatile ("sti; nop;");
}

/**
 * Call block with interrupts and protections disabled
 */
static void doKernelPatch(void (^patchFunc)(void)) {
    int intrflag = cli();
    uintptr_t cr0 = get_cr0();
    set_cr0(cr0 & ~CR0_WP);
    
    patchFunc();
    
    set_cr0(cr0);
    if (intrflag) {
        sti();
    }
}

#pragma mark - Patched functions

static boolean_t patched_cs_validate_range(vnode_t vp,
                                           memory_object_t pager,
                                           memory_object_offset_t offset,
                                           const void *data,
                                           vm_size_t size,
                                           unsigned *result) {
    char path[kPathMaxLen];
    int pathlen = kPathMaxLen;
    boolean_t res = orig_cs_validate_range(vp, pager, offset, data, size, result);
    if (vn_getpath(vp, path, &pathlen) != 0) {
        static_assert(sizeof(kAmdBronzeMtlDriverPath) <= sizeof(path));
        static_assert(sizeof(kDyldCachePath) <= sizeof(path));
        if (UNLIKELY(strncmp(path, kAmdBronzeMtlDriverPath, sizeof(kAmdBronzeMtlDriverPath)) == 0) ||
            UNLIKELY(strncmp(path, kDyldCachePath, sizeof(kDyldCachePath)) == 0)) {
            void *res;
            if (UNLIKELY((res = memmem(data, size, kAmdBronzeMtlAddrLibGetBaseArrayModeReturnOriginal, sizeof(kAmdBronzeMtlAddrLibGetBaseArrayModeReturnOriginal))) != NULL)) {
                IOLog("Polaris22MetalFixup: found code to patch!\n");
                doKernelPatch(^{
                    static_assert(sizeof(kAmdBronzeMtlAddrLibGetBaseArrayModeReturnOriginal) == sizeof(kAmdBronzeMtlAddrLibGetBaseArrayModeReturnPatched));
                    memcpy(res, kAmdBronzeMtlAddrLibGetBaseArrayModeReturnPatched, sizeof(kAmdBronzeMtlAddrLibGetBaseArrayModeReturnPatched));
                });
            }
        }
    }
    return res;
}

static const struct {
    uint8_t jmpInst[8];
    uintptr_t origAddr;
} __attribute__((packed)) gTrampolineToPatched = {
    {0xFF,0x25,0x02,0x00,0x00,0x00,0x00,0x00}, // JMP [RIP+2]
    (uintptr_t)patched_cs_validate_range
};

#pragma mark - Patches on start/stop

kern_return_t Polaris22MetalFixup_start(kmod_info_t * ki, void *d) {
    IOLog("Polaris22MetalFixup: patching cs_validate_range\n");
    doKernelPatch(^{
        // first setup the trampoline
        uintptr_t addr = (uintptr_t)cs_validate_range + sizeof(gTrampolineToOrig.origCode);
        memcpy((void *)&gTrampolineToOrig.origCode, cs_validate_range, sizeof(gTrampolineToOrig.origCode));
        memcpy((void *)&gTrampolineToOrig.origAddr, &addr, sizeof(gTrampolineToOrig.origAddr));
        
        // then overwrite the original function
        static_assert(sizeof(gTrampolineToPatched) <= sizeof(gTrampolineToOrig.origCode));
        memcpy((void *)cs_validate_range, &gTrampolineToPatched, sizeof(gTrampolineToPatched));
    });
    return KERN_SUCCESS;
}

kern_return_t Polaris22MetalFixup_stop(kmod_info_t *ki, void *d) {
    IOLog("Polaris22MetalFixup: removing patches for cs_validate_range\n");
    doKernelPatch(^{
        // restore the original function
        memcpy((void *)cs_validate_range, &gTrampolineToOrig.origCode, sizeof(gTrampolineToOrig.origCode));
    });
    return KERN_SUCCESS;
}
