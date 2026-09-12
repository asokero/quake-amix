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
| `build/quake-va2000-68040` | `/apps/quake/quake` | the game, needs the VA2000 |
| `build/quake-null-68040` | `/apps/quake/quake-bench` | renders to memory, displays nothing |
| `build/rawkeyprobe-68040` | `/apps/quake/rawkeyprobe` | diagnostic; see "Keyboard" below |
| `id1/pak0.pak` | `/apps/quake/id1/pak0.pak` | 18.7 MB, shareware pak is enough |

Any shared filesystem the machine can already see is the easy path — an NFS
export or a mounted SMB share. None of the emulator's tftp gymnastics apply on
real hardware, and in particular the 16 MB transfer limit does not, so `pak0.pak`
goes across in one piece.

```sh
# On the AMIX machine, with the build staged somewhere it can read:
mkdir -p /apps/quake/id1
cp $STAGING/quake-va2000-68040 /apps/quake/quake
cp $STAGING/quake-null-68040   /apps/quake/quake-bench
cp $STAGING/rawkeyprobe-68040  /apps/quake/rawkeyprobe
cp $STAGING/pak0.pak           /apps/quake/id1/
chmod +x /apps/quake/quake /apps/quake/quake-bench /apps/quake/rawkeyprobe
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
cd /apps/quake
./quake
```

## Keyboard

Keyboard only — the mouse is not implemented on either path. Which path you get
is printed at startup and decides how the game feels, so read that line:

```
IN_Init: /dev/screen rawkey, group 1            <- the console
IN_Init: tty fallback, in_keyhold 250 ms; ...   <- telnet
```

### The raw path, on the physical console

`/dev/screen` in `SIM_RAWKEY` mode reports Amiga raw key codes for presses
**and** releases. That gives Quake a real Ctrl, Shift and Alt — its three
default action keys — a real key-up, and any number of keys held at once.
Nothing needs rebinding and `in_keyhold` does nothing. This is the path to play
on.

Selecting it needs a screen group, which the console has and telnet does not.
`SCREEN_GROUP` overrides the group if the automatic lookup gets it wrong;
`-ttyinput` forces the tty path for comparison.

Quake's key table lives in `raw_keymap[]` in `src/in_amix.c`. Two entries there
are decisions rather than transcription: the numeric keypad is mapped to the
same digits as the number row, and the right Amiga key is a second Ctrl —
because the only real Ctrl on an Amiga keyboard is in the far bottom-left
corner, which is not where your left hand is while walking.

### The tty path, over telnet

A tty reports presses and never releases, so a key counts as held for as long
as the terminal keeps auto-repeating it and comes up `in_keyhold` milliseconds
after the repeats stop.

**Expect to tune that.** It is a cvar precisely because it cannot be guessed
from here:

- movement stutters at the start of a hold → raise it (`in_keyhold 400`)
- the player keeps moving after you let go → lower it (`in_keyhold 150`)

Set it from the Quake console (`~`), or put `in_keyhold 400` in
`/apps/quake/id1/autoexec.cfg`.

**A tty also cannot see modifier keys.** Ctrl, Shift and Alt never arrive as
keys of their own — Ctrl+A is just byte 0x01 — so every default bind that uses
one is dead. Quake binds attack to Ctrl, which means **you cannot shoot** until
you rebind. Put this in `/apps/quake/id1/autoexec.cfg` before the first run:

```
bind SPACE +attack
bind j +jump
in_keyhold 400
```

(Do not write `bind ALT +jump`: `ALT` is the string Quake parses for its own
K_ALT, and that key cannot reach a tty either, so the bind would be just as
dead as the one it replaced.)

### When a key does nothing

`rawkeyprobe` says whether the kernel reported the key at all, which is the
half of the question Quake cannot answer for you:

```sh
./rawkeyprobe                 # safe: opens the device, does not take the screen
./rawkeyprobe -capture 1 15   # takes the keyboard for 15 s; Escape ends it early
```

Capture activates a screen of its own, so it belongs on the physical console
and nowhere else; closing the descriptor puts the previous screen back. Hold a
chord — arrow + Ctrl + Shift — and check that every key produces its own `down`
and its own `up`. Bit 7 of the code marks a release.

It also counts `IETYPE_MOUSE` events and says so at the end. That is an open
question worth answering while you are there: `<sys/screen.h>` defines mouse
events but offers no `SIM_` mode to ask for them, so whether they arrive on this
descriptor anyway is what decides if the console build can have mouselook.

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
- Keyboard response over the tty is usable but not good. `in_keyhold` has not
  been tuned to a value anyone is happy with. The raw path exists to make that
  question irrelevant on the console — see below, it is the next thing to test.

Still open, and worth recording if you get to it:

- **the `/dev/screen` raw keyboard, which has never been run on the machine.**
  It compiles into all three builds and follows the doom-amix port's working
  implementation closely, but nothing here has seen it drive Quake. Check the
  startup line first, then that Ctrl fires and that walking while firing works;
  `rawkeyprobe` separates "the table is wrong" from "the kernel never said".
- whether `SIM_RAWKEY` also delivers mouse events, which `rawkeyprobe` reports
  and which decides whether the console build can have mouselook

- whether the aspect ratio is exactly right; `vid.aspect` is id's 320×200
  expression and the board scales 2× into a 640×480 frame
- the `ARCH=68020` build on actual 68030 silicon — it is shipped and has never
  been measured there
- the GCC 14 `-O1` build through the VA2000, which is the one gap in the
  benchmark table

Whatever fails, note **what was printed last**: stdout is unbuffered here, so the
final line before a crash really is the final line.
