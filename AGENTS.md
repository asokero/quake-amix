# Working in this repository with an AI assistant

This file is for coding agents. It is the canonical instruction set; `CLAUDE.md` points here so
that the rules cannot drift between tools.

This is a port of id Software's **Quake** to **AMIX** — Commodore's Amiga UNIX, System V Release 4
on 68k. It exists to produce a number. Quake is the heaviest game of its era, so a working port
turns "how fast is the 68040/68060 AMIX kernel port on real silicon" from an argument into a
measurement. Treat performance claims as the deliverable, and hold them to the standard that
implies.

## Read these first

1. `docs/TOOLCHAIN.md`. **Not optional.** The cross toolchain silently miscompiled this code in
   three separate ways, and every one of them presented as an application bug. If something is
   broken and you have not read this, you are about to spend hours in the wrong file.
2. `docs/BENCHMARKS.md` for what has been measured, on what, and how — including two ways of
   measuring wrong that produced numbers which had to be discarded.
3. `docs/EMULATOR.md` before touching the emulator, `docs/HARDWARE.md` before touching the real
   machine.
4. `README.md` for current state.

If a document disagrees with the code, the code is right and the document needs fixing. Say so
rather than working around it.

## Reference material

`docs/reference/` holds read-only copies. Not built, not edited, and worth reading before writing
anything platform-specific — each one is a working implementation of something this port needs:

| File | Why it is here |
|---|---|
| `wolf3d_id_vl_amix.c.ref` | **The proven VA2000 sequence.** Mode set, palette and framebuffer geometry, all established on the real machine by the wolf3d-amix port. `src/vid_va2000.c` follows it, and worked first try on hardware because of that. |
| `wolf3d_id_in_amix.c.ref` | **The proven termios keyboard**, including the SVR4 signal traps that took real debugging — and the fact that AMIX's console sends the 8-bit CSI introducer rather than `ESC [`. |
| `vid_svgalib.c.orig` | id's own console framebuffer backend. The structure a video driver here should follow. |
| `sys_linux.c.orig` | What `src/sys_amix.c` was before adaptation, for seeing what changed and why. |

The live wolf3d-amix port is at `~/kehitys/amix-playground/wolf3d-amix/master`; its `CLAUDE.md`
carries AMIX platform constraints that apply here too. The AmigaOS Quake port this tree was seeded
from is at `~/kehitys/amix-playground/amixquake` — keep it, its `git log` explains why parts of the
renderer look the way they do.

## Suspect the toolchain before you suspect the code

The compiler is GCC **2.7.2.3** (1995) with binutils **2.8.1** (1997), and they disagree about
several m68k mnemonics and one assembler directive. The assembler takes the disagreement without
complaint. Three of the four defects found so far were silent:

| Defect | How it presented |
|---|---|
| `.swbeg` reserves no bytes | every jump-table `switch` branched four bytes past its case — a SIGILL at a PC that `adb` disassembled as `chk2.b`, an instruction no C compiler emits |
| `tdivs.l` assembles as the 64-bit divide | every 32-bit `%` returned a stale register — a texture animation that "lost" one frame |
| float compares emitted reversed | 88 register-to-register comparisons ran inverted, with no diagnostic |

All are fixed in `gcc-cross-amix/amix-gcc-wrapper.sh`. **Those fixes are not committed in that
repository.** If it is ever reinstalled, reapply them or everything silently breaks again.

When a symptom looks like broken logic in code that is unchanged from every other Quake port,
check what came out of the assembler first:

```sh
m68k-cbm-sysv4-gcc.real -S -O -m68040 -DAMIX -Isrc src/foo.c -o /tmp/foo.s
grep -cE '\.swbeg|tdiv[su]|fcmp' /tmp/foo.s
```

A count above zero is not itself a bug — the wrapper repairs those — but it tells you the file
contains the constructs that have bitten before.

## How performance claims are made here

Every number in `docs/BENCHMARKS.md` names the machine, the kernel build, the demo and the
resolution. Reproduce that or do not add a row.

**Before every timing run:**

```sh
ps -ef | grep quake | grep -v grep    # expect nothing
kill -9 <pid>...                      # by pid -- AMIX has no pkill
uptime                                # expect load average 0.00
```

and kill the process afterwards. This is not ceremony. `timedemo` does not exit: Quake prints the
result and returns to its console, where it spins at full speed forever. Four measurements in this
project were taken against a machine with one to three finished runs still burning CPU, and every
one of them was wrong. A cleanup step that used `pkill` failed silently — the command does not
exist on AMIX — and contaminated the next run too. Anything compiled on the Amiga during a run
counts as load as well.

**A frame count proves nothing about pixels.** Two builds that both render 969 frames may render
different ones. `vid_null.c` carries two `#ifdef` hooks for settling this:

```sh
make DEFINES="-DAMIX -DVID_CHECKSUM"                      # per-frame checksum
make DEFINES="-DAMIX -DVID_CHECKSUM -DVID_DUMP_FRAME=800" # dump frame 800, then quit
```

Use them whenever an optimisation could change output. Use the frame dump, not the checksum, to
settle a disagreement: a checksum says two builds differ, a dumped frame says by how many pixels,
and the difference between "eleven pixels" and "forty thousand pixels" is the difference between
floating-point rounding and a broken renderer. Do not use a *cumulative* checksum — frames drawn
while the map loads contain whatever console text had been printed by then, which is
timing-dependent, so one early difference poisons every later value. That mistake was made here
and produced a confident wrong conclusion.

## The emulator is not the machine

Amiberry can prove the engine runs. It cannot produce a performance figure: its throughput follows
the *host's* load, and three runs of the same binary spanned 1.3 to 3.5 fps. Never quote an
emulator number as a performance result.

It also has no VA2000, so only `VIDEO=null` runs there.

This project has its own Amiberry config, its own kernel copy and its own disk image, driven by
`tools/emu-quake.sh`. Do not use the kernel project's `emu-reset-boot.sh`: it stages
`kernelsupport/build/unix-040-dbg` and restores that project's golden image, so a benchmark run
writes into someone else's working files. During bring-up that twice booted a kernel which had
been rebuilt underneath the test, once a diagnostic build whose per-page-fault printing would have
invalidated any timing. Keep a **quiet** kernel in `emu/kernel/`.

## Things that look mechanical and are not

* **`ARCH` is not just about which CPUs the binary loads on.** At `-m68020` gcc emits FSGLMUL and
  FSGLDIV for single-precision multiply and divide, and neither is implemented in the 68060's FPU —
  all 956 of them in a Quake build trap into the FPSP and run in software, in the renderer's
  hottest arithmetic. `ARCH=68040` emits FSMUL/FSDIV, which the 040 and 060 execute in hardware,
  and measured **3.6× faster** on real silicon. Before assuming an instruction is free on this CPU,
  check it against the 68060's unimplemented list.
* **`memcpy` is not a memory copy when the destination is a Zorro board.** AMIX's libc `memcpy` is
  a byte loop, and on a 16-bit bus a byte write costs the cycle a word write would. Replacing it
  with a 32-bit copy took the frame blit from 62 ms to 29.6 ms. Anything writing to the card should
  be looked at with the bus in mind, not the instruction count.
* **Hand-written assembly is not automatically faster.** `src/d_scan_amix.s` is a faithful
  conversion of the AmigaOS port's routine and measured **0.9% slower** than what gcc 2.7.2.3 makes
  of the same loop in C. `ASM=no` is the default for that reason. Measure before assuming.
* **The AmigaOS port's assembly is not a reference implementation.** Its `D_DrawSpans8` does not do
  what `d_scan.c`'s does: no per-8-pixel perspective recomputation, no `tstep` at all, and it
  destroys the t coordinate by loading the fetched texel into the register holding it. Converting
  it faithfully would produce a faster renderer that draws the wrong thing. Read the C before
  trusting any of it.
* **`-O2` is slower here**, by 3.9%, while rendering byte-identical frames. It was checked
  properly; do not re-litigate it without a new measurement.

## Code that runs on AMIX

* GCC 2.7.2.3, so no C99: declarations at block start, no `//` comments in new files. ANSI
  prototypes are fine and are what this tree uses. The "K&R, one parameter per declaration line"
  rule in the va2000 and wolf3d docs is about AMIX's *native* `cc` compiling kernel drivers and
  does not apply here.
* **`poll`, not `select` or `usleep`.** AMIX exports `poll` and `sysconf` from `libc.so.1`; the BSD
  calls exist only in the static `libc.a` and in `libucb.a`. `libamixcompat.a` carries the handful
  of static objects that are genuinely needed — see `docs/TOOLCHAIN.md` before adding to it, and
  prefer an SVR4 native over dragging in BSD compatibility wholesale.
* **`SA_RESTART` is a trap.** With a periodic SIGALRM, `select`/`poll` restart with the original
  timeout and never return.
* **Ignore SIGTTIN/SIGTTOU before touching the tty** — opening the RTG screen can move the process
  out of the console's foreground process group.
* A tty reports key presses and never releases. `in_amix.c` leans on auto-repeat and releases a key
  once bytes stop for `in_keyhold` ms; that cvar is the feel of the controls and is meant to be
  tuned on hardware, not guessed at here.
* Long-running processes need `nohup`, or closing the telnet session SIGHUPs them. A "crash" that
  leaves no exit code is almost always this.
* Bourne sh only: backticks, no `$(...)`, no `[[`. `grep` has no `-q`, `-E` or `-A`. `pkill` does
  not exist.

## The VA2000

`/dev/va2000`, character device major 68, driver in `~/kehitys/va2000-amix`. Established by the
wolf3d-amix port on real hardware and not to be re-derived:

* `mmap` 4 MB of the board; the framebuffer starts at offset `0x10000`.
* 8bpp, stride = width; `fb[y*320 + x]` is a palette index.
* Mode setting and the palette are **direct 16-bit register writes**. The `va2000-amix` docs
  describe an ioctl interface that the working code does not use.
* 320×200 uses scale mode 5 inside an 800×600 frame, and the display fetches 240 rows — clear all
  240 or the bottom of the screen shows old board contents.
* On exit restore Amiga passthrough, or the machine is left with no picture. Build
  `va2000_restore` from the driver's `tools/` and have it to hand *before* a session, because
  without a display you will be typing it blind.

The board is a Zorro II **and** Zorro III card depending on firmware; it runs in Zorro II mode only
because AMIX has no Zorro III support yet.

## Before you claim you are finished

* A measurement that was not taken on an idle machine is not a measurement.
* A hardware result you did not see did not happen. If a run has not happened, say that.
* If an optimisation could change what is drawn, show that it did not.
* If you attributed a fault to another project — the kernel port especially — and were wrong,
  retract it in the same place you claimed it. One diagnosis in this project blamed the kernel's
  known ISSUE-10 on the strength of a matching reproducer; the guest was merely saturated by disk
  I/O and came back on its own. A kernel bug is an expensive thing to attribute on circumstantial
  evidence.

## What not to do

* Do not raise the optimisation level, change `ARCH`, or enable `ASM` because it seems like it
  should help. All three were measured; two lost.
* Do not put the machine's root password in this repository. Pass it through `AMIX_PASS` in the
  environment.
* Do not commit game data. `pak0.pak` is not distributable and `.gitignore` covers it.
* Do not write into the kernel project's files — not `kernelsupport/build/`, not its golden image,
  not its Amiberry config. This project has its own of each.
* Do not quote a figure for another Quake port that you have not measured on the same machine, the
  same demo and the same resolution. The comparison that motivates this work deserves better than
  a remembered number.
