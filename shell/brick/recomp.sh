#!/bin/sh
# Build a recomp module (recomp/modules/MODULE) for NextUI's tg5040 handhelds from the builder's own disc, into
# build-brick/recomp/MODULE/: what Deadstation keeps in <DEADSTATION_HOME>/recomp/MODULE/ (frontend/recomp.h):
# run.sh and the runtime, module.cfg, game.toml, the BIOS PSXRecomp boots (OpenBIOS) and the licences.
#   shell/brick/recomp.sh tekken3 "/path/to/Tekken 3 (USA).chd"
# Every step runs without network (recomp/sbx). PSXRecomp (recomp/psxrecomp) is brought to its audited pin and
# patches first (recomp/prepare-psxrecomp.sh). From the disc come recomp/modules/MODULE/disc (the staged disc),
# generated/ (the game's C) and, if the module has captures, overlays/ baked into generated/: never in git.
# Needs, besides Deadstation's own (shell/brick/build.sh; the device's libcurl for the menu's downloads: devlibs.sh):
# gcc, Python 3, bubblewrap and the host's SDL3
# (the translator and the overlay bake run on this machine).
set -e
cd "$(dirname "$0")/../.."
ROOT="$PWD"
MODULE=${1:?usage: shell/brick/recomp.sh MODULE DISC.chd}
CHD=${2:?usage: shell/brick/recomp.sh MODULE DISC.chd}
M="recomp/modules/$MODULE"
[ -f "$M/game.toml" ] || { echo "recomp: no $M/game.toml"; exit 2; }
TC="$ROOT/build-brick/toolchain"
[ -d "$TC/aarch64-nextui-linux-gnu/libc" ] || { echo "recomp: no toolchain (shell/brick/toolchain.sh)"; exit 2; }
SBX="$ROOT/recomp/sbx"
JOBS=$(nproc)

recomp/prepare-psxrecomp.sh > /dev/null

# the translator (and libchdr), for this machine
R="$ROOT/build-brick/recomp-host/recompiler"
if [ ! -x "$R/psxrecomp-game" ]; then
    $SBX cmake -S recomp/psxrecomp/recompiler -B "$R" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSXRECOMP_ENABLE_CHD=ON > "$R.configure.log"
fi
$SBX cmake --build "$R" -j"$JOBS" > "$R.build.log"

# the disc, staged from the CHD (cue + bins), and its boot EXE
CUE=$(sed -n 's/^disc = "\(.*\)"$/\1/p' "$M/game.toml")
if [ ! -f "$M/$CUE" ]; then
    mkdir -p "$M/disc"
    $SBX python3 recomp/psxrecomp/tools/psx_chd.py "$CHD" --lib "$R/libchdr.so" --extract "$M/disc" \
        --cue-name "$(basename "$CUE")" > /dev/null
    (cd "$M" && $SBX python3 "$ROOT/recomp/psxrecomp/tools/new_project_layout/probe_disc.py" "$CUE" \
        --write-boot-exe disc --out-dir disc --disc-rel "$CUE" > /dev/null)
fi

# the game's C
(cd "$M" && $SBX "$R/psxrecomp-game" --config "$PWD/game.toml" > "$ROOT/build-brick/recomp-host/$MODULE-generate.log")

# the captured overlays, baked into native code (the bake checks the translator against a codegen hash a
# configure for this machine writes)
H="$ROOT/build-brick/recomp-host/$MODULE"
if [ -f "$M/overlays/overlay_captures.json" ]; then
    $SBX cmake -S "$M" -B "$H" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSX_DEBUG_TOOLS=ON -DPSX_RECOMP_UI=OFF \
        -DPSX_NETPLAY=OFF -DPSX_REWIND=OFF -DPSX_ENABLE_VULKAN=OFF -DPSX_SDL3_FETCH=OFF -DPSX_ZLIB_FETCH=OFF \
        -DPSXRECOMP_BIOS_STEMS=OpenBIOS > "$H.configure.log"
    $SBX cmake --build "$H" --target psxrecomp_codegen_hash > /dev/null
    (cd "$M" && $SBX python3 "$ROOT/recomp/psxrecomp/tools/compile_overlays.py" --static \
        --captures overlays/overlay_captures.json --game-toml game.toml --recompiler "$R/psxrecomp-game" \
        --runtime-include "$ROOT/recomp/psxrecomp/runtime/include" --out-dir generated \
        --gcc "$(command -v gcc)" --cps > "$ROOT/build-brick/recomp-host/$MODULE-overlays.log")
fi

# the module, for the Brick: PSXRecomp's SDL2 backend (the sysroot's), CHD discs, Deadstation's presenter
# (recomp/brick/brick.cmake); pkg-config sees the sysroot's packages only
B="$ROOT/build-brick/recomp-brick/$MODULE"
SYSROOT="$TC/aarch64-nextui-linux-gnu/libc"
mkdir -p build-brick/include
rm -rf build-brick/include/vulkan build-brick/include/vk_video build-brick/include/curl
cp -r /usr/include/vulkan /usr/include/vk_video /usr/include/curl build-brick/include/
PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig" PKG_CONFIG_SYSROOT_DIR="$SYSROOT" \
    $SBX cmake -S "$M" -B "$B" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$ROOT/shell/brick/tg5040.cmake" \
    -DDEADSTATION_TG5040_ROOT="$TC" -DCMAKE_BUILD_TYPE=Release -DPSX_SDL_BACKEND=SDL2 -DPSX_RECOMP_UI=OFF \
    -DPSX_NETPLAY=OFF -DPSX_REWIND=OFF -DPSX_ENABLE_VULKAN=OFF -DPSX_ZLIB_FETCH=OFF -DPSX_DEBUG_TOOLS=OFF \
    -DPSX_STATIC_RUNTIME=OFF -DPSXRECOMP_ENABLE_CHD=ON -DPSXRECOMP_BIOS_STEMS=OpenBIOS \
    -DDEADSTATION_VULKAN_INCLUDE="$ROOT/build-brick/include" -DDEADSTATION_CURL_INCLUDE="$ROOT/build-brick/include" \
    -DDEADSTATION_CURL_LIBRARY="$ROOT/build-brick/devlibs/libcurl.so.4" > "$B.configure.log"
$SBX cmake --build "$B" --target psxrecomp_codegen_hash > /dev/null
$SBX cmake --build "$B" --target psx-runtime -j"$JOBS" > "$B.build.log"
EXE=$(cat "$B/psxrecomp_exe_name-psx-runtime.txt")

# the module's folder
OUT="build-brick/recomp/$MODULE"
rm -rf "$OUT"
mkdir -p "$OUT/bios" "$OUT/licenses"
llvm-strip -o "$OUT/$EXE" "$B/$EXE"
cp "$M/run.sh" "$M/module.cfg" "$M/game.toml" "$OUT/"
# the pipelines it draws, built ahead of use from its first run on (recomp/brick/psx_gpu.cpp)
[ -f "$M/pipelines.keys" ] && cp "$M/pipelines.keys" "$OUT/"
[ -f "$M/game_options.toml" ] && cp "$M/game_options.toml" "$OUT/"
# the mod catalog the build staged (the module's own package, PSXRecomp's Fast Loading and CD Speed, ...): the
# runtime finds its mods there
[ -d "$B/mods/bundled" ] && mkdir -p "$OUT/mods" && cp -r "$B/mods/bundled" "$OUT/mods/"
cp recomp/psxrecomp/bios/openbios.bin "$OUT/bios/"
cp recomp/psxrecomp/bios/OpenBIOS.LICENSE "$OUT/licenses/OpenBIOS.LICENSE"
cp recomp/psxrecomp/LICENSE "$OUT/licenses/PSXRecomp-PolyForm-Noncommercial-1.0.0.txt"
cp recomp/psxrecomp/THIRD_PARTY_ATTRIBUTION.md "$OUT/licenses/PSXRecomp-THIRD_PARTY_ATTRIBUTION.md"
chmod +x "$OUT/run.sh" "$OUT/$EXE"
echo "recomp: $OUT ($(du -sh "$OUT" | cut -f1))"
