# Cross-compilation toolchain for 32-bit ARM Linux (armhf) -- the family
# covering most home-router/IoT-gateway SoCs (e.g. Broadcom/MediaTek ARM
# boards running a Linux-based OpenWrt-style firmware). This is a build
# (not run) sanity check: it proves the codebase has no hidden
# x86-only assumptions, not that it's been validated on real hardware.
#
# Usage:
#   cmake -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-linux-gnueabihf.cmake
#   cmake --build build-arm

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

# Resulting binaries are armhf executables; the host can't run them
# directly (qemu-user or real hardware/a target runner is required),
# so TRY_COMPILE must not attempt to execute a test binary.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
