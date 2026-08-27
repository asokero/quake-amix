# Running on the real machine

Every step here has been run on real hardware: an Amiga 3000 with a 68060
accelerator, AMIX 2.1p2a, the 68040/68060 kernel port, and an MNT VA2000. The
game renders, benchmarks and plays on that machine.

**One machine, though.** Nothing here has been tried on an Amiga 4000, on a
68040, on a 68030, or against a different VA2000 firmware revision. Where a step
is inference rather than experience it says so.

This file assumes you reach the machine over the network as root. Nothing in
this repository stores the address or the password: set `AMIX_HOST` for the
tooling, and pass the password through `AMIX_PASS` at the point of use.

## What to copy

| From | To on AMIX | Notes |
|---|---|---|
| `build/quake-va2000-68040` | `/quake/quake` | the game, needs the VA2000 |
| `build/quake-null-68040` | `/quake/quake-bench` | renders to memory, displays nothing |
| `id1/pak0.pak` | `/quake/id1/pak0.pak` | 18.7 MB, shareware pak is enough |

Any shared filesystem the machine can already see is the easy path — an NFS
export or a mounted SMB share. None of the emulator's tftp gymnastics apply on
real hardware, and in particular the 16 MB transfer limit does not, so `pak0.pak`
goes across in one piece.

```sh
# On the AMIX machine, with the build staged somewhere it can read:
mkdir -p /quake/id1
cp $STAGING/quake-va2000-68040 /quake/quake
cp $STAGING/quake-null-68040   /quake/quake-bench
cp $STAGING/pak0.pak           /quake/id1/
chmod +x /quake/quake /quake/quake-bench
```

**Put game data under `/`, not `/tmp`** — AMIX clears `/tmp` on every boot.

## Prerequisites

- **Run as root.** `/dev/va2000` and the framebuffer mmap need it.
- The va2000 driver must be installed and `/dev/va2000` must exist
  (`mknod /dev/va2000 c 68 0`). See the `va2000-amix` project.
- The default build is `-m68040`, so it needs a 68040 or 68060. Build
  `ARCH=68020` for a 68030 — it runs on 040/060 too, at roughly a third of the
  speed there, because the 68060 emulates the FSGLMUL/FSGLDIV it emits.
- Quake wants an 8 MB heap by default. `-mem 8` sets it explicitly; the emulated
  machine had ~30 MB free, real hardware should be comfortable.

## The measurement first

Do this one before anything with a picture. It renders into a malloc'd buffer
and displays nothing, so it measures the CPU alone with no framebuffer copy in
the number — which is exactly what tells us what the port and the kernel are worth:

```sh
cd /quake
./quake-bench +timedemo demo1 > /quake/bench.log 2>&1
```

The line worth reporting is the last one:

```
<n> frames <x.x> seconds <y.y> fps
```

Then the same demo through the VA2000, which adds the framebuffer copy over
Zorro II. The difference between the two numbers is the display cost:

```sh
./quake +timedemo demo1
```

## Playing

```sh
cd /quake
./quake
```

Keyboard only — the mouse is not implemented. It comes through the console tty,
which reports presses but never releases, so a key is held for as long as the
terminal keeps auto-repeating it and released `in_keyhold` milliseconds after
the repeats stop.

**Expect to tune that.** It is a cvar precisely because it cannot be guessed
from here:

- movement stutters at the start of a hold → raise it (`in_keyhold 400`)
- the player keeps moving after you let go → lower it (`in_keyhold 150`)

Set it from the Quake console (`~`), or put `in_keyhold 400` in
`/quake/id1/autoexec.cfg`.

**A tty cannot see modifier keys.** Ctrl, Shift and Alt never arrive as keys of
their own — Ctrl+A is just byte 0x01 — so every default bind that uses one is
dead. Quake binds attack to Ctrl, which means **you cannot shoot** until you
rebind. Put this in `/quake/id1/autoexec.cfg` before the first run:

```
bind SPACE +attack
bind ALT +jump
in_keyhold 400
```

(`ALT` there is the literal string Quake parses for its own K_ALT; it will not
fire either, so pick a letter key instead if jumping matters —
`bind j +jump`.)

**To quit**, use ESC → the menu → Quit → `y`. Only mapped keys are needed for
that path: ESC, the arrows, Enter and `y`. Function keys are not mapped, so
Quake's usual F10 will not work.

## If it goes wrong

**The screen stays black or garbled.** The mode-set register sequence is copied
verbatim from wolf3d-amix, which drives the same board in the same 320×200 8bpp
mode, so the values themselves are sound; a difference would be in how Quake
uses them. `docs/reference/wolf3d_id_vl_amix.c.ref` is the known-good original
to compare against.

**The machine is left with no picture.** `VID_Shutdown` restores Amiga
passthrough, but a crash skips it. Recover with the driver's own tool:

```sh
/path/to/va2000-amix/tools/va2000_restore
```

Worth having built and to hand *before* starting, since without a display you
will be typing it blind.

**`VID_Init: cannot open /dev/va2000`** — not root, or the device node or driver
is missing.

**It dies somewhere in the renderer.** Three of the four toolchain bugs found so
far were silent miscompilations that presented as application logic errors, and
`vid_va2000.c` and `in_amix.c` have never been through a compiler-vs-hardware
shakedown. Before reading the C, check what came out of the assembler —
`docs/TOOLCHAIN.md` §1 has the greps.

## What the first runs settled

- The picture is correct on the board, first try, and stayed correct across the
  Zorro II → Zorro III change.
- The numbers are in `docs/BENCHMARKS.md`, each one tagged with the CPU, the
  kernel build and the bus it came from. Report those three with any new figure
  or it cannot be compared against anything.
- Keyboard response is usable but not good. `in_keyhold` has not been tuned to a
  value anyone is happy with — if you find one, put it in the benchmarks doc.

Still open, and worth recording if you get to it:

- whether the aspect ratio is exactly right; `vid.aspect` is id's 320×200
  expression and the board scales 2× into a 640×480 frame
- the `ARCH=68020` build on actual 68030 silicon — it is shipped and has never
  been measured there
- the GCC 14 `-O1` build through the VA2000, which is the one gap in the
  benchmark table

Whatever fails, note **what was printed last**: stdout is unbuffered here, so the
final line before a crash really is the final line.
