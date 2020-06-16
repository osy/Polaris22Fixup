Polaris22 Driver Fixup
======================
This KEXT fixes known graphics issues with Polaris22/VegaM on OSX. Only tested/supported on 10.14+.

* Framebuffer corruption due to Metal driver bug. The details of the issue and the fix can be found [here](https://osy.gitbook.io/hac-mini-guide/details/metal-driver-fix).
* On 10.14.5 Beta 2 and newer versions, boot fails with accelerator enabled and ends in a black screen.

## Installation

* You need Lilu 1.3.8 or newer as older versions had incompatibilities
* Install the KEXT along with Lilu

## Implementation

### Metal Bug

Userland code injection on 10.14+ is tricky because of various security features designed to prevent malware from taking over system applications. See [this article](https://knight.sc/malware/2019/03/15/code-injection-on-macos.html) for reasons why various older techniques no longer work.

We do not have to worry about permissions because we have kernel access, but finding *when* a particular piece of user code is loaded is tricky. OSX does not page in the code until it is needed, so we cannot hook `mmap` or related functions. Instead we hook `cs_validate_range`. This function is called after the code is paged in, in order to check the code signature. We put our hook after this call so we can modify the code after the signature check passes. The advantage of placing our hook here is that we do not cause needless paging which reduces performance.

In the hook, we check that the file getting paged in is either `AMDMTLBronzeDriver` or the shared cache `dyld_shared_cache_x86_64h` which contains every platform binary on the system. Once that matches, we do a pattern search for the function to patch and fix it.

### SMU Loading Bug

`Polaris22_UploadSMUFirmwareImageDefault` calls`PECI_IsEarlySAMUInitEnabled` to check if SMU firmware can be loaded directly. `PECI_IsEarlySAMUInitEnabled` looks at bit 0x160 of `CAIL_DDI_CAPS_POLARIS22_A0` which should be 0. But it is 1, leading the firmware to not be loaded. Patching the function to return 0 will fix it.

Before 10.14.5 Beta 2, it worked by chance. `AtiAppleCailServices::isAsicCapEnabled` did not have `CAIL_DDI_CAPS_POLARIS22_A0` and defaulted all bits to 0.

## Performance

Because we are hooking on a hot piece of code (code signature checking), we need to make sure we are not adversely impacting performance. The two operations we add are a string compare for the path name (as well as potentially needing to generate the path name) as well as finding the pattern to patch (only if the path name matches first).

Although both operations can be costly, we point out that the path name is at most 1024 bytes which is 1/4 of the minimum size of data (4096) that code signature is checked on. In practice, the path is likely much shorter. In terms of the actual pattern search, we note that right before our hook is called, a SHA hash is computed on the same data. This means the data is already local (in cache) and we have already done Θ(n) work (n bytes). We use a O(n) optimized `memmem` implementation from musl for the pattern search.
