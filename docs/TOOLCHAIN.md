# AMIX cross toolchain notes

Everything here was established while getting Quake to build, on 2026-08-14.
Some of it is a fix to the toolchain itself, not to Quake, and belongs upstream
in `gcc-cross-amix`; those items say so.

The toolchain is `m68k-cbm-sysv4-gcc` — GCC **2.7.2.3** with binutils **2.8.1**,
built by the **gcc-cross-amix** project and installed here in `~/opt/amix-cross`.

```sh
export PATH=$HOME/opt/amix-cross/bin:$PATH
```

## 1. gcc 2.7.2.3 emits compare operands backwards — including float

**This is a toolchain bug, fixed in `amix-gcc-wrapper.sh`. It is the most
important thing in this document.**

GCC 2.7.2.3 prints compare operands in the opposite order from the one this gas
accepts. The wrapper already swapped integer compares (`cmp.b/w/l`); floating
point compares need exactly the same treatment and were not being swapped.

For `a < b`, gcc emits:

```
fmov.s 8(%fp),%fp0        | a -> fp0
fcmp.s %fp0,12(%fp)       | wrong order: destination must be the FPn
fslt %d0
```

Motorola syntax is `fcmp.<fmt> <source>,FPn`, computing `FPn - source`. The
correct form is `fcmp.s 12(%fp),%fp0`. Two different failure modes follow:

| Form | Count in a full Quake build | What happens |
|---|--:|---|
| `fcmp` against memory or an immediate | 422 | gas rejects it: `operands mismatch`. Loud, harmless. |
| `fcmp.x %fpN,%fpM` — register to register | **88** | **gas accepts it and the comparison sense is inverted.** Silent. |

The 88 silent ones are the reason this matters. Any float-heavy program built
with the unfixed toolchain has wrong-way comparisons in it and no diagnostic.

The swap is unconditional and safe: over a full Quake build, all 510 `fcmp`
instructions have `%fpN` as the first operand — there is no correctly-ordered
case to protect.

Arithmetic is **not** affected. `fsub.s 12(%fp),%fp0` and
`fsgldiv.s 12(%fp),%fp0` are emitted in the correct order; only compares are
reversed. This was verified separately, because a reversal in a non-commutative
operation would also have been silent.

### 1b. fsglmul / fsgldiv with a register source

Same wrapper, second rule. gcc emits the size suffix of the C type:

```
fsgldiv.s %fp0,%fp2       | rejected: a register source is extended precision
```

An FPn source is always extended, so gas wants `.x`. The value is unaffected —
these opcodes round the *result* to single precision regardless of how the
source is named. The wrapper rewrites `.s`/`.d` to `.x` for register-to-register
`fsglmul`/`fsgldiv`. ~130 instances in a Quake build; all fail loudly.

## 1c. `.swbeg` — every jump-table switch branches into mid-instruction

**The worst of the four. Silent, and it breaks ordinary control flow, not just
arithmetic.**

gcc 2.7.2.3 compiles a dense `switch` into a PC-relative jump table:

```
	mov.w .LD80(%pc,%d0.l*2),%d0
	jmp 6(%pc,%d0.w)         | base = (jmp+2) + 6 = jmp+8
	.swbeg &8                | SVR4 switch-table marker
.L80:
	.short .L75-.L80
	...
```

`.swbeg` is an SVR4 assembler directive. **This gas accepts it and emits
nothing**, so the table lands at `jmp+4` while the `jmp`'s hardcoded
displacement of 6 aims at `jmp+8`. Every case target is therefore four bytes
past where it should be — in the middle of an instruction. Execution runs off
into whatever those bytes decode as.

Proof, reduced:

```
	nop            | 2 bytes
	.swbeg &8
mark:                  | ends up at offset 2, not offset 6
```

The fix reserves the four bytes gcc counted on (`.swbeg &N` → `.long 0`). They
sit between an unconditional `jmp` and the table, so they are never executed.

21 jump tables in a Quake build, in 11 files — `pr_exec.c` (the QuakeC
interpreter's dispatch), `cl_parse.c` (network message dispatch), `menu.c`,
`sv_phys.c` and others. This is what made Quake die with SIGILL at a PC that
adb disassembled as `chk2.b -(%a4),%d2`, an instruction no C compiler emits.

## 1d. `tdivs.l` / `tdivu.l` — every 32-bit `%` is wrong

gcc emits `tdivs.l`/`tdivu.l` meaning DIVSL.L/DIVUL.L, the **32-bit-dividend**
divide that returns quotient and remainder. This gas assembles those mnemonics
as DIVS.L/DIVU.L, the **64-bit-dividend** form — extension word bit 10 set
instead of clear:

| written | encoding | bit 10 | dividend |
|---|---|:--:|---|
| `tdivs.l %d2,%d1:%d0` | `4c42 0c01` | 1 | 64-bit `%d1:%d0` |
| `divsl.l %d2,%d1:%d0` | `4c42 0801` | 0 | 32-bit `%d0` — what gcc means |

The 64-bit form reads the remainder register as the high half of the dividend,
and gcc leaves whatever it last computed there. The quotient then overflows 32
bits, the 68020 leaves the operands untouched on overflow, and the "remainder"
is that stale value.

Worked example, from the failure that exposed it — `anims[(j+1)%max]` with
`max`=7, where gcc had just put `2*(j+1)` in `%d1` for the preceding statement:

```
j=0: dividend 8589934593  quotient fits    -> real remainder 2   (still wrong)
j=1: dividend 17179869186 quotient OVERFLOW -> remainder = 4    (stale %d1)
j=2:                      OVERFLOW          -> 6
j=3..6:                   OVERFLOW          -> 8, 10, 12, 14
```

Observed indices were exactly 2, 4, 6, 8, 10, 12, 14. 17 occurrences in a Quake
build, in `console.c`, `draw.c`, `mathlib.c`, `model.c`, `r_light.c`,
`r_part.c`, `r_surf.c`, `snd_dma.c`.

## 1e. The assembler architecture was pinned to 68020, and it cost 4x

Not a miscompilation — a performance trap, and the largest single win found so
far.

`compile_one()` in the wrapper ran `as -m68020` regardless of what the compile
asked for. `-m68040` therefore failed to assemble, and the project ran on
68020 code generation without anyone choosing that.

At `-m68020`, gcc compiles single-precision multiply and divide to **FSGLMUL**
and **FSGLDIV**. Neither is implemented in the 68060's FPU. Every one of them
takes an unimplemented-instruction trap and is executed by the FPSP in
software. A Quake build contains **956** of them, in the renderer's hottest
arithmetic:

```
$ objdump -d quake-null | grep -cE 'fsglmul|fsgldiv'
956          # vs fmul 234, fdiv 80
```

At `-m68040` gcc emits **FSMUL** and **FSDIV** instead, which the 040 and the
060 both execute in hardware:

| flag | single-precision multiply | on 68060 |
|---|---|---|
| `-m68020` | `fsglmuls %fp@(12),%fp0` | traps to FPSP |
| `-m68040` | `fsmuls %fp@(12),%fp0` | hardware |

Measured on real 68060 silicon, `timedemo demo1` at 320x200, otherwise
identical builds — see `docs/BENCHMARKS.md`.

Passing the architecture through to the assembler needed three more mnemonic
fixes, all of which fail loudly:

| gcc emits | gas wants | why |
|---|---|---|
| `fsmul.s %fp1,%fp0` etc. | `.x` | a register source is extended precision; same rule as `fsgl*` above, now covering the whole FSxxx/FDxxx family |
| `fdmov.d` | `fdmove.d` | gas knows `fsmov` by the short name but not `fdmov` — an asymmetry in its opcode table |
| `fmovm.l %fpcr,%d0` | `fmovem.l` | from the float-to-int sequence; the suffixless register-list `fmovm` in prologues is fine |
| `mov &16,%d1` | `mov.l &16,%d1` | same sequence, setting FPCR round-to-zero; gcc omits the size |

**A 68040 binary does not run on a 68030.** `ARCH=68020` in the Makefile still
builds the compatible one, at the cost above.

## 2. The sysroot needs files the bootstrap does not install

The `gcc-cross-amix` sysroot ships `libc.so.1` and the crt objects. That is
enough for `hello.c` and not enough for anything using floating point. Copy
from a real AMIX installation — an unpacked distribution tree, or the files
copied off a running machine:

```sh
SYSROOT=$HOME/opt/amix-cross/m68k-cbm-sysv4/sysroot
AMIXTREE=/path/to/amix            # a real AMIX root
cp $AMIXTREE/usr/ccs/lib/libm.a $SYSROOT/usr/lib/
```

**These belong in the `sysroot` target upstream.**

## 3. libamixcompat.a — what AMIX ships only statically

AMIX's dynamic `libc.so.1` does not export everything its static `libc.a` does.
Verified by attempted link, which is the only reliable method here: `nm -D` and
`readelf --dyn-syms` both fail to read this vintage of ELF shared object.

| Symbol | In `libc.so.1`? | Notes |
|---|---|---|
| `sysconf`, `poll` | yes | SVR4 natives — prefer these |
| `gettimeofday` | **no** | needed by `Sys_FloatTime` |
| `select` | **no** | AMIX's static `select()` is itself a wrapper over `poll()` |
| `usleep`, `getpagesize` | **no** | BSD; `libucb.a` also has them |
| `syslog`, `seteuid` | **no** | needed by `libsocket.so`, see §4 |
| `_fpsetround`, `_fpsetsticky` | **no** | **`libm.a` needs these** — nothing links without them |

Rather than link `libucb.a` — which overrides libc functions with BSD semantics
wholesale — extract the specific objects. All are leaf syscall or register
wrappers with no libc internals, so mixing them with the dynamic libc is safe:

```sh
LIBCA=vanillarw/usr/ccs/lib/libc.a
mkdir /tmp/compat && cd /tmp/compat
for m in fp_data.o fpgetmask.o fpgetrnd.o fpgetsticky.o \
         fpsetrnd.o fpsetsticky.o fpsetmask.o \
         gettimeofday.o hrtsys.o syscall.o cerror.o; do
    m68k-cbm-sysv4-ar p $LIBCA $m > $m
done
m68k-cbm-sysv4-ar rcs libamixcompat.a *.o
cp libamixcompat.a $SYSROOT/usr/lib/
```

The dependency chain that makes `gettimeofday` work is
`gettimeofday.o` → `_hrtcntl` (`hrtsys.o`) → `_syscall` (`syscall.o`) →
`_cerror` (`cerror.o`), ending at `errno`, which is dynamic.

`libamixcompat.a` must be linked **last**, after `-lm`, so it can satisfy libm.

## 4. Two wrapper behaviours to know

**Passing any `-l` drops the implicit libc.** The wrapper only defaults to
`-lc` when no `-l` at all was given, so `-lm` alone fails on `atexit` and
`_environ` from crt1.o. Always spell out `-lc`:

```sh
m68k-cbm-sysv4-gcc foo.c -o foo -lm -lc -lamixcompat
```

**The assembler is hardcoded to `-m68020`.** `compile_one()` runs
`as -m68020`, so `-m68040` fails on 040-only opcodes (`fsmov`, `fdmov`). This
is not a problem for this project — a 68020+68881 binary runs on the A3000's
030 and on the 040/060 cards alike, which is exactly what we want — but it does
mean CPU-specific tuning is not available without changing the wrapper.

## 5. Sockets are present but do not link yet

AMIX has BSD sockets: `libsocket.so`, `libnsl.so`, `<sys/socket.h>`,
`<netinet/in.h>`. `src/net_udp.c` compiles unchanged apart from `<sys/filio.h>`
for `FIONBIO`/`FIONREAD` (the same `#ifdef __sun__` path id already had — AMIX
is SVR4 too).

The blocker is on the library side: `libsocket.so` refers to `select`, `syslog`
and `seteuid`, none of which `libc.so.1` exports. Deferred rather than solved —
see `src/net_amix.c` for the reasoning and what restoring UDP involves.
