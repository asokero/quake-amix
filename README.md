# Quake for AMIX

A port of id Software's **Quake** to **AMIX** — Commodore's Amiga UNIX, System V
Release 4 on the Motorola 68000 family. It runs from the console, renders through
an MNT VA2000 RTG card, and needs no X11.

## Overview

This is a hobby port, built with AI assistance, of the most demanding game of the
AMIX era onto an operating system that was discontinued in 1993.

The motive is **measurement**. There is an ongoing effort to make AMIX run on
68040 and 68060 processors, and "is the kernel port any good" is a hard question
to answer with a shell prompt. Quake answers it in a number: it exercises the
FPU, the memory system, the display path and the scheduler all at once, for
minutes at a time, reproducibly. Every optimization in this repository was
adopted or rejected on a measurement, and `docs/BENCHMARKS.md` records the
rejected ones too.

It builds on work by other people: id Software released the engine under the GPL,
NovaCoder wrote the 68k assembly that the AmigaOS port preserved, and the AMIX
side stands on the projects below.

| | |
|---|---|
| [`va2000-amix`](https://github.com/asokero/va2000-amix) | the VA2000 kernel driver this renders through |
| [`xrtg-amix`](https://github.com/asokero/xrtg-amix) | X11R5 RTG server for the same board — not used here, Quake needs no X11 |
| [`gcc-cross-amix`](https://github.com/isoriano1968/gcc-cross-amix) | the m68k-cbm-sysv4 cross toolchain, and where the four assembler fixes live |
| [`terriblefire/amiquake`](https://github.com/terriblefire/amiquake) | the AmigaOS port this tree was seeded from |

`wolf3d-amix`, whose VA2000 and termios layers were established on real hardware,
is the other direct ancestor; copies of both files are in `docs/reference/`.

## Disclaimer

A hobby project. Use it at your own risk.

- It has been tested on **one machine**: an Amiga 3000 with a 68060 accelerator,
  AMIX 2.1p2a, one VA2000 firmware revision. Other configurations may behave
  differently, and nothing here has been near an Amiga 4000.
- It needs to run as **root** (the framebuffer mmap does), which means a bug in
  the display path can leave the machine without a picture. `va2000_restore`
  from `va2000-amix` recovers it — build it and have it to hand first.
- Not affiliated with id Software, MNT Research, Commodore, or anyone else.

## Status (August 2026)

### Working

- **Runs on real hardware** — Amiga 3000 + 68060, AMIX 2.1p2a, kernel
  `68060-260819-13`
- **Renders correctly through the VA2000** at 320×200×8bpp, over Zorro II or
  Zorro III
- **Playable from the console** — menus, movement, loading a map, and actually
  playing it, confirmed on the machine
- Both a period-correct build (GCC 2.7.2.3) and a modern one (GCC 14.2.0), the
  latter measurably faster and producing byte-identical frames
- `timedemo` for measurement, with a per-frame checksum and a frame dump so that
  "faster" can be separated from "different"

### Not implemented, or known to be rough

- **No sound.** AMIX has no audio driver in this project; `snd_null.c` is a stub.
- **No mouse.** Keyboard only.
- **Networking is loopback only.** Enough for single player; `src/net_amix.c`
  explains what UDP would need.
- **Keyboard input is a compromise.** It arrives through the console tty, which
  reports presses but never releases, so a key is held until auto-repeat stops
  and then released `in_keyhold` milliseconds later. Expect to tune that cvar.
- **A tty cannot see Ctrl, Shift or Alt at all**, so Quake's default `+attack`
  bind is dead. You cannot shoot until you rebind — see Quick start.
- **GCC 14 at `-O2` crashes when a map loads.** The renderer is correct at `-O2`
  and the benchmark passes, but starting a game corrupts a saved frame pointer.
  `-O1` is the default for that reason and the root cause is not found.
- The GCC 14 build has not been timed through the VA2000 at `-O1` — the only
  display measurement for that compiler is the `-O2` build, which is not usable.

## Performance

Measured on the real machine, `timedemo demo1`, 969 frames at 320×200, nothing
else running. Full method and the failures along the way in `docs/BENCHMARKS.md`.

| Build | CPU only | Through the VA2000 (Zorro III) |
|---|--:|--:|
| GCC 2.7.2.3, `-O -m68040` | 7.5 fps | 6.9 fps |
| GCC 14.2.0, `-O1 -m68060` | **8.7 fps** | not yet timed |

Three findings are worth more than the frame rates themselves:

| | Factor |
|---|--:|
| Building for `-m68040` rather than `-m68020` | **3.6×** |
| Zorro III, plus a 32-bit framebuffer copy instead of libc `memcpy` | ~1.4× |
| GCC 14 instead of GCC 2.7.2.3 | 1.16× |

The first two were **hardware being used wrongly**, not code being written badly.
At `-m68020` the compiler emits `FSGLMUL`/`FSGLDIV`, which the 68060 does not
implement — every one of roughly 950 of them, in the renderer's hottest
arithmetic, trapped into the FPSP and was emulated in software. Rasterisation is
now about 92% of a frame, so that is where anything further has to come from.

## Hardware requirements

- Amiga 3000 (or 3000T) with a **68040 or 68060** accelerator
- **AMIX 2.1p2a** with the 68040/68060 kernel port
- **MNT VA2000** with the `va2000-amix` driver installed and `/dev/va2000`
  present, for a picture. Without one, the `VIDEO=null` build still runs and
  still benchmarks.
- About **8 MB** free for Quake's heap
- **Quake game data** — `id1/pak0.pak`. The shareware pak is enough for
  everything documented here. It is not included; see the licence section.

A 68030 will run the `ARCH=68020` build, slowly. That configuration is built and
shipped but has not been measured on 030 silicon.

## Quick start

Copy the binary and the game data onto the machine, however you normally move
files — NFS, a NAS mount, or `tftp`:

```sh
mkdir -p /quake/id1
cp quake-va2000-68040 /quake/quake
cp pak0.pak           /quake/id1/
chmod +x /quake/quake
```

**Rebind before the first run.** Attack is bound to Ctrl by default and a tty
cannot see Ctrl, so create `/quake/id1/autoexec.cfg`:

```
bind SPACE +attack
bind j +jump
in_keyhold 400
```

Then, as root:

```sh
cd /quake && ./quake
```

Quit with ESC → Quit → `y`; function keys are not mapped, so F10 does nothing.
If movement stutters at the start of a hold, raise `in_keyhold`; if the player
keeps moving after you let go, lower it.

To measure instead of play, use the `null` build — it renders into memory and
displays nothing, so the number is the CPU alone:

```sh
./quake-null-68040 +timedemo demo1
```

`docs/HARDWARE.md` is the full runbook, including what to do when the screen
stays black.

## Build

Cross-compiled from Linux with the `m68k-cbm-sysv4` toolchain — GCC 2.7.2.3 and
binutils 2.8.1, both period-correct for the target, from `gcc-cross-amix`.

```sh
export PATH=$HOME/opt/amix-cross/bin:$PATH
make                    # build/quake-null-68040
make VIDEO=va2000       # build/quake-va2000-68040
make config             # print toolchain, flags, file counts
```

Binaries are named `quake-<VIDEO>-<ARCH>`, so the two cannot overwrite each other
and the wrong one cannot be shipped by accident.

| Knob | Default | |
|---|---|---|
| `VIDEO` | `null` | `null` renders to memory and displays nothing — the benchmark and dedicated-server build. `va2000` drives the board. |
| `ARCH` | `68040` | Runs on 68040 and 68060. `ARCH=68020` also runs on a 68030 and is **3.6× slower on a 68060**. |
| `ASM` | `no` | Hand-written assembly for `D_DrawTurbulent8Span`. Measured 0.9% *slower* than the C; off for that reason, kept as a worked example. |

For the GCC 14 build, which does not use this Makefile, see
`docs/NEWER-COMPILER.md` and `tools/build-gcc14.sh`.

**`docs/TOOLCHAIN.md` is required reading before changing the build.** Getting
this code to run at all needed four fixes to the toolchain, three of them
silent — GCC 2.7.2.3 and binutils 2.8.1 disagree about several m68k mnemonics and
one assembler directive, and the assembler accepts the disagreement without
complaint:

| Problem | Effect | Count in a Quake build |
|---|---|--:|
| `.swbeg` emits nothing, gcc assumes 4 bytes | every jump-table `switch` branches mid-instruction | 21 |
| `tdivs.l`/`tdivu.l` assemble as the 64-bit divide | every 32-bit `%` returns a stale register | 17 |
| float compares emitted with operands reversed | register-to-register form silently inverts the comparison | 88 |
| `fsglmul`/`fsgldiv` with a register source | rejected by the assembler (loud) | ~130 |

The fixes live in `gcc-cross-amix`'s `amix-gcc-wrapper.sh`; the analysis, with
the worked example that exposed each one, is in that document.

## Binary releases

Prebuilt binaries are attached to the GitHub releases, six of them, in one tar:
both video backends × `68040`/`68020`, plus the two faster GCC 14 builds. AMIX
has no `unzip`, hence tar.

```sh
sh tools/make-release.sh v0.1      # -> build/quake-amix-v0.1.tar
```

The script builds every binary from clean and **verifies each one before writing
the archive**: a file named `-68040` must contain no `FSGLMUL`, a file named
`-68020` must contain some (otherwise the `ARCH` flag never reached the
compiler), and the GCC 14 builds must be static with no `PT_GNU_STACK`. Shipping
a binary labelled `-68040` that holds 68020 code is a mistake this project has
already made once, and it cost a factor of 3.6.

## Repository layout

| | |
|---|---|
| `src/` | The engine. id's Quake, plus the AMIX platform layer: `vid_va2000.c`, `vid_null.c`, `in_amix.c`, `sys_amix.c`, `net_amix.c`, `d_scan_amix.s` |
| `docs/` | Everything learned, including what did not work |
| `docs/reference/` | Known-good originals kept for comparison — id's `sys_linux.c` and `vid_svgalib.c`, wolf3d-amix's VA2000 and termios layers |
| `tools/` | `amixsh.py` (telnet shell), `emu-quake.sh` (Amiberry harness), `build-gcc14.sh` |

## Documentation

| | |
|---|---|
| `docs/TOOLCHAIN.md` | four ways GCC 2.7.2.3 and gas 2.8.1 disagree, three of them silent |
| `docs/NEWER-COMPILER.md` | building with GCC 14 instead, and the four obstacles in the way |
| `docs/BENCHMARKS.md` | every measured number, and two ways this project measured wrong |
| `docs/HARDWARE.md` | the runbook for the real machine |
| `docs/EMULATOR.md` | testing under Amiberry, and what it cannot tell you |
| `docs/OTHER-PORTS.md` | what would and would not transfer to wolf3d, doom, xrtg |
| `AGENTS.md` | working agreement for coding assistants in this repository |

## Provenance and license

**GPL v2** — the full text is in [`LICENSE`](LICENSE).

This is not a choice, and it is worth stating because the sibling AMIX projects
(`va2000-amix`, `xrtg-amix`, `zz9000-amix`) are MIT. This repository contains id
Software's Quake engine, which id released under the GPL v2 in 1999. Everything
that links against it inherits that, including the AMIX platform layer written
for this port — `vid_va2000.c`, `in_amix.c`, `sys_amix.c`, `net_amix.c`. Every
source file carries the header.

The chain of provenance, since it is three deep:

| Layer | Origin | |
|---|---|---|
| Engine | id Software, Quake 1996–97 | GPL v2 since 1999 |
| 68k optimizations | NovaCoder's AmiQuake, via the GCC/m68k-amigaos port at `github.com/terriblefire/amiquake` | GPL v2 |
| AMIX layer | this repository | GPL v2 |

Seeding from the AmigaOS port rather than from pristine id source was deliberate:
it inherits the portable 68k work — the Quake III `InvSqrt`, a fast reciprocal in
the span drawer, per-frame turbulence caching. The AmigaOS backends were removed;
the Unix platform files are id's own, recovered from that repository's history.
`src/d_scan_amix.s` descends from assembly NovaCoder wrote and the AmigaOS port
disassembled back into readable source, and stays readable source here for the
same reason.

The VA2000 and termios layers follow the wolf3d-amix port, whose hardware findings
were established on the real machine. Copies of both are kept in `docs/reference/`.

**Game data is not included and is not distributable.** Quake needs
`id1/pak0.pak`; the shareware pak is enough to run everything documented here.
