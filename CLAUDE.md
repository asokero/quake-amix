# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Port of **Quake** (id Software, GPL v2) to **AMIX** — Amiga UNIX, SVR4 on 68k.
Target machine: Amiga 3000 with an accelerator (68040 Mercury / 68060), running
the 68040/68060 AMIX kernel port from `~/kehitys/amix-playground/kernelsupport`.

The point of the exercise is a benchmark: Quake is the heaviest game of its era,
and a working port makes the kernel port's performance measurable on real
silicon rather than argued about.

Scope of the first version, in order:

1. Console only — no X11.
2. VA2000 RTG framebuffer, 320×200×8bpp. Other RTG drivers (xsvga, zz9000) later.
3. No sound.
4. Networking deferred (see `src/net_amix.c`).

### Source base and provenance

Seeded from `~/kehitys/amix-playground/amixquake`, the GCC/m68k-amigaos port of
NovaCoder's AmiQuake (`github.com/terriblefire/amiquake`), **not** from pristine
id source. That base carries portable 68k optimizations worth having: Quake III
`InvSqrt` in `mathlib.c`, `fast_reciprocal` in `d_scan.c`, per-frame turbulence
table caching in `r_misc.c`, squared-distance dynamic light culling in `r_main.c`.

The AmigaOS backends were dropped. `src/sys_amix.c` and the `*_null.c` stubs are
id's own Unix files, recovered from that repo's git history (commit `967fc47`
deleted the non-Amiga platform files). Keep the amixquake clone around: its
`git log` explains why parts of the renderer look the way they do.

The first commit here is the pristine import, so every AMIX-specific change is
visible as a diff against the AmigaOS port.

GPL v2. Attribution and license headers stay.

## Build

Cross-compiled from Linux. Never build on AMIX itself — a full build there would
take hours.

```sh
export PATH=$HOME/opt/amix-cross/bin:$PATH
make                    # VIDEO=null, the default
make VIDEO=va2000
make clean
make config
```

**Read `docs/TOOLCHAIN.md` before touching the build.** The toolchain needed
fixes to compile floating-point code at all, one of which was silently
miscompiling comparisons. In particular:

- The sysroot needs `libm.a` and a hand-built `libamixcompat.a` that the
  bootstrap does not install.
- Link order is `-lm -lc -lamixcompat`, compat last, and `-lc` must be spelled
  out or the wrapper drops it.
- `-m68020` is deliberate: one binary for 030/040/060. `-m68040` cannot be
  assembled by the current wrapper.

## Reference material

`docs/reference/` holds read-only copies, not built, not edited:

| File | Why it is here |
|---|---|
| `vid_svgalib.c.orig` | id's console framebuffer backend. The structure `vid_va2000.c` should follow — `VID_Init`, `VID_Update`, `VID_SetPalette`, `D_BeginDirectRect`. |
| `sys_linux.c.orig` | What `sys_amix.c` was before adaptation. |
| `wolf3d_id_vl_amix.c.ref` | **The proven VA2000 recipe.** Register-level mode set, palette, framebuffer geometry, all verified on the real machine. |
| `wolf3d_id_in_amix.c.ref` | **The proven termios keyboard**, including SVR4 signal traps that took real debugging. |

The live wolf3d-amix port is at `~/kehitys/amix-playground/wolf3d-amix/master`;
its `CLAUDE.md` carries AMIX platform constraints that apply here too.

## VA2000 display

`/dev/va2000`, character device major 68, driver in `~/kehitys/va2000-amix`.
Facts established by the wolf3d port and not to be re-derived:

- `mmap` 4 MB (`0x00400000`) of the board; framebuffer starts at offset `0x10000`.
- 8bpp, stride = width in bytes; `fb[y*320 + x]` is a palette index.
- Mode set is **direct 16-bit register writes**, not ioctls. The docs in
  `va2000-amix` describe an ioctl interface that the working code does not use.
- Palette is direct register writes too: R at `0x200 + idx*2`, G at `0x400 + …`,
  B at `0x600 + …`.
- 320×200 uses scale mode 5 (2× both axes) inside an 800×600 frame, and the
  display reads 240 rows — clear all 240 or the bottom 40 show garbage.
- On exit, restore Amiga passthrough (`0x38 = 0xf8`, `0x4e = 1`) or the machine
  is left with no usable display.

Zorro II gives roughly 3.5 MB/s, so a 64 KB frame costs ~20 ms in blit alone.
Render into a malloc'd buffer and copy, as wolf3d does — do not rasterize
directly into the board.

## AMIX / SVR4 constraints

These come from the va2000 and wolf3d ports and are hard limits.

- **GCC 2.7.2.3, no C99.** Declarations at block start, no `//` comments in new
  files. ANSI prototypes are fine and are what this tree uses throughout — the
  "K&R, one parameter per declaration line" rule in the va2000 and wolf3d docs
  is about AMIX's *native* `cc` compiling kernel drivers, and does not apply to
  this cross-compiled user-space code. `src/vid_va2000.c` follows the
  surrounding Quake style.
- **`poll`, not `select`/`usleep`.** AMIX exports `poll` and `sysconf` from
  `libc.so.1` but not the BSD calls. See `Sys_ConsoleInput` in `sys_amix.c`.
- **`SA_RESTART` is a trap.** With a periodic SIGALRM, `select`/`poll` restart
  with the original timeout and never return. wolf3d busy-waits on
  `gettimeofday` instead; the reasoning is in `wolf3d_id_in_amix.c.ref`.
- **Ignore SIGTTIN/SIGTTOU** before touching the tty — opening the RTG screen can
  move the process out of the console's foreground process group.
- Shell is Bourne sh: backticks, no `$(...)`, no `[[`, no arrays. `grep -q` does
  not work.
- Big-endian, as the m68k-amigaos port already assumed.

## Optimizer

`-O` globally. GCC 2.7.2.3's optimizer is not trustworthy on this code and the
failures are not symmetric — wolf3d-amix found one file that **hangs** the
compiler at `-O` (`wl_draw.c`) and another that is **miscompiled without** `-O`
(`wl_main.c`). Expect to pin individual files. The Makefile has a commented
per-file rule and `CFLAGS_NO` ready; when you use it, record which symptom
earned it.

## Current state

**Milestone 1 is done: Quake runs on AMIX.** Verified under Amiberry on the
68040 kernel `68040-260812-06`: `./quake -dedicated` loads `pak0.pak` (339
files), initialises the 8 MB heap, runs the QuakeC VM, spawns `start.bsp` and
precaches every model, then sits in the game loop with CPU time climbing.

Not yet done: real hardware, `vid_va2000.c`, `timedemo` numbers, sound,
networking.

### If something breaks, suspect the toolchain first

Three of the four toolchain bugs found so far were **silent** — the assembler
accepted what gcc wrote and encoded something else. Symptoms looked like
application logic bugs every time: a texture-animation group that "lost" one
frame (it was `%` returning a stale register), and a SIGILL at a PC that
disassembled to `chk2.b` (it was a `switch` jumping four bytes past its case).

Before debugging Quake, check the generated assembly:

```sh
m68k-cbm-sysv4-gcc.real -S -O -DAMIX -Isrc src/foo.c -o /tmp/foo.s
grep -cE '\.swbeg|tdiv[su]|fcmp' /tmp/foo.s
```

`docs/TOOLCHAIN.md` has the full analysis. The fixes are in
`gcc-cross-amix/amix-gcc-wrapper.sh` and are **not committed there** — if that
repo is ever reinstalled, they must be reapplied or everything silently breaks
again.

## Testing

`docs/EMULATOR.md` has the working procedure and the traps, which cost real
time to find. The short version:

- **This project has its own Amiberry config, kernel copy and disk image**, and
  its own boot script `tools/emu-quake.sh`. Do not use the kernel project's
  `emu-reset-boot.sh`: it stages `kernelsupport/build/unix-040-dbg` and restores
  that project's golden image, so a test run writes into the kernel work's
  files. Ports here are serial 1236 and telnet **2325**.
- Keep a **quiet** kernel in `emu/kernel/`; the diagnostic builds print on every
  page fault, which no timing should be measured through.
- Amiberry has no VA2000, so it can only run the `VIDEO=null` build.
- The `login:` prompt goes to the Amiga console, **never to the serial log**.
- Console keys need `sendkeys.py` over Amiberry IPC (xdotool does not work),
  and the guest keymap is German.
- Inbound telnet needs the guest to `ping 10.0.2.2` first.
- `tools/amixsh.py` gives a scriptable shell. **Log out cleanly** — leaking
  sessions eventually wedges the guest.
- Long-running processes need `nohup`, or closing the telnet session SIGHUPs
  them. A "crash" that leaves no exit code is usually this.
- TFTP tops out at 16,776,704 bytes: AMIX's client treats the block number as
  signed 16-bit. `pak0.pak` is 18.7 MB, so it must be split — see
  `docs/EMULATOR.md`.

Real hardware is `10.0.10.10` (root/`s0l0n`), file transfer via the NAS share
`/mnt/nasu/` on AMIX, `/run/user/12044/gvfs/smb-share:server=nasu.local,share=public/`
on Linux.
