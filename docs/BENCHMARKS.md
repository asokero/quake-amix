# Benchmarks

`timedemo demo1`, 320×200×8bpp, shareware `pak0.pak`, 8 MB heap, 600 KB surface
cache. 969 frames every time, so the seconds column is the raw measurement and
fps is derived.

## Real hardware

Amiga 3000 + 68060, AMIX kernel `68060-260813-01`, node `solon`. Builds are
identical apart from the flag named; both render the same frames.

| Build | `ARCH` | Kernel | Bus | Seconds | fps | |
|---|---|---|---|--:|--:|---|
| `quake-null` | 68020 | 260813-01 | — | 495.5 | 2.0 | FSGLMUL/FSGLDIV, emulated by the FPSP |
| `quake-null` | 68040 | 260813-01 | — | 137.1 | 7.1 | FSMUL/FSDIV, hardware — **3.6× faster** |
| `quake-va2000` | 68040 | 260813-01 | Zorro II | 197.3 | 4.9 | display via libc `memcpy` |
| `quake-va2000` | 68040 | 260813-01 | Zorro II | 165.8 | 5.8 | display via a 32-bit copy |
| `quake-null` | 68040 | 260819-13 | — | **129.9** | **7.5** | newer kernel, 5.5% faster on CPU alone |
| `quake-va2000` | 68040 | 260819-13 | **Zorro III** | **141.0** | **6.9** | |
| `quake-null`, **gcc 14.2.0** | 68060 | 260819-13 | — | **105.9** | **9.2** | same source, **22.6% faster** |
| `quake-va2000`, **gcc 14.2.0** | 68060 | 260819-13 | Zorro III | **118.4** | **8.2** | playable build |

`quake-null` renders into a malloc'd buffer and displays nothing, so its number
is the CPU alone. Subtracting it gives what the board costs:

| | per frame | effective |
|---|--:|--:|
| rasterisation | 141 ms | |
| Zorro II copy, byte-wise | 62 ms | 1.0 MB/s |
| Zorro II copy, 32-bit | **29.6 ms** | **2.2 MB/s** |

### The compiler was worth 22.6%, and it renders identically

The leading suspect for the gap against the AmigaOS port was the compiler: this project used GCC
**2.7.2.3** (1995) against the AmigaOS port's GCC 6.5. Measured, same source, same machine, same
demo, clean machine both times:

| | Seconds | fps |
|---|--:|--:|
| gcc 2.7.2.3, `-O -m68040` | 129.9 | 7.5 |
| **gcc 14.2.0, `-O2 -m68060`** | **105.9** | **9.2** |

**24 seconds off a 130-second run, from the compiler alone.** Rasterisation is 92% of a frame, so
this is very nearly all of it.

And it draws the same picture. Frame 800 of `demo1`, dumped from both builds and compared on the
machine:

```
VIDSUM frame 800 = f255bf50     gcc 2.7.2.3
VIDSUM frame 800 = f255bf50     gcc 14.2.0
cmp fr-gcc27.raw fr-gcc14.raw   ->  identical, 64000 bytes
```

`tools/build-gcc14.sh` builds it; `docs/NEWER-COMPILER.md` documents the four ABI and format
obstacles that had to be cleared first, one of which — `-malign-int` — is the difference between a
working program and one that runs correctly and prints absolutely nothing.

The display build was then measured too: **118.4 s, 8.2 fps** through the VA2000, against 6.9 with
gcc 2.7.2.3. The extrapolation from CPU time plus the measured copy said 8.3, so for once the
arithmetic held.

Where the playable build has got to over this session:

| | fps |
|---|--:|
| gcc 2.7, Zorro II, libc `memcpy` | 4.9 |
| gcc 2.7, Zorro II, 32-bit copy | 5.8 |
| gcc 2.7, Zorro III | 6.9 |
| **gcc 14, Zorro III** | **8.2** |

### Zorro III: measured, and it paid

The VA2000 is a Zorro II **and** a Zorro III card depending on firmware. Once it was switched to
Zorro III mode and the kernel gained support for it, the frame copy was measured again. The
kernel changed at the same time, so the CPU-only baseline was re-measured too — without that,
the kernel's own improvement would have been credited to the bus.

| | display per frame | effective |
|---|--:|--:|
| Zorro II, 32-bit copy | 29.6 ms | 2.16 MB/s |
| **Zorro III, same copy** | **11.5 ms** | **5.59 MB/s** |

**2.6× on the copy**, slightly better than the 2× the kernel project's busbench predicted. Total
24.8 s off the run, which decomposes as:

| | |
|---|--:|
| the bus | 17.6 s (71%) |
| the newer kernel's faster CPU rendering | 7.2 s (29%) |

Isolating the bus: with the new kernel, Zorro II would have run 158.6 s = 6.1 fps, so **Zorro III
alone is worth +0.8 fps, 13%**. The display is now **7.9% of the frame**, down from 17.3%.

The prediction made before this measurement was 6.9 fps, and the measurement is 6.9 fps — but for
the wrong reasons, and the agreement is a coincidence of two cancelling errors. It assumed the copy
would reach ~15 MB/s (it reached 5.6) and that CPU time would stay at 141 ms (it fell to 130).
Worth remembering before trusting the next extrapolation from a bus figure.

### The bottleneck is now unambiguously the rasteriser

129.9 ms of a 141.0 ms frame — **92%** — is CPU. Everything remaining on the display side is
capped at +0.6 fps, and the two obvious rasteriser levers have already been tried and lost:
hand-written assembly was slower than the compiler, and -O2 was slower than -O. The next
improvement will have to be algorithmic, or come from a newer compiler.

### The frame copy was paying four times over

AMIX's libc `memcpy` is a byte loop — `move.b (a1)+,(a0)+` around a `dbf`. The
destination is the board, where every access is a bus cycle, and on the 16-bit
Zorro II bus it currently sits on, a byte write costs the same cycle a word
write would. Copying
64 KB a byte at a time therefore spends twice the bus cycles of a word copy and
four times the loop overhead of a long-word one.

`VA2000_CopyRow` in `vid_va2000.c` copies in 32-bit units instead, handling the
head and tail for rects that are not long-aligned. That is the 62 ms → 29.6 ms
row above: **+0.9 fps for a copy loop**, no kernel involvement.

2.2 MB/s is around 60% of Zorro II's practical ceiling on an A3000 (~3.5 MB/s),
so some room remains — burst access, or moving the copy off the CPU.

### The prediction this replaced

Before the card was switched, the estimate here was that Zorro III would be worth about +1.1 fps,
taking the display from 17% of the frame to roughly 3%. The measurement above says +1.1 fps
against the same starting point — and 7.9%, not 3%.

It is kept as a note because the arithmetic behind it was wrong in two places that happened to
cancel: it assumed the copy would reach ~15 MB/s (5.6) and that CPU time would stay put (it fell
7.2 s under a newer kernel). Extrapolating a frame rate from a bus bandwidth figure is worth less
than it looks.

The proportions do hold at higher resolutions, since Quake's rasterisation scales with pixel count
much as the copy does; the absolute milliseconds grow on both sides.

### Why the flag is worth 3.6×

At `-m68020` gcc compiles single-precision multiply and divide to FSGLMUL and
FSGLDIV. **Neither is implemented in the 68060's FPU** — each one takes an
unimplemented-instruction trap and runs in software. There are 956 of them in
the binary, against 314 ordinary `fmul`/`fdiv`, and they sit in the renderer's
hottest arithmetic. `-m68040` emits FSMUL/FSDIV, which the 040 and 060 both
execute in hardware. `docs/TOOLCHAIN.md` §1e has the details; the flag only
became usable after the wrapper stopped hardcoding `as -m68020`.

The 68020 build is still the one to use on a 68030 — a 68040 binary will not
run there.

### For scale

NovaCoder's AmiQuake reaches roughly 10–14 fps on a 68060 at this resolution,
with hand-written 68040 assembly for the span drawer and C2P, and no MMU
underneath it. 7.1 fps from portable C on top of a Unix kernel is a reasonable
starting point rather than a disappointing one. The obvious remaining headroom
is the same assembly: `d_scan_68k.s` exists in the AmigaOS port, in vasm syntax,
and would need converting for GNU as.

## -O2 does not break anything, and is slower

| `demo1`, 969 frames | Seconds | fps |
|---|--:|--:|
| `-O` | **137.1** | **7.1** |
| `-O2` | 142.5 | 6.8 |
| `-O2 -fno-schedule-insns -fno-schedule-insns2` | 142.4 | 6.8 |

**3.9% slower**, consistently, so there is no reason to adopt it. Instruction
scheduling is not the cause — disabling it changes the binary and not the time.
The likely culprit is code growth (371 → 377 KB) against the 68060's 8 KB
instruction cache.

Worth recording because gcc 2.7.2.3 at -O2 was a real unknown: wolf3d-amix
found this same compiler both hanging at `-O` on one file and miscompiling
another *without* `-O`. Here all 75 files compile, none hangs, and -O2
introduces no instruction mnemonic that -O did not already use — so none of the
assembler mismatches in `docs/TOOLCHAIN.md` are newly exposed.

### Proving it renders the same, rather than assuming

An identical frame count proves nothing about pixels. `vid_null.c` has two
`#ifdef` hooks for settling this, and they are the tool for any future
optimisation, not just this one:

```sh
make DEFINES="-DAMIX -DVID_CHECKSUM"                      # per-frame checksums
make DEFINES="-DAMIX -DVID_CHECKSUM -DVID_DUMP_FRAME=800" # dump frame 800, then quit
```

Both builds' frame 200 and frame 800 of `demo1` came out **byte-identical,
0 pixels different of 64000**. -O2 renders exactly what -O renders.

One trap, since it cost a wrong conclusion first time: a *cumulative* checksum
is useless here. Frames drawn while the map loads contain whatever console text
had been printed by then, which is timing-dependent, so a single early
difference poisons every later value — two builds that render the demo
identically still disagree at every checkpoint. The checksum is per frame now,
and a dumped frame is what actually settles a disagreement.

## Hand-written assembly did not beat the compiler

`src/d_scan_amix.s` is `D_DrawTurbulent8Span` converted from the AmigaOS port's
vasm assembly to this assembler. `demo2` runs on e1m4, the water map, so it
exercises the routine properly:

| `demo2`, 985 frames | Seconds | fps |
|---|--:|--:|
| C, compiled by gcc 2.7.2.3 | 130.7 | 7.5 |
| hand-written assembly | 131.9 | 7.5 |

**0.9% slower**, on a clean machine both times. The routine is a short integer
loop dominated by two dependent memory accesses — the sine-table lookup and the
texel fetch — and gcc schedules it about as well as the hand-written version
does. `ASM=no` is therefore the default.

This is worth knowing before anyone spends time on the span drawer: the
assembly route is not the easy win it looks like. `D_DrawSpans8` has more state
and might reward hand-tuning more, but the expectation should be tempered by
this result rather than by the assembly's provenance.

### The span drawer in that file was not converted, and should not be

`d_scan_68k.s` in the AmigaOS port also contains a `D_DrawSpans8`. It does not
implement what `d_scan.c`'s `D_DrawSpans8` does:

- the C recomputes the perspective every 8 pixels — fresh `sdivz`, `tdivz`,
  `zi`, `z`, `snext`, `tnext`, `sstep`, `tstep` per segment. The assembly
  computes one `sstep` for a whole span, so long spans would visibly warp.
- it has no `tstep` at all.
- it destroys `t`: the inner loop keeps `t` in `%d3`, then loads the fetched
  texel into `%d3`. From the second pixel of every span onward, the t
  coordinate is the previous pixel's colour index.
- it reads and writes the z-buffer, which `D_DrawSpans8` does not touch, and
  tests "transparency" against a register overwritten two instructions earlier.

A faithful conversion would have produced a faster renderer that draws the
wrong thing. Writing a correct one from the C is a separate piece of work.

## Under Amiberry — do not use these as numbers

| Run | Seconds | fps | Host doing |
|---|--:|--:|---|
| 1 | 274.6 | 3.5 | mostly idle |
| 2 | 530.9 | 1.8 | compiling, polling |
| 3 | 764.6 | 1.3 | deliberately idle |

Same binary and data each time. **The emulator's throughput follows the host's
load, and the spread is wider than any change being measured.** Amiberry is
good for proving the engine runs; it cannot produce a performance figure.

## Method, and two ways to get it wrong

Both of these produced numbers that had to be thrown away.

**`timedemo` does not exit.** Quake prints the result and returns to its
console, where it spins at full speed forever. Three finished runs were still
burning CPU while a fourth was being measured; every one of those figures was
low, and the first "real hardware" result (1.8 fps) was one of them.

**AMIX has no `pkill`.** A cleanup step that used it failed silently, the
previous run kept spinning, and the next measurement was contended too.

So, before every run:

```sh
ps -ef | grep quake | grep -v grep    # expect nothing
kill -9 <pid>...                      # by pid; pkill does not exist
uptime                                # expect load average 0.00
```

and after it, kill the process that just finished.

Anything compiled on the machine during a run counts as load too — building
`va2000_restore` on the Amiga cost one measurement.
