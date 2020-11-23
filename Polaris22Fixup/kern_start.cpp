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

static const int kEllesmereDeviceId = 0x67DF;

static const uint8_t kAmdBronzeMtlAddrLibGetBaseArrayModeReturnOriginal[] = {
    0xb8, 0x02, 0x00, 0x00, 0x00, 0x0f, 0x43, 0xc1, 0xeb,
};

static const uint8_t kAmdBronzeMtlAddrLibGetBaseArrayModeReturnPatched[] = {
    0xb8, 0x02, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90, 0xeb,
};

static constexpr size_t kAmdBronzeMtlAddrLibGetBaseArrayModeReturnSize = sizeof(kAmdBronzeMtlAddrLibGetBaseArrayModeReturnOriginal);

static_assert(kAmdBronzeMtlAddrLibGetBaseArrayModeReturnSize == sizeof(kAmdBronzeMtlAddrLibGetBaseArrayModeReturnPatched), "patch size invalid");

static const uint8_t kBigSurAmdBronzeMtlAddrLibGetBaseArrayModeReturnOriginal[] = {
    0xb9, 0x02, 0x00, 0x00, 0x00, 0x01, 0xc8, 0x41, 0x83, 0xf8, 0x21, 0x0f, 0x42, 0xc1, 0xeb,
};

static const uint8_t kBigSurAmdBronzeMtlAddrLibGetBaseArrayModeReturnPatched[] = {
    0xb9, 0x02, 0x00, 0x00, 0x00, 0x01, 0xc8, 0x41, 0x83, 0xf8, 0x00, 0x0f, 0x43, 0xc1, 0xeb,
};

static constexpr size_t kBigSurAmdBronzeMtlAddrLibGetBaseArrayModeReturnSize = sizeof(kBigSurAmdBronzeMtlAddrLibGetBaseArrayModeReturnOriginal);

static_assert(kBigSurAmdBronzeMtlAddrLibGetBaseArrayModeReturnSize == sizeof(kBigSurAmdBronzeMtlAddrLibGetBaseArrayModeReturnPatched), "patch size invalid");

static const char kAmdBronzeMtlDriverPath[kPathMaxLen] = "/System/Library/Extensions/AMDMTLBronzeDriver.bundle/Contents/MacOS/AMDMTLBronzeDriver";

static const char kDyldCachePath[kPathMaxLen] = "/private/var/db/dyld/dyld_shared_cache_x86_64h";

static const char kBigSurDyldCachePath[kPathMaxLen] = "/System/Library/dyld/dyld_shared_cache_x86_64h";

static const char *kAmdRadeonX4000HwLibsPath[] { "/System/Library/Extensions/AMDRadeonX4000HWServices.kext/Contents/PlugIns/AMDRadeonX4000HWLibs.kext/Contents/MacOS/AMDRadeonX4000HWLibs" };

static const char *kAmdRadeonX4000Path[] { "/System/Library/Extensions/AMDRadeonX4000.kext/Contents/MacOS/AMDRadeonX4000" };

enum {
    kAmdRadeonX4000=0,
    kAmdRadeonX4000HwLibs,
};

static KernelPatcher::KextInfo kAMDHWLibsInfo[] = {
    [kAmdRadeonX4000] = { "com.apple.kext.AMDRadeonX4000", kAmdRadeonX4000Path, arrsize(kAmdRadeonX4000Path), {true}, {}, KernelPatcher::KextInfo::Unloaded },
    [kAmdRadeonX4000HwLibs] = { "com.apple.kext.AMDRadeonX4000HWLibs", kAmdRadeonX4000HwLibsPath, arrsize(kAmdRadeonX4000HwLibsPath), {true}, {}, KernelPatcher::KextInfo::Unloaded },
};

static mach_vm_address_t orig_cs_validate {};
static mach_vm_address_t orig_IsEarlySAMUInitEnabled {};
static mach_vm_address_t orig_getHardwareInfo {};

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

template <size_t patchSize>
static inline void searchAndPatch(const void *haystack,
                                  size_t haystackSize,
                                  const char (&path)[kPathMaxLen],
                                  const char (&dylibCachePath)[kPathMaxLen],
                                  const uint8_t (&needle)[patchSize],
                                  const uint8_t (&patch)[patchSize]) {
    if (UNLIKELY(strncmp(path, kAmdBronzeMtlDriverPath, sizeof(kAmdBronzeMtlDriverPath)) == 0) ||
        UNLIKELY(strncmp(path, dylibCachePath, sizeof(dylibCachePath)) == 0)) {
        void *res;
        if (UNLIKELY((res = memmem(haystack, haystackSize, needle, patchSize)) != NULL)) {
            SYSLOG(MODULE_SHORT, "found function to patch!");
            SYSLOG(MODULE_SHORT, "path: %s", path);
            doKernelPatch(^{
                lilu_os_memcpy(res, patch, patchSize);
            });
        }
    }
}

#pragma mark - Patched functions

// pre Big Sur
static boolean_t patched_cs_validate_range(vnode_t vp,
                                           memory_object_t pager,
                                           memory_object_offset_t offset,
                                           const void *data,
                                           vm_size_t size,
                                           unsigned *result) {
    char path[kPathMaxLen];
    int pathlen = kPathMaxLen;
    boolean_t res = FunctionCast(patched_cs_validate_range, orig_cs_validate)(vp, pager, offset, data, size, result);
    if (res && vn_getpath(vp, path, &pathlen) == 0) {
        searchAndPatch(data, size, path, kDyldCachePath, kAmdBronzeMtlAddrLibGetBaseArrayModeReturnOriginal, kAmdBronzeMtlAddrLibGetBaseArrayModeReturnPatched);
    }
    return res;
}

// For Big Sur
static void patched_cs_validate_page(vnode_t vp,
                                          memory_object_t pager,
                                          memory_object_offset_t page_offset,
                                          const void *data,
                                          int *arg4,
                                          int *arg5,
                                          int *arg6) {
    char path[kPathMaxLen];
    int pathlen = kPathMaxLen;
    FunctionCast(patched_cs_validate_page, orig_cs_validate)(vp, pager, page_offset, data, arg4, arg5, arg6);
    if (vn_getpath(vp, path, &pathlen) == 0) {
        searchAndPatch(data, PAGE_SIZE, path, kBigSurDyldCachePath, kBigSurAmdBronzeMtlAddrLibGetBaseArrayModeReturnOriginal, kBigSurAmdBronzeMtlAddrLibGetBaseArrayModeReturnPatched);
    }
}

static int patched_IsEarlySAMUInitEnabled(void *ctx) {
    DBGLOG(MODULE_SHORT, "PECI_IsEarlySAMUInitEnabled: return 0");
    return 0;
}

static int patched_getHardwareInfo(void *obj, uint16_t *hwInfo) {
    int ret = FunctionCast(patched_getHardwareInfo, orig_getHardwareInfo)(obj, hwInfo);
    DBGLOG(MODULE_SHORT, "AMDRadeonX4000_AMDAccelDevice::getHardwareInfo: return 0x%08X");
    if (ret == 0) {
        SYSLOG(MODULE_SHORT, "getHardwareInfo: deviceId = 0x%x", *hwInfo);
        *hwInfo = kEllesmereDeviceId;
    }
    return ret;
}

#pragma mark - Patches on start/stop

static void pluginStart() {
    LiluAPI::Error error;
    
    DBGLOG(MODULE_SHORT, "start");
    if (getKernelVersion() < KernelVersion::BigSur) {
        error = lilu.onPatcherLoad([](void *user, KernelPatcher &patcher){
            DBGLOG(MODULE_SHORT, "patching cs_validate_range");
            mach_vm_address_t kern = patcher.solveSymbol(KernelPatcher::KernelID, "_cs_validate_range");
            
            if (patcher.getError() == KernelPatcher::Error::NoError) {
                orig_cs_validate = patcher.routeFunctionLong(kern, reinterpret_cast<mach_vm_address_t>(patched_cs_validate_range), true, true);
                
                if (patcher.getError() != KernelPatcher::Error::NoError) {
                    SYSLOG(MODULE_SHORT, "failed to hook _cs_validate_range");
                } else {
                    DBGLOG(MODULE_SHORT, "hooked cs_validate_range");
                }
            } else {
                SYSLOG(MODULE_SHORT, "failed to find _cs_validate_range");
            }
        });
    } else { // >= macOS 11
        error = lilu.onPatcherLoad([](void *user, KernelPatcher &patcher){
            DBGLOG(MODULE_SHORT, "patching cs_validate_page");
            mach_vm_address_t kern = patcher.solveSymbol(KernelPatcher::KernelID, "_cs_validate_page");
            
            if (patcher.getError() == KernelPatcher::Error::NoError) {
                orig_cs_validate = patcher.routeFunctionLong(kern, reinterpret_cast<mach_vm_address_t>(patched_cs_validate_page), true, true);
                
                if (patcher.getError() != KernelPatcher::Error::NoError) {
                    SYSLOG(MODULE_SHORT, "failed to hook _cs_validate_page");
                } else {
                    DBGLOG(MODULE_SHORT, "hooked cs_validate_page");
                }
            } else {
                SYSLOG(MODULE_SHORT, "failed to find _cs_validate_page");
            }
        });
    }
    if (error != LiluAPI::Error::NoError) {
        SYSLOG(MODULE_SHORT, "failed to register onPatcherLoad method: %d", error);
    }
    error = lilu.onKextLoad(kAMDHWLibsInfo, arrsize(kAMDHWLibsInfo), [](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size){
        DBGLOG(MODULE_SHORT, "processing AMDRadeonX4000HWLibs");
        for (size_t i = 0; i < arrsize(kAMDHWLibsInfo); i++) {
            if (i == kAmdRadeonX4000 && kAMDHWLibsInfo[i].loadIndex == index) {
                KernelPatcher::RouteRequest amd_requests[] {
                    KernelPatcher::RouteRequest("__ZN29AMDRadeonX4000_AMDAccelDevice15getHardwareInfoEP24_sAMD_GET_HW_INFO_VALUES", patched_getHardwareInfo, orig_getHardwareInfo),
                };
                if (patcher.routeMultiple(index, amd_requests, address, size, true, true)) {
                    DBGLOG(MODULE_SHORT, "patched getHardwareInfo");
                } else {
                    SYSLOG(MODULE_SHORT, "failed to patch getHardwareInfo: %d", patcher.getError());
                }
            } else if (i == kAmdRadeonX4000HwLibs && kAMDHWLibsInfo[i].loadIndex == index) {
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
    KernelVersion::BigSur,
    pluginStart
};
