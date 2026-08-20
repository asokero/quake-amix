#!/bin/sh
# build-gcc14.sh -- build Quake for AMIX with GCC 14 instead of GCC 2.7.2.3.
#
# An experiment, not the supported build; `make` remains that.  The question it
# exists to answer is in docs/NEWER-COMPILER.md: the compiler is the leading
# unmeasured suspect for the gap against the AmigaOS port, rasterisation being
# 92% of a frame and both obvious ways to attack it having already lost.
#
# Uses the Debian cross packages -- gcc-m68k-linux-gnu and binutils-m68k-linux-gnu,
# which are already installed here for the kernel port's ELF inspection.
#
#   sh tools/build-gcc14.sh                  -> build/quake-gcc14         (no display)
#   VIDEO=va2000 sh tools/build-gcc14.sh     -> build/quake-gcc14-va2000  (playable)
#
# Output is STATIC on purpose.  A dynamic build compiles and links but segfaults
# inside AMIX's 1992 ld.so before main; static linking removes that from the
# problem rather than negotiating with it.  See docs/NEWER-COMPILER.md.

set -e

CC=m68k-linux-gnu-gcc
LD=m68k-linux-gnu-ld
SYSROOT="${AMIX_SYSROOT:-$HOME/opt/amix-cross/m68k-cbm-sysv4/sysroot}"
CRT="$SYSROOT/usr/ccs/lib"
# The static libc lives only in a real AMIX tree; the cross sysroot ships the
# shared one.  Override with AMIX_LIBC_A if yours is elsewhere.
LIBCA="${AMIX_LIBC_A:-$HOME/kehitys/amix-playground/vanillarw/usr/ccs/lib/libc.a}"
VIDEO="${VIDEO:-null}"
case "$VIDEO" in
	null)   VIDEO_SRC=vid_null   ; SUFFIX= ;;
	va2000) VIDEO_SRC=vid_va2000 ; SUFFIX=-va2000 ;;
	*) echo "ERROR: VIDEO must be null or va2000"; exit 1 ;;
esac
OUT="${OUT:-build/quake-gcc14$SUFFIX}"
OBJ="${OBJ:-obj-gcc14}"

test -x "$(command -v $CC)"  || { echo "ERROR: $CC not found (apt install gcc-m68k-linux-gnu)"; exit 1; }
test -f "$CRT/crt1.o"        || { echo "ERROR: AMIX crt objects missing: $CRT"; exit 1; }
test -f "$LIBCA"             || { echo "ERROR: static libc.a missing: $LIBCA"; exit 1; }

# -std=gnu89   gcc 14 defaults to C23, which rejects AMIX's 1992 K&R headers
# -fcommon     gcc 10+ defaults to -fno-common; Quake has tentative definitions
#              of the same global in more than one file
# -m68060      not available from gcc 2.7.2.3 at all
# -fno-aggressive-loop-optimizations: id's source walks two 2D arrays as if they
#              were flat -- model.c:665 iterates vecs[0][j] to j=7 through a
#              float[2][4], r_part.c:139 iterates avelocities[0][i] to i=485
#              through a vec3_t[162].  Both work and both are undefined
#              behaviour, and gcc 14 reports having detected it, which means -O2
#              is entitled to act on it.  gcc 2.7.2.3 never looked.
# -O1, not -O2, and this is not caution.  At -O2 the renderer is fine -- frame
# 800 of demo1 comes out byte-identical -- but starting a map crashes: the saved
# frame pointer is overwritten with its top half replaced (0x80047060 becomes
# 0x73007060) and the process dies in `unlk %a6`.  Demo playback never runs the
# server, which is why the benchmark passes and `map start` does not.  -O1 makes
# it go away, keeps most of the win (8.7 fps against 9.2 at -O2, versus 7.5 from
# gcc 2.7.2.3), and renders the same identical frame.  The cause is not yet
# found; the QuakeC VM and the entity spawning are the unexamined part.
OPT="${OPT:--O1}"
# Include order is load-bearing, and getting it wrong cost most of a debugging
# session.  AMIX's own <stdarg.h> defines va_start as
#     list = (void *)((char *)&lastarg + sizeof(lastarg))
# which assumes the last named parameter is spilled to its stack home with the
# varargs directly after it.  That held for the 1992 compiler.  gcc 14 keeps it
# in a register, so the macro produces a garbage pointer -- which is precisely
# why modern compilers require __builtin_va_start.  Quake builds every line it
# prints with vsprintf, so the symptom was a program that ran, printed nothing
# at -O0, and crashed inside strlen at -O2.
#
# So: -nostdinc, then gcc's own headers FIRST (stdarg.h, stddef.h) and AMIX's
# after.  -nostdinc also keeps the host's glibc headers out entirely, which
# would otherwise be searched and are wrong for this target in every respect.
GCCINC="$($CC -print-file-name=include)"
# -malign-int is not a tuning flag, it is THE ABI flag.  gcc defaults to 2-byte
# alignment for int and pointer members on m68k; AMIX's compiler used 4.  Every
# struct shared with libc is laid out differently without it -- FILE comes out
# 14 bytes instead of 16, so stdout, which is &__iob[1], points into the middle
# of the first entry and every stdio call writes to a zeroed struct.  The
# symptom is a program that runs correctly and prints absolutely nothing.
CFLAGS="$OPT -m68060 -malign-int -std=gnu89 -fcommon -ffreestanding -fno-strict-aliasing -fno-PIC \
        -fno-aggressive-loop-optimizations -nostdinc \
        -isystem $GCCINC -isystem $SYSROOT/usr/include \
        -DAMIX $EXTRA_DEFINES -Isrc"

CORE="chase cl_demo cl_input cl_main cl_parse cl_tent cmd common console crc cvar \
d_edge d_fill d_init d_modech d_part d_polyse d_scan d_sky d_sprite d_surf d_vars \
d_zpoint draw host host_cmd keys mathlib menu model net_dgrm net_loop net_main \
net_vcr nonintel pr_cmds pr_edict pr_exec r_aclip r_alias r_bsp r_draw r_edge \
r_efrag r_light r_main r_misc r_part r_sky r_sprite r_surf r_vars sbar screen \
sv_main sv_move sv_phys sv_user view wad world zone \
sys_amix net_amix snd_null cd_null in_amix $VIDEO_SRC"

mkdir -p "$OBJ" build

# AMIX defines _lib_version only in the dynamic libc.so.1, never in libc.a, and
# libm's atan2/pow reference it.  A static link has to supply it.
cat > "$OBJ/libver.c" <<'EOF'
/* SVR4 libm conformance mode: _IEEE_ = -1, _SVID_ = 0, _XOPEN_ = 1, _POSIX_ = 2 */
int _lib_version = 0;
EOF

OBJS=""
for b in $CORE libver; do
	src="src/$b.c"
	test -f "$src" || src="$OBJ/$b.c"
	$CC $CFLAGS -c "$src" -o "$OBJ/$b.o"
	OBJS="$OBJS $OBJ/$b.o"
done
echo "[*] compiled $(echo $OBJS | wc -w) objects"

# --hash-style=sysv: AMIX's loader knows DT_HASH only, never DT_GNU_HASH.
$LD -static --hash-style=sysv -o "$OUT" \
	"$CRT/crt1.o" "$CRT/crti.o" $OBJS \
	"$SYSROOT/usr/lib/libm.a" "$LIBCA" "$CRT/crtn.o"

# Neutralise PT_GNU_STACK.  `ld -z nognustack` is ignored by this binutils, and
# a 1992 loader has no reason to know the type; PT_NULL it must ignore.
python3 - "$OUT" <<'EOF'
import struct, sys
p = sys.argv[1]
d = bytearray(open(p, "rb").read())
phoff  = struct.unpack_from(">I", d, 0x1C)[0]
phentsz= struct.unpack_from(">H", d, 0x2A)[0]
phnum  = struct.unpack_from(">H", d, 0x2C)[0]
for i in range(phnum):
    off = phoff + i * phentsz
    if struct.unpack_from(">I", d, off)[0] == 0x6474e551:      # PT_GNU_STACK
        struct.pack_into(">I", d, off, 0)                      # PT_NULL
open(p, "wb").write(d)
EOF

echo "[*] $OUT"
file "$OUT" 2>/dev/null | sed 's/^/    /'
echo ""
echo "    Untested on hardware.  Before trusting any timing from it, compare a"
echo "    rendered frame against the gcc 2.7.2.3 build -- see docs/BENCHMARKS.md."
