#!/bin/sh
# Cross-build Deadstation for NextUI's tg5040 handhelds (TrimUI Brick, aarch64) into build-brick/deadstation.
# Needs:
#  - the toolchain (shell/brick/toolchain.sh)
#  - the host's Vulkan headers (vulkan-headers; the sysroot has none) and glslangValidator (glslang),
#    which compiles the shaders to SPIR-V
#  - the device's libcurl.so.4 (shell/brick/devlibs.sh SERIAL), to link against, and the host's curl
#    headers (curl); the sysroot has neither
#  - clang, lld, cmake and ninja on the host
# Arguments go to ninja.
set -e
cd "$(dirname "$0")/../.."
B=build-brick/deadstation
TC="$PWD/build-brick/toolchain"
[ -d "$TC/aarch64-nextui-linux-gnu/libc" ] || { echo "brick: no toolchain (shell/brick/toolchain.sh)"; exit 2; }
mkdir -p build-brick/include
CURLLIB="$PWD/build-brick/devlibs/libcurl.so.4"
[ -f "$CURLLIB" ] || { echo "brick: no $CURLLIB (shell/brick/devlibs.sh SERIAL)"; exit 2; }
rm -rf build-brick/include/vulkan build-brick/include/vk_video build-brick/include/curl
cp -r /usr/include/vulkan /usr/include/vk_video /usr/include/curl build-brick/include/
# pkg-config sees the sysroot's packages only, never the host's
SYSROOT="$TC/aarch64-nextui-linux-gnu/libc"
export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
unset PKG_CONFIG_PATH
mkdir -p "$B"
cmake -S . -B "$B" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$PWD/shell/brick/tg5040.cmake" \
    -DDEADSTATION_TG5040_ROOT="$TC" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SKIP_RPATH=ON \
    -DDEADSTATION_VULKAN_INCLUDE="$PWD/build-brick/include" \
    -DSDL2_DIR="$PWD/shell/brick/cmake" \
    -DDEADSTATION_CURL_INCLUDE="$PWD/build-brick/include" \
    -DDEADSTATION_CURL_LIBRARY="$CURLLIB" > "$B.configure.log"
ninja -C "$B" "$@" deadstation
echo "brick: built $B/core/deadstation/deadstation"
