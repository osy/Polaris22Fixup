//
//  kern_start.cpp
//  Polaris22Fixup
//
//  Copyright © 2019 osy86. All rights reserved.
//

#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>

#define MODULE_SHORT "p22"

// Paths
static const char binPathMetalBronzeDriver[] = "/System/Library/Extensions/AMDMTLBronzeDriver.bundle/Contents/MacOS/AMDMTLBronzeDriver";
static const char binDyldCache[] = "/private/var/db/dyld/dyld_shared_cache_x86_64h"; // FIXME: better way of patching dyld cache!

static const uint32_t SectionActive = 1;

static const uint8_t amdBronzeMtlAddrLibGetBaseArrayMode_ret_original[] = {
    0xb8, 0x02, 0x00, 0x00, 0x00, 0x0f, 0x43, 0xc1, 0xeb, 0x0f,
};

static const uint8_t amdBronzeMtlAddrLibGetBaseArrayMode_ret_patched[] = {
    0xb8, 0x02, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90, 0xeb, 0x0f, 
};

static const UserPatcher::BinaryModPatch patchAmdBronzeMtlAddrLibGetBaseArrayMode {
    CPU_TYPE_X86_64,
    amdBronzeMtlAddrLibGetBaseArrayMode_ret_original,
    amdBronzeMtlAddrLibGetBaseArrayMode_ret_patched,
    sizeof(amdBronzeMtlAddrLibGetBaseArrayMode_ret_original),
    0,
    1,
    UserPatcher::FileSegment::SegmentTextText,
    SectionActive
};

// All patches for binaries

static UserPatcher::BinaryModPatch allPatches[] = {
    patchAmdBronzeMtlAddrLibGetBaseArrayMode,
};

static UserPatcher::BinaryModInfo binaryPatches[] {
    { binPathMetalBronzeDriver, allPatches, arrsize(allPatches) },
    { binDyldCache, allPatches, arrsize(allPatches) },
};

// main function
static void pluginStart() {
    DBGLOG(MODULE_SHORT, "start");
    lilu.onProcLoadForce(nullptr, 0, nullptr, nullptr, binaryPatches, arrsize(binaryPatches));
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
    KernelVersion::Mojave,
    pluginStart
};
