#!/bin/sh
# The Tekken 3 recomp module, as Deadstation runs it (frontend/recomp.h): its disc as the one argument.
# PSXRecomp's runtime plays it; its frames go to Deadstation's presenter (recomp/brick/host.cpp). Resolution
# (Recomp Options: Original or 1024 x 768) and Lighting come from Deadstation's store of the module's settings.
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
if grep -q '^tekken3.fps = on$' "$STORE" 2>/dev/null; then
	export DEADSTATION_SHOW_FPS=1
fi
# 1024 x 768 (the default; Original only when chosen): the game's frames drawn by Deadstation's Vulkan
# (recomp/brick/psx_gpu.cpp) at the panel's own size, VRAM at twice the PlayStation's
if ! grep -q '^tekken3.scale = 1$' "$STORE" 2>/dev/null; then
	export DEADSTATION_RESOLUTION=1024
	export PSX_INTERNAL_RESOLUTION=480
fi
# The fighters' 3D layer (src/plugin.cpp), drawn by Deadstation's presenter: their shadows on the floor are always its
export DEADSTATION_TEKKEN3_SCENE=1 DEADSTATION_SCENE_LIGHT=1
# Lighting: the game's own instead of Deadstation's (self-shadowing, the sparks' light)
if grep -q '^tekken3.lighting = original$' "$STORE" 2>/dev/null; then
	export DEADSTATION_TEKKEN3_LIGHTING=original
fi
# Unlock Everything, Fight GUI, and Spark Lighting, 0-10 (the plugin applies them as they change in Recomp Options too)
if grep -q '^tekken3.unlock = on$' "$STORE" 2>/dev/null; then
	export DEADSTATION_TEKKEN3_UNLOCK=1
fi
if grep -q '^tekken3.gui = hide$' "$STORE" 2>/dev/null; then
	export DEADSTATION_TEKKEN3_GUI=hide
fi
SPARKS=$(sed -n 's/^tekken3.sparks = //p' "$STORE" 2>/dev/null)
[ -n "$SPARKS" ] && export DEADSTATION_TEKKEN3_SPARKS="$SPARKS"
# the disc, for Deadstation's menu in the module (recomp/brick/module_menu.cpp)
export DEADSTATION_DISC="$1"
./Tekken_3_recomp_module --no-launcher --renderer vulkan --game game.toml --disc "$1"
code=$?
# a run that ended as it should: the module runs again next time (frontend/recomp.h runEnded)
[ "$code" = 0 ] && rm -f /tmp/deadstation-recomp-run
exit $code
