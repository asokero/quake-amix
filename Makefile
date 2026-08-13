# Makefile for Quake on AMIX SVR4 (Amiga UNIX), cross-built from Linux.
#
#   make            build/quake, the current VIDEO backend (default: null)
#   make VIDEO=va2000
#   make clean
#   make config     print toolchain, flags and file counts
#
# The toolchain is the m68k-cbm-sysv4 cross GCC from gcc-cross-amix.  Put it on
# PATH first, or override CROSS_BIN:
#
#   export PATH=$$HOME/opt/amix-cross/bin:$$PATH
#
# See docs/TOOLCHAIN.md for the sysroot files this expects and why.

CROSS_BIN ?= $(HOME)/opt/amix-cross/bin
CC         = $(CROSS_BIN)/m68k-cbm-sysv4-gcc

# -m68020 with hardware FPU is the deliberate baseline: one binary that runs on
# the A3000's 68030+68882 and on the 68040/68060 accelerator cards.  Do not add
# -m68040: the cross wrapper assembles with a hardcoded `as -m68020`, and 040-only
# opcodes (fsmov/fdmov) would be rejected.
ARCH_FLAGS = -m68020

# GCC 2.7.2.3 is from 1995 and its optimizer is not trustworthy on this code.
# The wolf3d-amix port found -O hangs on one file and, worse, found a file that
# miscompiles WITHOUT -O.  Start at -O and move individual files to OPT_NONE
# below as problems are found on hardware -- with a comment saying which symptom.
OPT_FLAGS  = -O
OPT_NONE   =

DEFINES    = -DAMIX
INCLUDES   = -Isrc
CFLAGS     = $(ARCH_FLAGS) $(OPT_FLAGS) $(DEFINES) $(INCLUDES)
CFLAGS_NO  = $(ARCH_FLAGS) $(OPT_NONE) $(DEFINES) $(INCLUDES)

# Link order matters.  libamixcompat.a must come last: it carries the objects
# AMIX ships only in the static libc.a (the IEEE fp*() control functions libm
# needs, and gettimeofday + its syscall chain).  docs/TOOLCHAIN.md explains how
# it is built.
LIBS       = -lm -lc -lamixcompat

SRCDIR     = src
OBJDIR     = obj
TARGET     = build/quake

# ---------------------------------------------------------------- source lists

# The shared Quake engine, identical to every other port.
CORE = \
	chase cl_demo cl_input cl_main cl_parse cl_tent cmd common console crc cvar \
	d_edge d_fill d_init d_modech d_part d_polyse d_scan d_sky d_sprite d_surf \
	d_vars d_zpoint draw host host_cmd keys mathlib menu model net_dgrm net_loop \
	net_main net_vcr nonintel pr_cmds pr_edict pr_exec r_aclip r_alias r_bsp \
	r_draw r_edge r_efrag r_light r_main r_misc r_part r_sky r_sprite r_surf \
	r_vars sbar screen sv_main sv_move sv_phys sv_user view wad world zone

# AMIX platform layer.
#   sys_amix   id's sys_linux.c, adapted (poll instead of select/usleep, etc.)
#   net_amix   driver table, loopback only -- see the file for why not UDP
PLATFORM = sys_amix net_amix

# Sound and CD are stubs for now: the first target is a working renderer, and
# AMIX has no audio driver in this project yet.  snd_null replaces the whole
# snd_dma/snd_mem/snd_mix set, so those three are deliberately absent from CORE.
STUBS = snd_null cd_null in_null

# Video backend.  null renders into a malloc'd buffer and displays nothing,
# which is what the dedicated-server and pure-rasterisation benchmarks want.
VIDEO ?= null
ifeq ($(VIDEO),null)
VIDEO_SRC = vid_null
endif
ifeq ($(VIDEO),va2000)
VIDEO_SRC = vid_va2000
endif
ifndef VIDEO_SRC
$(error VIDEO must be one of: null va2000)
endif

SRCS = $(CORE) $(PLATFORM) $(STUBS) $(VIDEO_SRC)
OBJS = $(patsubst %,$(OBJDIR)/%.o,$(SRCS))

# ---------------------------------------------------------------------- rules

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p build
	$(CC) -o $@ $(OBJS) $(LIBS)
	@echo "Build complete: $@ (VIDEO=$(VIDEO))"
	@ls -l $@ | awk '{print "  size:", $$5, "bytes"}'

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Per-file optimizer opt-outs go here, each with the symptom that earned it:
#   $(OBJDIR)/foo.o: $(SRCDIR)/foo.c
#	@mkdir -p $(OBJDIR)
#	$(CC) $(CFLAGS_NO) -c $< -o $@

clean:
	rm -rf $(OBJDIR) build

config:
	@echo "CC       : $(CC)"
	@$(CC).real --version 2>/dev/null | head -1
	@echo "CFLAGS   : $(CFLAGS)"
	@echo "LIBS     : $(LIBS)"
	@echo "VIDEO    : $(VIDEO) -> $(VIDEO_SRC).c"
	@echo "C files  : $(words $(SRCS))"

.PHONY: all clean config
