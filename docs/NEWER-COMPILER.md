# A newer compiler for AMIX

Status: **not finished, and further along than expected.** The whole project compiles and links
with GCC 14.2.0. What it does not yet do is start: AMIX's 1992 dynamic linker rejects the binary.

This matters because the compiler is the leading unmeasured suspect for the gap against the
AmigaOS port. The current toolchain is GCC **2.7.2.3** (1995); the AmigaOS port uses GCC 6.5
(2018). Rasterisation is 92% of a frame here, and the two obvious ways to attack it — hand-written
assembly and `-O2` — were both measured and both lost. A compiler is what is left.

## The compiler was already installed

`gcc-m68k-linux-gnu 4:14.2.0-1`, from Debian, alongside `binutils-m68k-linux-gnu 2.44`. It was
installed for the kernel port, whose `config.sh.example` explains why:

> GNU m68k binutils: `m68k-linux-gnu-{nm,objcopy,objdump,readelf,size,gcc}`. Used for symbol
> surgery, relocation inspection and the ELF checks.

That is, it was put there to *inspect* binaries. The compiler came with the package and sat unused.

## What works

**The ABI matches.** Floats return in `%fp0` and arguments go on the stack in both compilers —
checked before anything else, because a mismatch there would have ended the experiment:

```
GCC 2.7.2.3                   GCC 14.2.0
retf:                         retf:
    pea (%fp)                     fsmove.s 4(%sp),%fp0
    move.l %sp,%fp                fsmul.s 8(%sp),%fp0
    fsmov.s 8(%fp),%fp0           rts
    fsmul.s 12(%fp),%fp0
    unlk %fp
    rts
```

**All 75 files compile**, against AMIX's own 1992 headers, with:

```sh
m68k-linux-gnu-gcc -c -O2 -m68060 -std=gnu89 -fcommon -DAMIX -Isrc \
    -isystem $SYSROOT/usr/include -ffreestanding -fno-strict-aliasing -fno-PIC
```

* `-std=gnu89` — GCC 14 defaults to C23, which rejects the K&R declarations in AMIX's headers.
* `-fcommon` — GCC 10+ defaults to `-fno-common`, so Quake's tentative definitions (`viddef_t vid;`
  in more than one file) become duplicate symbols.
* `-m68060` is available, which GCC 2.7.2.3 does not support at all.

**The dependencies are all AMIX's own libc** — and notably *no* libgcc helpers, since m68k does
multiply, divide and floating point in hardware. `_xstat`, `__filbuf` and `__iob` appear in the
undefined list, which confirms AMIX's headers were the ones used.

**It links** against AMIX's `crt1.o`, `libc.so.1` and `libm.a`.

## What does not work yet

The binary segfaults before `main`, with the PC inside the dynamic linker (`0xC10xxxxx`), not in
program text (`0x80xxxxxx`). Two format differences were found and fixed; at least one remains.

| Difference | Fix | Result |
|---|---|---|
| Only `DT_GNU_HASH`; AMIX's loader knows only `DT_HASH` | `--hash-style=sysv` | crash moved to a different address in the loader |
| Extra `PT_GNU_RELRO` segment | `-z norelro` | still crashes |
| Extra `PT_GNU_STACK` segment | `-z nognustack` is *ignored* by this ld | untried — patch `p_type` to `PT_NULL` |

Dynamic tags and relocation types (`R_68K_COPY`, `R_68K_JMP_SLOT`) otherwise match the working
binary exactly.

## Two real bugs the old compiler never mentioned

gcc 14 diagnoses things gcc 2.7.2.3 cannot, and two of them are in **id's original source**, not
in anything this port added:

```
model.c:665   for (j=0 ; j<8 ; j++)  out->vecs[0][j] = ...     /* vecs is float[2][4] */
r_part.c:139  for (i=0 ; i<NUMVERTEXNORMALS*3 ; i++)  avelocities[0][i] = ...
                                                               /* vec3_t[162] */
```

Both walk a two-dimensional array as though it were flat. Both work, and both are undefined
behaviour — and gcc reports that it *detected* the overrun
(`iteration 4 invokes undefined behavior`), which is exactly the position from which an optimiser
is entitled to act on it. gcc 2.7.2.3 never performed the analysis, so the code has been safe by
accident for thirty years.

`tools/build-gcc14.sh` passes `-fno-aggressive-loop-optimizations`, which is the cheap fix and
leaves id's source alone. Casting to a flat pointer in those two loops would be the real one.

This is worth knowing beyond this experiment: a newer compiler will not just generate better code
for this codebase, it will also be entitled to assume things about it that the 1995 compiler never
assumed. Any mysterious rendering fault under gcc 14 should be checked against its warning output
before being blamed on the port.

## Where to resume

**Static linking works.** `tools/build-gcc14.sh` produces `build/quake-gcc14`, statically linked,
no undefined symbols, `PT_GNU_STACK` neutralised to `PT_NULL` so a 1992 loader has nothing
unfamiliar to interpret. It has never been run.

The remaining step is one `timedemo demo1` on the machine, against the **129.9 s / 7.5 fps**
recorded in `BENCHMARKS.md` for the same source under gcc 2.7.2.3. Compare a dumped frame first.

The dynamic-linking path, if anyone wants it: It removes the dynamic linker from the problem entirely
rather than negotiating with it. it still segfaults inside `ld.so` after
`--hash-style=sysv` and `-z norelro`, at a different address than before those. `PT_GNU_STACK`
remains the untried suspect there.

The one obstacle static linking did have is solved: `_lib_version`, referenced by libm's `atan2`
and `pow`, exists only in the *dynamic* `libc.so.1` and never in `libc.a`. It is SVR4 libm's
conformance-mode variable and the build script defines it.

## What it might be worth, and what to watch

Unknown, and static instruction counts do not predict it — GCC 14 emitted *more* instructions for
`D_DrawSpans8` (295 vs 245), which says nothing, since it unrolls and schedules where 2.7.2.3 does
neither. Only a run on hardware settles it.

Two things to check if it does run, before believing any number:

* **The frame comparison.** `-DVID_CHECKSUM -DVID_DUMP_FRAME=800` and a byte comparison against a
  known-good frame. A new compiler is exactly the circumstance where output could change.
* Whether the four assembler mismatches in `docs/TOOLCHAIN.md` are simply gone. They are artefacts
  of gcc 2.7.2.3 against gas 2.8.1; a self-consistent modern toolchain should not have them, which
  would make the wrapper's fix_asm unnecessary for this path.
