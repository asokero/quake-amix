#!/bin/sh
# emu-quake.sh -- boot the Quake test guest under Amiberry.
#
# This deliberately does NOT use the kernel project's emu-reset-boot.sh.  That
# script stages whatever is in kernelsupport/build/unix-040-dbg and restores
# kernelsupport's golden image, so using it means writing into the kernel
# project's working files -- which is how a benchmark run can quietly clobber
# somebody's kernel build.  Everything here belongs to this repository:
#
#   config    ~/Amiberry/Configurations/a3000ux060-amixquake.uae
#   kernel    emu/kernel/unix-040-dbg   (a copy; the guest loads it by that name)
#   disk      amix_hardfileX11R5-quake.hdf, restored from -quake-golden.hdf
#   serial    TCP 1236          (kernel work uses 1234, wolf3d 1235)
#   telnet    localhost 2325    (kernel work uses 2323, wolf3d 2324)
#
# The only file shared with the kernel project is the AmigaOS boot volume
# HD0-MAIN1-060-os32.hdf, which its own three configs already share.
#
# usage:
#   sh tools/emu-quake.sh              boot from the golden image (wipes the guest)
#   sh tools/emu-quake.sh keep         boot the existing disk, keeping its contents
#   sh tools/emu-quake.sh snapshot     copy the current disk over the golden
#
# "snapshot" is how the 18.7 MB pak0.pak stops having to be transferred: get the
# game data in once, snapshot, and every later boot starts with it present.
# Shut the guest down cleanly first (sync; sync; haltsys) or the snapshot
# captures a dirty filesystem.

set -e

# Paths.  The defaults are one working setup; override any of them in the
# environment rather than editing this file -- the hard-drive directory in
# particular follows the locale's "Documents" name and will not match yours.
#
#   AMIBERRY_HD    directory holding the .hdf images
#   AMIBERRY_CONF  the .uae configuration for this project's guest
#   AMIBERRY_BIN   the amiberry executable
HD="${AMIBERRY_HD:-$HOME/Asiakirjat/FS-UAE/Hard Drives}"
GOLDEN="$HD/amix_hardfileX11R5-quake-golden.hdf"
DISK="$HD/amix_hardfileX11R5-quake.hdf"
CONF="${AMIBERRY_CONF:-$HOME/Amiberry/Configurations/a3000ux060-amixquake.uae}"
AMIBERRY="${AMIBERRY_BIN:-$HOME/kehitys/amiberry/build/amiberry}"
LOG="${2:-/tmp/quake-emu.log}"
MODE="${1:-reset}"

test -f "$GOLDEN"   || { echo "ERROR: golden image missing: $GOLDEN"; exit 1; }
test -f "$CONF"     || { echo "ERROR: config missing: $CONF"; exit 1; }
test -x "$AMIBERRY" || { echo "ERROR: amiberry missing: $AMIBERRY"; exit 1; }

if [ "$MODE" = snapshot ]; then
	test -f "$DISK" || { echo "ERROR: no working disk to snapshot"; exit 1; }
	cp --sparse=always "$DISK" "$GOLDEN.new"
	mv "$GOLDEN.new" "$GOLDEN"
	echo "[*] snapshot taken: $(du -h "$GOLDEN" | awk '{print $1}') on disk"
	exit 0
fi

# Only this config's Amiberry is stopped, matched on the config filename, so a
# kernel-project instance on 1234 keeps running.
if pgrep -f "a3000ux060-amixquake" > /dev/null 2>&1; then
	echo "[*] stopping the running Quake guest"
	pkill -f "a3000ux060-amixquake" || true
	sleep 2
fi
if pgrep -f "nc localhost 1236" > /dev/null 2>&1; then
	pkill -f "nc localhost 1236" || true
	sleep 1
fi

if [ "$MODE" = keep ]; then
	test -f "$DISK" || { echo "ERROR: no working disk; boot without 'keep' first"; exit 1; }
	echo "[*] keeping the existing disk"
else
	echo "[*] restoring golden image -> $(basename "$DISK")"
	cp --sparse=always "$GOLDEN" "$DISK"
fi

: > "$LOG"
setsid sh -c "while :; do nc localhost 1236 >> '$LOG' 2>/dev/null; sleep 1; done" > /dev/null 2>&1 &
echo "[*] serial capture -> $LOG"

DISPLAY="${DISPLAY:-:1}" setsid "$AMIBERRY" -f "$CONF" -G -D > /dev/null 2>&1 &
echo "[*] amiberry started (config $(basename "$CONF"))"
echo ""
echo "    The login prompt goes to the Amiga console, never to the serial log."
echo "    Log in and prime slirp from the console:"
echo "      python3 \$KERNELSUPPORT/test-tools/sendkeys.py 'root' RET"
echo "      python3 \$KERNELSUPPORT/test-tools/sendkeys.py 'ping 10.0.2.2' RET"
echo "    then:  AMIX_PORT=2325 python3 tools/amixsh.py 'uname -a'"
