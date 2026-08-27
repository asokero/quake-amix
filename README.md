# Quake for AMIX

A port of id Software's **Quake** to **AMIX** — Commodore's Amiga UNIX, System V
Release 4 on the Motorola 68000 family.

The target is an Amiga 3000 with a 68040 or 68060 accelerator, running the
68040/68060 AMIX kernel port, displaying through an MNT VA2000 RTG card. It runs
from the console; there is no X11 dependency.

The motive is measurement. Quake is the most demanding game of the AMIX era, so
a working port turns "how fast is this kernel port on real silicon" from an
argument into a number.

## Status

**Milestone 1 reached: Quake runs on AMIX.** The dedicated server boots the
engine, loads `pak0.pak`, runs the QuakeC VM, spawns `start.bsp` with all its
models, and sits in the game loop accumulating CPU time — verified under
Amiberry on the 68040 kernel (`68040-260812-06`).

- [x] Cross toolchain able to build this code at all — four bugs found and fixed, see below
- [x] All 72 C files compile with GCC 2.7.2.3
- [x] Links to an AMIX binary: `ELF 32-bit MSB executable, Motorola m68k, 68020, SYSV`
- [x] Runs `-dedicated` under Amiberry: PAK loaded, QuakeC VM live, map spawned
- [x] Runs on real hardware — Amiga 3000 + 68060, kernel `68060-260813-01`
- [x] VA2000 video backend: **the game renders correctly on the board**, confirmed
      on hardware, first try
- [x] `timedemo` numbers: **7.5 fps** CPU-only, **6.9 fps** through the VA2000 on
      Zorro III, at 320×200 — see `docs/BENCHMARKS.md`
- [x] **9.2 fps** built with GCC 14 instead of GCC 2.7.2.3 — same source, identical
      frames, 22.6% faster (`tools/build-gcc14.sh`, `docs/NEWER-COMPILER.md`)
- [ ] Playable: keyboard is written but untried; no mouse, no sound

Deferred on purpose: sound, networking (`src/net_amix.c` explains), other RTG
cards (xsvga, zz9000).

## Build

Cross-compiled from Linux with the `m68k-cbm-sysv4` toolchain — GCC 2.7.2.3 and
binutils 2.8.1, both period-correct for the target.

```sh
export PATH=$HOME/opt/amix-cross/bin:$PATH
make                    # build/quake-null-68040
make VIDEO=va2000       # build/quake-va2000-68040
```

Binaries are named `quake-<VIDEO>-<ARCH>`, so the two cannot overwrite each
other and the wrong one cannot be shipped by accident.

| Knob | Default | |
|---|---|---|
| `VIDEO` | `null` | `null` renders to memory and displays nothing — the benchmark and dedicated-server build. `va2000` drives the board. |
| `ARCH` | `68040` | Runs on 68040 and 68060. `ARCH=68020` also runs on a 68030 and is **3.6× slower on a 68060** — see `docs/BENCHMARKS.md`. |
| `ASM` | `no` | Hand-written assembly for `D_DrawTurbulent8Span`. Measured slower than the C; off for that reason. |

**`docs/TOOLCHAIN.md` is required reading before changing the build.** Getting
this code to run at all needed four fixes to the toolchain, three of them
silent — GCC 2.7.2.3 and binutils 2.8.1 disagree about several m68k mnemonics
and one assembler directive, and the assembler takes the disagreement without
complaint:

| Problem | Effect | Count in a Quake build |
|---|---|--:|
| `.swbeg` emits nothing, gcc assumes 4 bytes | every jump-table `switch` branches mid-instruction | 21 |
| `tdivs.l`/`tdivu.l` assemble as the 64-bit divide | every 32-bit `%` returns a stale register | 17 |
| float compares emitted with operands reversed | register-to-register form silently inverts the comparison | 88 |
| `fsglmul`/`fsgldiv` with a register source | rejected by the assembler (loud) | ~130 |

The fixes live in `gcc-cross-amix`'s `amix-gcc-wrapper.sh`; the analysis, with
the worked example that exposed each one, is in that document.

## Documentation

| | |
|---|---|
| `docs/TOOLCHAIN.md` | four ways gcc 2.7.2.3 and gas 2.8.1 disagree, three of them silent |
| `docs/NEWER-COMPILER.md` | building with gcc 14 instead, and the four obstacles in the way |
| `docs/BENCHMARKS.md` | every measured number, and two ways this project measured wrong |
| `docs/EMULATOR.md`, `docs/HARDWARE.md` | how to run it in each place |
| `docs/OTHER-PORTS.md` | what would and would not transfer to wolf3d, doom, xrtg |

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
