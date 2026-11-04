#!/bin/sh
# Assemble build-brick/PS-DEADSTATION1.pak from the Brick build (shell/brick/build.sh), shell/brick/pak and
# pak.json (at the repository's root, where the Pak Store reads it):
# the folder NextUI runs for games in Roms/<name> (PS-DEADSTATION1).
set -e
cd "$(dirname "$0")/../.."
EXE=build-brick/deadstation/core/deadstation/deadstation
PAK=build-brick/PS-DEADSTATION1.pak
[ -x "$EXE" ] || { echo "package: $EXE missing (shell/brick/build.sh)"; exit 2; }
rm -rf "$PAK"
mkdir -p "$PAK/licenses" "$PAK/bios"
cp shell/brick/pak/launch.sh pak.json "$PAK/"
llvm-strip -o "$PAK/deadstation" "$EXE"
chmod +x "$PAK/launch.sh" "$PAK/deadstation"
cp core/COPYING "$PAK/licenses/pcsx_rearmed-GPL-2.0.txt"
# the menu's font, in the binary
cp ui/fonts/NotoSans-OFL.txt "$PAK/licenses/NotoSans-OFL-1.1.txt"
cp ui/LICENSE "$PAK/licenses/deadui-MIT.txt"
echo "package: $PAK ($(du -sh "$PAK" | cut -f1))"
# the release's file (pak.json release_filename): the pak's contents at the zip's root, as the Pak Store
# unpacks it into Emus/tg5040/PS-DEADSTATION1.pak/, in a fixed order
rm -f "$PAK.zip"
python3 - "$PAK" "$PAK.zip" <<'EOF2'
import os, sys, zipfile
src, dst = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, dirs, files in os.walk(src):
        dirs.sort()
        if root != src and not files and not dirs:
            z.write(root, os.path.relpath(root, src) + "/")
        for name in sorted(files):
            path = os.path.join(root, name)
            z.write(path, os.path.relpath(path, src))
EOF2
echo "package: $PAK.zip ($(du -h "$PAK.zip" | cut -f1))"
