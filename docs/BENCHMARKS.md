# Benchmarks

`timedemo demo1`, 320×200×8bpp, shareware `pak0.pak`, 8 MB heap, 600 KB surface
cache. 969 frames every time, so the seconds column is the raw measurement and
fps is derived.

## Real hardware

Amiga 3000 + 68060, AMIX kernel `68060-260813-01`, node `solon`. Builds are
identical apart from the flag named; both render the same frames.

| Build | `ARCH` | Seconds | fps | |
|---|---|--:|--:|---|
| `quake-null` | 68020 | 495.5 | 2.0 | FSGLMUL/FSGLDIV, emulated by the FPSP |
| `quake-null` | 68040 | **137.1** | **7.1** | FSMUL/FSDIV, hardware — **3.6× faster** |
| `quake-va2000` | 68040 | 197.3 | 4.9 | display via libc `memcpy` |
| `quake-va2000` | 68040 | **165.8** | **5.8** | display via a 32-bit copy |

`quake-null` renders into a malloc'd buffer and displays nothing, so its number
is the CPU alone. Subtracting it gives what the board costs:

| | per frame | effective |
|---|--:|--:|
| rasterisation | 141 ms | |
| Zorro II copy, byte-wise | 62 ms | 1.0 MB/s |
| Zorro II copy, 32-bit | **29.6 ms** | **2.2 MB/s** |

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

### What Zorro III would be worth here

The display is now 17% of the frame, down from 30%. Extrapolating from the
measured copy rate:

| | copy | frame | fps | vs today |
|---|--:|--:|--:|--:|
| **today**, 32-bit copy over Zorro II | 29.6 ms | 170 ms | **5.8** | — |
| a perfect Zorro II copy (3.5 MB/s) | 18 ms | 159 ms | 6.3 | +0.5 |
| Zorro III (~15 MB/s) | 4 ms | 145 ms | 6.9 | **+1.1** |

**The VA2000 is both a Zorro II and a Zorro III card, depending on firmware.**
It runs in Zorro II mode here only because AMIX has no Zorro III support yet, so
kernel Zorro III work would apply to this board directly — roughly +1.1 fps, or
19%, on this benchmark.

What it cannot do is more than that: rasterisation is 141 ms of a 170 ms frame,
and no bus improvement touches it. Zorro III takes the display's share from 17%
to about 3%, which is close to the whole of what is available on that side. The
larger remaining win for this game is the rasteriser — and the AmigaOS port's
hand-written `d_scan_68k.s` already exists, in vasm syntax, for whoever converts
it to GNU as.

The proportions hold at higher resolutions, since Quake's rasterisation scales
with pixel count much as the copy does; the absolute milliseconds grow on both
sides.

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
