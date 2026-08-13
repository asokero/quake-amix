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

Milestone 1 — the engine, with no display driver in the way.

- [x] Cross toolchain able to build floating-point code (this took fixing, see below)
- [x] All 72 C files compile with GCC 2.7.2.3
- [x] Links to an AMIX binary: `ELF 32-bit MSB executable, Motorola m68k, 68020, SYSV`
- [ ] Runs `-dedicated` under Amiberry
- [ ] Runs on real hardware
- [ ] VA2000 video backend
- [ ] Playable at 320×200×8bpp

Deferred on purpose: sound, networking (`src/net_amix.c` explains), other RTG
cards (xsvga, zz9000).

## Build

Cross-compiled from Linux with the `m68k-cbm-sysv4` toolchain — GCC 2.7.2.3 and
binutils 2.8.1, both period-correct for the target.

```sh
export PATH=$HOME/opt/amix-cross/bin:$PATH
make                    # dedicated/benchmark build, no display
make VIDEO=va2000       # once the VA2000 backend exists
```

**`docs/TOOLCHAIN.md` is required reading before changing the build.** Getting
floating-point code to compile at all needed toolchain work, and one of the
problems was silent: GCC 2.7.2.3 emits floating-point compares with their
operands reversed, and the register-to-register form assembles cleanly with the
comparison inverted. 88 such instructions in a full Quake build. The fix lives
in `gcc-cross-amix`'s wrapper; the analysis is in that document.

## Provenance and license

GPL v2, as the original Quake source.

Seeded from the GCC/m68k-amigaos port of NovaCoder's AmiQuake
(`github.com/terriblefire/amiquake`) rather than from pristine id source,
to inherit its portable 68k optimizations — the Quake III `InvSqrt`, a fast
reciprocal in the span drawer, per-frame turbulence caching. The AmigaOS
backends were removed; the Unix platform files are id's own, recovered from that
repository's history.

The VA2000 and termios layers follow the wolf3d-amix port, whose hardware
findings were established on the real machine. Copies of both are kept in
`docs/reference/`.

Requires Quake game data (`id1/pak0.pak`), which is not distributable and is not
included here.
