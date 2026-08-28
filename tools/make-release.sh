#!/bin/sh
# make-release.sh -- build and package the binaries for a GitHub release.
#
# Cross-built on Linux, unlike the sibling projects' release scripts which run
# on AMIX.  Needs both toolchains: m68k-cbm-sysv4 (gcc 2.7.2.3) for the four
# supported binaries, and Debian's m68k-linux-gnu (gcc 14) for the two faster
# experimental ones.
#
#   sh tools/make-release.sh [version]      -> build/quake-amix-<version>.tar
#
# Every binary is verified after linking, not merely built:  a build named
# -68040 that actually contains 68020 code is the specific mistake this
# repository has already made, and it cost a factor of 3.6.

set -e

VERSION="${1:-`date '+%Y%m%d'`}"
PKG="quake-amix-$VERSION"
STAGE="build/$PKG"
TAR="build/$PKG.tar"

export PATH="$HOME/opt/amix-cross/bin:$PATH"
command -v m68k-cbm-sysv4-gcc >/dev/null || { echo "ERROR: m68k-cbm-sysv4-gcc not on PATH"; exit 1; }
command -v m68k-linux-gnu-gcc >/dev/null || { echo "ERROR: m68k-linux-gnu-gcc not found (apt install gcc-m68k-linux-gnu)"; exit 1; }

echo "[*] building $PKG"
rm -rf obj-* build/quake-* "$STAGE" "$TAR"
mkdir -p "$STAGE"

# ---- supported builds, gcc 2.7.2.3 -----------------------------------------
for A in 68040 68020; do
	for V in null va2000; do
		echo "    make ARCH=$A VIDEO=$V"
		make ARCH=$A VIDEO=$V >/dev/null
		cp "build/quake-$V-$A" "$STAGE/"
	done
done

# ---- experimental builds, gcc 14 -------------------------------------------
for V in null va2000; do
	echo "    build-gcc14.sh VIDEO=$V"
	OBJ="obj-gcc14-$V" VIDEO="$V" OUT="$STAGE/quake-$V-gcc14" sh tools/build-gcc14.sh >/dev/null
done

# ---- verify ----------------------------------------------------------------
# The point of these checks is that they can fail.  A -68040 binary must have
# no FSGLMUL (the 68060 emulates it, at 3.6x the cost); a -68020 binary must
# have some, or the ARCH flag did not reach the compiler.  The gcc 14 builds
# must be static -- a dynamic one links fine and segfaults in AMIX's 1992 ld.so.
echo "[*] verifying"
fail=0
for f in "$STAGE"/quake-*; do
	b=`basename "$f"`
	n=`m68k-linux-gnu-objdump -d "$f" 2>/dev/null | grep -cE '\bfsgl(mul|div)' || true`
	case "$b" in
		*-68020) test "$n" -gt 0 || { echo "    FAIL $b: no FSGLMUL, ARCH flag did not apply"; fail=1; } ;;
		*)       test "$n" -eq 0 || { echo "    FAIL $b: $n FSGLMUL, emulated on 68060"; fail=1; } ;;
	esac
	case "$b" in
		*gcc14) file "$f" | grep -q "statically linked" || { echo "    FAIL $b: not static"; fail=1; }
		        m68k-linux-gnu-readelf -l "$f" | grep -q GNU_STACK && { echo "    FAIL $b: PT_GNU_STACK present"; fail=1; } ;;
	esac
	file "$f" | grep -q "ELF 32-bit MSB executable" || { echo "    FAIL $b: not an m68k SVR4 executable"; fail=1; }
	printf "    ok   %-24s %8s bytes  fsglmul=%s\n" "$b" "`wc -c < "$f"`" "$n"
done
test "$fail" -eq 0 || { echo "[!] verification failed, no archive written"; exit 1; }

# ---- stage the rest --------------------------------------------------------
cp LICENSE "$STAGE/"
cat > "$STAGE/autoexec.cfg" <<'CFG'
// Quake cannot see Ctrl, Shift or Alt through a tty, so the default +attack
// bind is dead.  Put this file in /quake/id1/ before the first run.
bind SPACE +attack
bind j +jump
in_keyhold 400
CFG

cat > "$STAGE/README.txt" <<TXT
Quake for AMIX -- $VERSION
==========================

Binaries for Commodore Amiga UNIX (AMIX) SVR4 on 68k.  Source, documentation
and the licence terms: https://github.com/asokero/quake-amix

WHICH ONE
---------
  quake-va2000-68040   the game.  68040/68060.  Start here.
  quake-null-68040     benchmark: renders to memory, shows nothing.
  quake-va2000-68020   the game on a 68030.  Three times slower on an 060.
  quake-null-68020     benchmark, same.

  quake-va2000-gcc14   EXPERIMENTAL, and faster: 8.7 fps against 7.5.  Built
  quake-null-gcc14     with GCC 14 rather than the period-correct GCC 2.7.2.3,
                       statically linked.  Plays correctly; less proven.

REQUIREMENTS
------------
  * AMIX 2.1p2a with the 68040/68060 kernel port
  * an MNT VA2000 with the va2000-amix driver, for the va2000 builds
  * root (the framebuffer mmap needs it)
  * about 8 MB free
  * Quake game data -- id1/pak0.pak.  NOT INCLUDED.  Shareware is enough.

INSTALL
-------
  mkdir -p /quake/id1
  cp quake-va2000-68040 /quake/quake
  cp autoexec.cfg       /quake/id1/
  cp pak0.pak           /quake/id1/
  chmod +x /quake/quake
  cd /quake && ./quake

Copy autoexec.cfg before the first run.  A tty cannot see Ctrl, so without it
you cannot shoot.  Quit with ESC -> Quit -> y; the function keys are not mapped.

To benchmark instead:  ./quake-null-68040 +timedemo demo1

KNOWN LIMITS
------------
  no sound, no mouse, networking is loopback only.  Key release is emulated
  from auto-repeat, so movement needs in_keyhold tuned to taste.

LICENCE
-------
GPL v2 -- see LICENSE.  The engine is id Software's, released under the GPL in
1999; the 68k assembly comes from NovaCoder's AmiQuake by way of the AmigaOS
port.  Source for these binaries is the tagged commit in the repository above,
which is how GPL v2 section 3 is satisfied.
TXT

tar cf "$TAR" -C build "$PKG"
rm -rf "$STAGE"
echo "[*] $TAR  (`wc -c < "$TAR"` bytes)"
tar tf "$TAR" | sed 's/^/      /'
