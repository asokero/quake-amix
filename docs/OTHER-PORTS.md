# Applying this to other AMIX programs

Written after getting Quake from 4.9 to 8.7 fps on real hardware, in answer to "would the same
thing speed up wolf3d, doom, or xrtg?"

**Everything about Quake here is measured. Everything about the other programs is inference**, and
is marked as such. The point of the document is the order in which to look, not a promise about
any particular number.

## The compiler was the smallest of the three wins

This matters because "recompile it with a newer gcc" is the memorable part of the story and the
least of it:

| Win | Factor | What it actually was |
|---|--:|---|
| `ARCH=68040` (FSGLMUL → FSMUL) | **3.6×** | the CPU was emulating instructions in software |
| Zorro III + a 32-bit frame copy | ~1.4× | the bus was being used a byte at a time |
| gcc 14 instead of gcc 2.7.2.3 | 1.16× | compiler quality |

The first two were **hardware being used wrongly**, not code being written badly. Both were found
by measuring, neither by reading code, and both were cheap to fix once seen. A new program
deserves the same order of attention.

## What to check first, before touching the compiler

**1. Is the CPU emulating instructions?** The 68060 implements a subset of the 68881/68882 FPU and
of the 68020 integer set; everything else traps to the FPSP or ISP and runs in software. A Quake
build compiled at `-m68020` contained 956 `FSGLMUL`/`FSGLDIV`, none of which the 060 has, in the
renderer's hottest arithmetic. Recompiling at `-m68040` was worth 3.6×.

```sh
objdump -d PROGRAM | grep -cE 'fsglmul|fsgldiv|fmovecr|fintrz|fscale|fgetexp|fsin|fcos|ftan'
```

Any hit in a hot path is worth chasing. Note this is a **floating-point** problem: a fixed-point
program will show nothing here and has nothing to gain.

**2. How does it get pixels to the card?** AMIX's libc `memcpy` is a byte loop — literally
`move.b (a1)+,(a0)+` around a `dbf`. On a 16-bit bus a byte write costs the cycle a word write
would, so a byte-wise copy spends twice the bus cycles of a word copy and four times the loop
overhead of a long-word one. Replacing it took Quake's frame copy from 62 ms to 29.6 ms, and the
frame rate from 5.8 to 5.8+0.9. `VA2000_CopyRow` in `src/vid_va2000.c` is the whole fix, about
twenty lines.

**3. Only then, the compiler.** It is real — 16.3% here — but it is the smallest lever and by far
the most work to set up.

## Wolf3D

Inference, not measurement.

**Likely to gain less than Quake, for a specific reason.** Its raycaster is fixed-point integer
arithmetic. The FSGLMUL problem is a floating-point problem and does not touch it at all, so the
3.6× has no analogue. What is left is compiler quality against a much simpler inner loop, where
there is less for a better register allocator to do.

Two things worth checking before anything else:

* **How `id_vl_amix.c` gets `chunkyBuffer` to the card.** If it uses `memcpy`, check 2 above
  applies directly and is a ten-line change.
* wolf3d-amix builds **natively on AMIX** with `/usr/local/bin/gcc`, so it never went through this
  project's cross wrapper and does not carry the four assembler bugs in `TOOLCHAIN.md`. It does
  carry the same compiler generation.

## Doom

There is no AMIX Doom port, so this is a new port rather than a recompile.

If one were made: Doom's renderer is fixed-point column drawing, the same class as Wolf3D. No FPU
emulation problem, but heavily memory-bandwidth bound — so the **bus lessons would transfer
directly and the compiler would be secondary**. It is also far lighter than Quake, so it might be
genuinely smooth on a 68060 rather than merely playable.

## Xrtg

This is the one to be most careful about, and where the compiler is probably the *least* of it.

**What would likely help more.** An X server spends its time parsing protocol and pushing pixels,
not computing. If xrtg writes to the framebuffer with `memcpy` or byte-wise, check 2 above is
worth more than any compiler change — and Zorro III is already done at the kernel level, so that
part is available now.

**What makes it riskier than Quake.**

* **`-malign-int` is far more dangerous here.** gcc defaults to 2-byte alignment for `int` and
  pointer members on m68k; AMIX's compiler used 4. Every struct shared with libc or the kernel
  depends on it, and an X server is full of them — ioctls, shared memory, event structures. **The
  failure mode is silent.** In Quake it presented as a program that initialised perfectly and
  printed nothing, because `FILE` came out 14 bytes instead of 16 and `stdout` pointed two bytes
  into the wrong entry. In an X server it could present as something much harder to read.
* X11R5's sources are even more K&R than Quake's. `-std=gnu89` covers a lot, but expect more
  friction.
* **The `-O2` hazard is unexplained.** Quake compiled with gcc 14 at `-O2` renders byte-identical
  frames and completes a 969-frame timedemo, and then corrupts a saved frame pointer when a map is
  loaded. `-O1` avoids it; the root cause was never found. Since it is not understood, there is no
  way to say whether xrtg would hit it.

## What transfers with certainty

The documentation, which is the part that would otherwise have to be rediscovered:

* `docs/TOOLCHAIN.md` — four ways gcc 2.7.2.3 and gas 2.8.1 disagree, three of them silent. These
  apply to anything **cross-compiled** with that toolchain; a native AMIX build is not affected.
* `docs/NEWER-COMPILER.md` — the four obstacles between a modern gcc and an AMIX binary:
  `DT_GNU_HASH`, AMIX's `va_start` macro, `-malign-int`, and the `-O2` hazard. Any program taking
  this route hits all four.
* `docs/BENCHMARKS.md` — how to measure without fooling yourself, including two ways this project
  did fool itself.

## And one thing the benchmark could not tell us

Quake at gcc 14 `-O2` passed every automated check this project has: it ran the demo, and its
rendered frames were byte-identical to the known-good build. It was still broken, because
`timedemo` replays recorded packets and never runs the server — half the engine was untested.

The bug was found by a person starting a new game from the menu.

Whatever gets ported next, the benchmark is not the test.
