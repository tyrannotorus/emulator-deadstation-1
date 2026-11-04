#!/bin/sh
# Deadstation 1 for NextUI on the TrimUI Brick (tg5040). NextUI passes the game picked in Roms: a disc
# (.chd, .cue, .pbp) or an .m3u naming its discs.
PAK_DIR="$(cd "$(dirname "$0")" && pwd)"
PAK_NAME="$(basename "$PAK_DIR")"
PAK_NAME="${PAK_NAME%.*}"

rm -f "$LOGS_PATH/$PAK_NAME.txt"
exec >>"$LOGS_PATH/$PAK_NAME.txt" 2>&1
echo "$0" "$@"
cd "$PAK_DIR" || exit 1

ROM="$1"
if [ ! -f "$ROM" ]; then
	echo "Deadstation: no game at $ROM"
	exit 1
fi

# Memory cards are kept in DEADSTATION_HOME. The BIOS is read from the pak's own bios/ when it holds
# one, else from the firmware's Bios/PS; without either the core's own BIOS runs.
export DEADSTATION_HOME="$USERDATA_PATH/$PAK_NAME"
export HOME="$DEADSTATION_HOME"
mkdir -p "$DEADSTATION_HOME" "$PAK_DIR/bios"
if [ -n "$(ls -A "$PAK_DIR/bios" 2>/dev/null)" ]; then
	export DEADSTATION_BIOS="$PAK_DIR/bios"
else
	export DEADSTATION_BIOS="$BIOS_PATH/PS"
fi

# For the menu: the game's name
export DEADSTATION_ROM="$ROM"

cleanup() {
	rm -f /tmp/stay_awake
	sync
}
trap cleanup EXIT INT TERM HUP QUIT

# no auto sleep while playing
echo 1 >/tmp/stay_awake
# NextUI starts paks at its performance clock; run at its Auto, as its own emulators do
sh "$SYSTEM_PATH/bin/governor.sh" auto

# exit 86: the game's other core chosen in Options (Emulator or its recomp module): started again,
# deadstation hosting the module when that is the core (ui/src/host.h); a few times at most
for run in 1 2 3 4 5; do
	"$PAK_DIR/deadstation" "$ROM"
	code=$?
	echo "Deadstation: deadstation exited with $code"
	[ "$code" = 86 ] || break
done
# NextUI's display mode again, should deadstation have been killed before restoring it
command -v fbset > /dev/null && fbset -g 1024 768 1024 16384 32
# and the USB ports as they were, should it have been killed with its USB link up (core/deadstation/link.cpp):
# ADB alone on the bottom port, and the port a device again if deadstation made it a host
G=/sys/kernel/config/usb_gadget/g1
if [ -e "$G/configs/c.1/ffs.deadstation" ]; then
	echo >"$G/UDC"
	rm -f "$G/configs/c.1/ffs.deadstation"
	umount /dev/usb-ffs/deadstation
	rmdir /dev/usb-ffs/deadstation "$G/functions/ffs.deadstation"
	sleep 0.3
	[ -n "$(cat "$G/UDC")" ] || ls /sys/class/udc >"$G/UDC"
fi
if [ -f /tmp/deadstation-link-host ]; then
	# reading this file is what switches the port (Allwinner's driver)
	cat /sys/devices/platform/soc/usbc0/usb_device >/dev/null
	rm -f /tmp/deadstation-link-host
fi
# and the top port's 5 V, off while a Brick there ran on its own battery
if [ -f /tmp/deadstation-link-top-off ]; then
	D=/sys/kernel/debug/sunxi_pinctrl
	echo r_pio >$D/dev_name
	echo PL8 >$D/sunxi_pin
	echo 'PL8 1' >$D/data
	rm -f /tmp/deadstation-link-top-off
fi
