#!/bin/sh
# The Rally Cross recomp module, as Deadstation runs it (frontend/recomp.h): its disc as the one argument.
# PSXRecomp's runtime plays it; its frames go to Deadstation's presenter (recomp/brick/host.cpp). Resolution
# (Recomp Options: 320 x 240, 640 x 480 or 1024 x 768) comes from Deadstation's store of the module's settings.
cd "$(dirname "$0")" || exit 1
# Loads as fast as the Brick allows: PSXRecomp's own Fast Loading (host pacing, uncapped) and CD Speed mods, on
# unless the module's mod state already names them
if ! grep -q 'psx.enhancement.fast-loading' mods/state.toml 2>/dev/null; then
	mkdir -p mods
	cat > mods/state.toml <<'STATE'
format_version = 2

[[package]]
id = "psx.enhancement.fast-loading"

[[package]]
id = "psx.enhancement.cd-speed"

[[feature]]
package_id = "psx.enhancement.fast-loading"
id = "fast-loading"
enabled = true
[feature.values]
uncapped = "true"

[[feature]]
package_id = "psx.enhancement.cd-speed"
id = "cd-speed"
enabled = true
[feature.values]
multiplier = "8"
STATE
fi
STORE="${DEADSTATION_HOME:-.}/recomp.cfg"
# Show FPS (Recomp Options): the frames the game drew each second, in the corner as the emulator's "F:"
if grep -q '^rallycross.fps = on$' "$STORE" 2>/dev/null; then
	export DEADSTATION_SHOW_FPS=1
fi
# Resolution: the game's frames drawn by Deadstation's Vulkan (recomp/brick/psx_gpu.cpp) at this size, the display
# scaling it to the panel. 1024 x 768 the default, the panel's own; VRAM at twice the PlayStation's but at 320 x 240.
# (1, as an older setting has it: 640 x 480. A test's DEADSTATION_RESOLUTION comes first.)
case "${DEADSTATION_RESOLUTION:-$(sed -n 's/^rallycross\.scale = //p' "$STORE" 2>/dev/null)}" in
320)
	export DEADSTATION_RESOLUTION=320
	;;
640|1)
	export DEADSTATION_RESOLUTION=640
	export PSX_INTERNAL_RESOLUTION=480
	;;
*)
	export DEADSTATION_RESOLUTION=1024
	export PSX_INTERNAL_RESOLUTION=480
	;;
esac
# the disc, for Deadstation's menu in the module (recomp/brick/module_menu.cpp)
export DEADSTATION_DISC="$1"
./Rally_Cross_recomp_module --no-launcher --renderer vulkan --game game.toml --disc "$1"
code=$?
# a run that ended as it should: the module runs again next time (frontend/recomp.h runEnded)
[ "$code" = 0 ] && rm -f /tmp/deadstation-recomp-run
exit $code
