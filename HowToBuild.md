# How to build

1. Download git submodules:
  ```git submodule update --init```

1. Download a [DEBUG version of the Lulu kext](https://github.com/acidanthera/lilu/releases), and place in the repository root folder under the name `Lulu.kext`.  You do **not** need the Apple Kernel Debug Kit (KDK) to build this kext.

1. Open the Xcode project, select the x86 build, and build for release.
