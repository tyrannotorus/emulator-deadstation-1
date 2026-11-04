#!/bin/sh
# Libraries the Brick has and the toolchain's sysroot has not, pulled over adb into
# build-brick/devlibs so the Brick build can link against them. Never shipped: the device has them.
#  - libcurl.so.4 (/usr/lib): the recomp modules' downloads; its headers come from the host (build.sh)
#   shell/brick/devlibs.sh SERIAL        (adb devices -l)
set -e
cd "$(dirname "$0")/../.."
SERIAL=${1:?usage: shell/brick/devlibs.sh SERIAL}
mkdir -p build-brick/devlibs
for lib in libcurl.so.4; do
    adb -s "$SERIAL" pull /usr/lib/$lib build-brick/devlibs/$lib > /dev/null
    echo "devlibs: build-brick/devlibs/$lib ($(wc -c < build-brick/devlibs/$lib) bytes)"
done
