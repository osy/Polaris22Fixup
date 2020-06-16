//
//  kern_start.cpp
//  Polaris22Fixup
//
//  Copyright © 2020 osy86. All rights reserved.
//

#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>

#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define MODULE_SHORT "p22"

extern "C" void *memmem(const void *h0, size_t k, const void *n0, size_t l);

static const int kPathMaxLen = 1024;

#pragma mark - Patches

static const uint8_t kAmdBronzeMtlAddrLibGetBaseArrayModeReturnOriginal[] = {
    0xb8, 0x02, 0x00, 0x00, 0x00, 0x0f, 0x43, 0xc1, 0xeb,
};

static const uint8_t kAmdBronzeMtlAddrLibGetBaseArrayModeReturnPatched[] = {
    0xb8, 0x02, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90, 0xeb,
};

static const char kAmdBronzeMtlDriverPath[] = "/System/Library/Extensions/AMDMTLBronzeDriver.bundle/Contents/MacOS/AMDMTLBronzeDriver";

static const char kDyldCachePath[] = "/private/var/db/dyld/dyld_shared_cache_x86_64h";

static const char *kAmdRadeonX4000HwLibsPath[] { "/System/Library/Extensions/AMDRadeonX4000HWServices.kext/Contents/PlugIns/AMDRadeonX4000HWLibs.kext/Contents/MacOS/AMDRadeonX4000HWLibs" };

static KernelPatcher::KextInfo kAMDHWLibsInfo[] = {
    { "com.apple.kext.AMDRadeonX4000HWLibs", kAmdRadeonX4000HwLibsPath, arrsize(kAmdRadeonX4000HwLibsPath), {true}, {}, KernelPatcher::KextInfo::Unloaded },
};

static mach_vm_address_t orig_cs_validate_range {};

static mach_vm_address_t orig_IsEarlySAMUInitEnabled {};

#pragma mark - Kernel patching code

/**
 * Call block with interrupts and protections disabled
 */
static void doKernelPatch(void (^patchFunc)(void)) {
    if (MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) == KERN_SUCCESS) {
        DBGLOG(MODULE_SHORT, "obtained write permssions");
    } else {
        SYSLOG(MODULE_SHORT, "failed to obtain write permissions");
        return;
    }
    
    patchFunc();
    
    if (MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock) == KERN_SUCCESS) {
        DBGLOG(MODULE_SHORT, "restored write permssions");
    } else {
        SYSLOG(MODULE_SHORT, "failed to restore write permissions");
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
    boolean_t res = FunctionCast(patched_cs_validate_range, orig_cs_validate_range)(vp, pager, offset, data, size, result);
    if (res && vn_getpath(vp, path, &pathlen) == 0) {
        static_assert(sizeof(kAmdBronzeMtlDriverPath) <= sizeof(path), "path too long");
        static_assert(sizeof(kDyldCachePath) <= sizeof(path), "path too long");
        if (UNLIKELY(strncmp(path, kAmdBronzeMtlDriverPath, sizeof(kAmdBronzeMtlDriverPath)) == 0) ||
            UNLIKELY(strncmp(path, kDyldCachePath, sizeof(kDyldCachePath)) == 0)) {
            void *res;
            if (UNLIKELY((res = memmem(data, size, kAmdBronzeMtlAddrLibGetBaseArrayModeReturnOriginal, sizeof(kAmdBronzeMtlAddrLibGetBaseArrayModeReturnOriginal))) != NULL)) {
                SYSLOG(MODULE_SHORT, "found function to patch!");
                doKernelPatch(^{
                    static_assert(sizeof(kAmdBronzeMtlAddrLibGetBaseArrayModeReturnOriginal) == sizeof(kAmdBronzeMtlAddrLibGetBaseArrayModeReturnPatched), "patch size invalid");
                    lilu_os_memcpy(res, kAmdBronzeMtlAddrLibGetBaseArrayModeReturnPatched, sizeof(kAmdBronzeMtlAddrLibGetBaseArrayModeReturnPatched));
                });
            }
        }
    }
    return res;
}

static int patched_IsEarlySAMUInitEnabled(void *ctx) {
    DBGLOG(MODULE_SHORT, "PECI_IsEarlySAMUInitEnabled: return 0");
    return 0;
}

#pragma mark - Patches on start/stop

static void pluginStart() {
    LiluAPI::Error error;
    
    DBGLOG(MODULE_SHORT, "start");
    error = lilu.onPatcherLoad([](void *user, KernelPatcher &patcher){
        DBGLOG(MODULE_SHORT, "patching cs_validate_range");
        mach_vm_address_t kern = patcher.solveSymbol(KernelPatcher::KernelID, "_cs_validate_range");
        
        if (patcher.getError() == KernelPatcher::Error::NoError) {
            orig_cs_validate_range = patcher.routeFunctionLong(kern, reinterpret_cast<mach_vm_address_t>(patched_cs_validate_range), true, true);
            
            if (patcher.getError() != KernelPatcher::Error::NoError) {
                SYSLOG(MODULE_SHORT, "failed to hook _cs_validate_range");
            } else {
                DBGLOG(MODULE_SHORT, "hooked cs_validate_range");
            }
        } else {
            SYSLOG(MODULE_SHORT, "failed to find _cs_validate_range");
        }
    });
    if (error != LiluAPI::Error::NoError) {
        SYSLOG(MODULE_SHORT, "failed to register onPatcherLoad method: %d", error);
    }
    error = lilu.onKextLoad(kAMDHWLibsInfo, arrsize(kAMDHWLibsInfo), [](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size){
        DBGLOG(MODULE_SHORT, "processing AMDRadeonX4000HWLibs");
        for (size_t i = 0; i < arrsize(kAMDHWLibsInfo); i++) {
            if (kAMDHWLibsInfo[i].loadIndex == index) {
                KernelPatcher::RouteRequest amd_requests[] {
                    KernelPatcher::RouteRequest("_PECI_IsEarlySAMUInitEnabled", patched_IsEarlySAMUInitEnabled, orig_IsEarlySAMUInitEnabled),
                };
                if (patcher.routeMultiple(index, amd_requests, address, size, true, true)) {
                    DBGLOG(MODULE_SHORT, "patched PECI_IsEarlySAMUInitEnabled");
                } else {
                    SYSLOG(MODULE_SHORT, "failed to patch PECI_IsEarlySAMUInitEnabled: %d", patcher.getError());
                }
            }
        }
    });
    if (error != LiluAPI::Error::NoError) {
        SYSLOG(MODULE_SHORT, "failed to register onKextLoad method: %d", error);
    }
}

// Boot args.
static const char *bootargOff[] {
    "-polaris22off"
};
static const char *bootargDebug[] {
    "-polaris22dbg"
};
static const char *bootargBeta[] {
    "-polaris22beta"
};

// Plugin configuration.
PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal,
    bootargOff,
    arrsize(bootargOff),
    bootargDebug,
    arrsize(bootargDebug),
    bootargBeta,
    arrsize(bootargBeta),
    KernelVersion::Mojave,
    KernelVersion::Catalina,
    pluginStart
};
