# A newer compiler for AMIX

**It works, and it is worth 22.6%.** Quake built with GCC 14.2.0 runs on AMIX and completes
`timedemo demo1` in 105.9 s against 129.9 s for the same source under GCC 2.7.2.3 — and renders
frame 800 byte-for-byte identically. `tools/build-gcc14.sh` builds it.

That confirms what was the leading unmeasured suspect for the gap against the AmigaOS port. The
current toolchain is GCC 2.7.2.3 (1995); the AmigaOS port uses GCC 6.5 (2018). Rasterisation is
92% of a frame here, and both other ways to attack it — hand-written assembly and `-O2` — had
already been measured and lost.

Four obstacles had to be cleared, and every one of them presented as something other than what it
was. They are documented below in the order they were hit, because the *symptoms* are the useful
part for anyone doing this again.

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

## The four obstacles

### 1. `DT_GNU_HASH` — crash inside the dynamic linker

A dynamically linked binary segfaulted before `main`, with the PC at `0xC10xxxxx`: inside AMIX's
1992 `ld.so`, not in program text. The binary carried only `DT_GNU_HASH`; that loader knows only
`DT_HASH`. `--hash-style=sysv` moved the crash to a different address in the loader rather than
removing it, and `-z norelro` did not finish the job either.

**Static linking removes the loader from the problem instead of negotiating with it**, and is what
the build script does. `PT_GNU_STACK` is patched to `PT_NULL` afterwards, since `ld -z nognustack`
is silently ignored by this binutils and a 1992 loader has no reason to know the type.

One obstacle static linking has of its own: `_lib_version`, referenced by libm's `atan2` and
`pow`, exists only in the *dynamic* `libc.so.1` and never in `libc.a`. It is SVR4 libm's
conformance-mode variable; the build script defines it.

### 2. `va_start` — a program that ran and printed nothing

With the loader out of the way, Quake started, initialised completely, reached its main loop —
and printed not one character, while raw `write(2, ...)` worked fine.

AMIX's own `<stdarg.h>` defines:

```c
typedef void *va_list;
#define va_start(list, name) (list = (void *)((char *)&name + sizeof(name)))
```

That takes the address of the last named parameter and assumes the varargs follow it in the stack
frame. True for the 1992 compiler, which spilled every parameter to its stack home. GCC 14 keeps
it in a register — which is exactly why modern compilers require `__builtin_va_start`. Quake
builds every line it prints with `vsprintf`, so the result was silence at `-O0` and a crash inside
`strlen` at `-O2`.

Fix: `-nostdinc`, then **gcc's own header directory first** and AMIX's after, so `stdarg.h` and
`stddef.h` come from gcc and everything else from AMIX. `-nostdinc` also keeps the host's glibc
headers out, which would otherwise be searched and are wrong for this target in every respect.

### 3. `-malign-int` — the one that matters most

Even with varargs fixed, `putc` and `fputc` still wrote nothing. The FILE struct read back as all
zeros:

```
sizeof(FILE)=14   offset _cnt=0  _ptr=4  _base=8
stdout->_file=0   stdout->_flag=0        <- should be 1 and non-zero
```

`__iob` is in libc's `.data` and is **320 bytes**. 320/16 = 20 entries exactly; 320/14 is not an
integer. AMIX's libc lays FILE out as 16 bytes; gcc 14 was laying it out as 14, because **gcc
defaults to 2-byte alignment for `int` and pointer members on m68k while AMIX's compiler used 4**.
So `stdout`, which is `&__iob[1]`, pointed two bytes into the first entry.

`-malign-int` is therefore not a tuning flag but *the* ABI flag. Without it every struct shared
with libc is laid out differently — `FILE`, `stat`, `timeval`, all of them — and the failure is
silent.

### 4. Two real bugs in id's source



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

## What is left

* **Measure the VA2000 build.** Only `VIDEO=null` has been timed under gcc 14. CPU 105.9 s plus
  the measured 11.1 s of Zorro III frame copy extrapolates to about 8.3 fps against 6.9 today, but
  that is arithmetic, not a measurement.
* **Decide whether this becomes a supported path.** `make` is still the supported build and still
  uses gcc 2.7.2.3. Making the modern compiler the default means, at minimum: validating more than
  one program, deciding static versus dynamic, and packaging the flags so nobody has to know them.
* **It cannot replace gcc 2.7.2.3 for the kernel.** `gcc-cross-amix` also builds kernel and driver
  objects, which need `-traditional` — removed from gcc long ago — and must satisfy AMIX's own
  linker. Two toolchains, one for each job, is the shape of this.
* Whether a gcc configured for `m68k-cbm-sysv4` proper is even possible is unknown; the m68k SVR4
  targets appear to have been dropped from gcc long ago, which would mean writing a target config.
  None of that was needed here: Debian's stock `m68k-linux-gnu` compiler pointed at AMIX's headers
  and libraries is enough.

## Why it is worth having even apart from the speed

* **The four assembler mismatches in `docs/TOOLCHAIN.md` simply do not exist on this path.**
  `.swbeg`, `tdivs.l`, reversed `fcmp` and the `fsgl*` suffixes are all artefacts of gcc 2.7.2.3
  disagreeing with gas 2.8.1. A self-consistent modern toolchain has none of them, and three of
  those four were silent.
* **Diagnostics.** It found two genuine undefined-behaviour sites in id's own source that the 1995
  compiler cannot see.
* `-m68060` exists, which gcc 2.7.2.3 does not support at all.
