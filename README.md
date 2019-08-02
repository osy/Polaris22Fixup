Polaris22 Metal Driver Fixup
============================
This kext fixes known graphics issues with Polaris22/VegaM on OSX. Only tested/supported on 10.14+. The details of the issue and the fix can be found [here](https://osy.gitbook.io/hac-mini-guide/details/metal-driver-fix).

*Note that this plugin is currently incompatible with any Lilu plugin that uses user patches!*

## Installation

Copy `Polaris22MetalFixup.kext` to either your `EFI/CLOVER/kexts/Other` directory in your EFI partition or to `/Library/Extensions` on your OSX partition (with SIP disabled).

## Implementation

Userland code injection on 10.14+ is tricky because of various security features designed to prevent malware from taking over system applications. See [this article](https://knight.sc/malware/2019/03/15/code-injection-on-macos.html) for reasons why various older techniques no longer work.

We do not have to worry about permissions because we have kernel access, but finding *when* a particular piece of user code is loaded is tricky. OSX does not page in the code until it is needed, so we cannot hook `mmap` or related functions. Instead we hook `cs_validate_range`. This function is called after the code is paged in, in order to check the code signature. We put our hook after this call so we can modify the code after the signature check passes. The advantage of placing our hook here is that we do not cause needless paging which reduces performance.

Since `cs_validate_range` is not exported by any KPI, we have to manually generate a proxy KEXT [using this tool](https://github.com/slavaim/dl_kextsymboltool) in order to import it.

In the hook, we check that the file getting paged in is either `AMDMTLBronzeDriver` or the shared cache `dyld_shared_cache_x86_64h` which contains every platform binary on the system. Once that matches, we do a pattern search for the function to patch and fix it.

### Boot Args

The default behaviour is to stop patching after the first success (and unload the kernel patches). In theory this is not enough because memory can be paged under high memory load and have to be re-read from the file. However, it would be rare for the userland GPU driver will ever get marked as unused (and get paged).

Nevertheless, the boot-arg `-p22fixupmultiple` is provided to always scan for the pattern to patch, even after first success.

## Performance

Because we are hooking on a hot piece of code (code signature checking), we need to make sure we are not adversely impacting performance. The two operations we add are a string compare for the path name (as well as potentially needing to generate the path name) as well as finding the pattern to patch (only if the path name matches first).

Although both operations can be costly, we point out that the path name is at most 1024 bytes which is 1/4 of the minimum size of data (4096) that code signature is checked on. In practice, the path is likely much shorter. In terms of the actual pattern search, we note that right before our hook is called, a SHA hash is computed on the same data. This means the data is already local (in cache) and we have already done Θ(n) work (n bytes). We use a O(n) optimized `memmem` implementation from musl for the pattern search.

### Why not Lilu?

One disadvantage of our implementation is that we have to hard code the number of bytes to patch for the trampoline. In theory, if the code changes enough in a future version, this would break. Lilu would work around this problem but Lilu is way too powerful of a tool for this simple patch. Additionally, there are [issues with dyld patching](https://github.com/acidanthera/bugtracker/issues/390) that makes it impossible to use. The pattern search in Lilu is also slower and prone to bugs. Finally, Lilu requires changes for every major OSX update that makes maintaining it a hassle. As of Lilu 1.3.8, Lilu should play well with the custom patcher in this project assuming Lilu is loaded AFTER. If have a version of Lilu before that, you may run into issues. If you load Lilu first, then the patches will gracefully fail.
