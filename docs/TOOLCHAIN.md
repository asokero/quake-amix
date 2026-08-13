# AMIX cross toolchain notes

Everything here was established while getting Quake to build, on 2026-08-14.
Some of it is a fix to the toolchain itself, not to Quake, and belongs upstream
in `gcc-cross-amix`; those items say so.

The toolchain is `m68k-cbm-sysv4-gcc` — GCC **2.7.2.3** with binutils **2.8.1**,
from `~/kehitys/amix-playground/gcc-cross-amix`, installed in `~/opt/amix-cross`.

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

## 2. The sysroot needs files the bootstrap does not install

The `gcc-cross-amix` sysroot ships `libc.so.1` and the crt objects. That is
enough for `hello.c` and not enough for anything using floating point. Copy
from a real AMIX tree (here: `~/kehitys/amix-playground/vanillarw`):

```sh
SYSROOT=$HOME/opt/amix-cross/m68k-cbm-sysv4/sysroot
cp vanillarw/usr/ccs/lib/libm.a $SYSROOT/usr/lib/
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
