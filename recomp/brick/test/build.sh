#!/bin/sh
# Builds recomp/brick/test/scene_light_test for this PC (the Vulkan headers the Brick's build uses).
set -e
root="$(cd "$(dirname "$0")/../../.." && pwd)"
out="$root/build-brick/scene-light-test"
mkdir -p "$out/shaders"
for s in "$root"/core/deadstation/shaders/frame.vert "$root"/core/deadstation/shaders/frame.frag \
		"$root"/recomp/brick/shaders/scene_shadow.vert "$root"/recomp/brick/shaders/scene_flash.vert "$root"/recomp/brick/shaders/scene_light.vert "$root"/recomp/brick/shaders/scene_light.frag; do
	name="$(basename "$s" | tr . _)"
	glslangValidator -V --quiet -I"$root/recomp/brick/shaders" --vn "$name" -o "$out/shaders/$name.h" "$s"
done
c++ -std=c++17 -O1 -g -Wall -I"$root/build-brick/include" -I"$root/core/deadstation" -I"$root/recomp/brick" -I"$out/shaders" \
	"$root/recomp/brick/test/scene_light_test.cpp" "$root/recomp/brick/scene_light.cpp" -ldl -o "$out/scene_light_test"
echo "scene-light-test: $out/scene_light_test"
