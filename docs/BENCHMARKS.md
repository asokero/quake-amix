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
destination is a Zorro II board, where every access is a bus cycle, and on a
16-bit bus a byte write costs the same cycle a word write would. Copying
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

| | copy | frame | fps |
|---|--:|--:|--:|
| today, 32-bit copy | 29.6 ms | 170 ms | 5.8 |
| a perfect Zorro II copy (3.5 MB/s) | 18 ms | 159 ms | 6.3 |
| Zorro III (~15 MB/s) | 4 ms | 145 ms | 6.9 |

So Zorro III is worth roughly **+0.6 fps over an ideal Zorro II copy** — the
141 ms of rasterisation dominates everything else, and no bus improvement
touches it.

**And it would not apply to this card anyway: the VA2000 is a Zorro II board.**
Kernel Zorro III support does not make a Zorro II card faster; it unlocks
Zorro III cards such as the ZZ9000. Worth doing for its own reasons — just not
as a Quake frame-rate measure.

The order of remaining wins for this game is therefore: the rasteriser first
(141 ms, and the AmigaOS port's hand-written `d_scan_68k.s` exists in vasm
syntax), then the bus.

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
