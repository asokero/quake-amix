# Running on the real machine

Nothing in this file has been executed on hardware yet. It is written from what
the code does and from what the va2000-amix and wolf3d-amix ports established;
treat every step as a claim to be checked, and the "if it goes wrong" section as
the part most likely to be needed.

Target: Amiga 3000 with a 68040 or 68060 card, the AMIX 68040/68060 kernel port,
and an MNT VA2000. Machine is `10.0.10.10`, root / `s0l0n`.

## What to copy

| From | To on AMIX | Notes |
|---|---|---|
| `build/quake-va2000` | `/quake/quake` | the game, needs the VA2000 |
| `build/quake-null` | `/quake/quake-bench` | renders to memory, displays nothing |
| `id1/pak0.pak` | `/quake/id1/pak0.pak` | 18.7 MB, shareware pak is enough |

Over the NAS, which is the easy path on the real machine — none of the emulator's
tftp gymnastics apply:

```sh
# Linux
cp build/quake-va2000 build/quake-null \
   "/run/user/12044/gvfs/smb-share:server=nasu.local,share=public/va2000dev/quake/"

# AMIX
mkdir -p /quake/id1
cp /mnt/nasu/va2000dev/quake/quake-va2000 /quake/quake
cp /mnt/nasu/va2000dev/quake/quake-null   /quake/quake-bench
cp /mnt/nasu/va2000dev/quake/pak0.pak     /quake/id1/
chmod +x /quake/quake /quake/quake-bench
```

## Prerequisites

- **Run as root.** `/dev/va2000` and the framebuffer mmap need it.
- The va2000 driver must be installed and `/dev/va2000` must exist
  (`mknod /dev/va2000 c 68 0`). See `~/kehitys/va2000-amix`.
- The binary is 68020 + 68881 code, so it runs on 030, 040 and 060 alike. On
  040/060 it needs the kernel's FPSP, which the port has.
- Quake wants an 8 MB heap by default. `-mem 8` sets it explicitly; the emulated
  machine had ~30 MB free, real hardware should be comfortable.

## The measurement first

Do this one before anything with a picture. It renders into a malloc'd buffer
and displays nothing, so it measures the CPU alone with no Zorro II blit in the
number — which is exactly what tells us what the port and the kernel are worth:

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

**To quit**, use ESC → the menu → Quit → `y`. Only keys that are mapped are
needed for that path: ESC, the arrows, Enter and `y`. Function keys are not
mapped, so Quake's usual F10 will not work.

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

## Worth writing down

- both fps numbers, and which CPU and kernel build produced them
- whatever `in_keyhold` value ends up feeling right
- whether the aspect ratio looks right; `vid.aspect` is currently id's 320×200
  expression and the board's 2× scaling into a 640×480 frame may not match it
- anything printed to the terminal before a failure — stdout is unbuffered here,
  so the last line before a crash is the real last line
