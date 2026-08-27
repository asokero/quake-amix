# Testing under Amiberry

Amiberry has no VA2000 emulation, so the emulator can run the `VIDEO=null`
build only — the dedicated server and the pure-rasterisation benchmark. That is
exactly milestone 1. Anything with a picture has to go to real hardware.

The emulator harness is not part of this repository — it belongs to the AMIX
68040/68060 **kernel port** project, and this file assumes that checkout sits
beside this one. `$KERNELSUPPORT` below stands for it, and its
`test-tools/README.md` is the authority; what follows is only the Quake-specific
path through it.

## Boot

This project has **its own Amiberry configuration, its own kernel copy and its
own disk image**, and its own boot script:

```sh
sh tools/emu-quake.sh              # boot from the golden image (guest is wiped)
sh tools/emu-quake.sh keep         # boot the existing disk, contents intact
sh tools/emu-quake.sh snapshot     # make the current disk the new golden
```

The script's paths default to one working setup; `AMIBERRY_HD`, `AMIBERRY_CONF`
and `AMIBERRY_BIN` override them without editing it.

Do **not** use the kernel project's `emu-reset-boot.sh` here. It stages
whatever sits in `kernelsupport/build/unix-040-dbg` and restores that project's
golden image, so a benchmark run writes into the kernel work's files — during
this port's bring-up it twice booted a kernel that had been rebuilt underneath
it, once a diagnostic build that would have invalidated any timing.

| | this project | kernel work | wolf3d |
|---|---|---|---|
| config | `a3000ux060-amixquake.uae` | `a3000ux*.uae` | `a3000ux-wolf3d.uae` |
| serial | TCP 1236 | 1234 | 1235 |
| telnet | localhost **2325** | 2323 | 2324 |
| disk | `amix_hardfileX11R5-quake.hdf` | `…X11R5.hdf` | `…X11R5-wolf3d.hdf` |
| kernel | `emu/kernel/unix-040-dbg` | `kernelsupport/build/…` | — (stock 030) |

The kernel in `emu/kernel/` is a copy of `unix-040-quiet` under the name the
guest's startup-sequence loads (`unix-040-dbg`). **Use a quiet build**: the
diagnostic ones print on every page fault, which is not something to measure
through. Update the copy deliberately when you want to test a newer kernel, and
record which one a result came from.

The only file shared with the kernel project is the AmigaOS boot volume
`HD0-MAIN1-060-os32.hdf`, which that project's own three configs already share.

### Game data, once

`tools/emu-quake.sh snapshot` is the answer to re-transferring an 18.7 MB
`pak0.pak` after every boot. Get the data into the guest, shut it down cleanly
(`sync; sync; haltsys`), snapshot, and every later `emu-quake.sh` starts with
the data already there. The golden is a sparse copy — 1 GB apparent, well under
half that on disk.

### The serial log will not tell you the machine is up

`emu-reset-boot.sh` captures serial to a log, but **the kernel's `login:` prompt
goes to the Amiga console, not to serial**. The log stops after the bootloader's
`image checksum = ...` line and stays there on a perfectly healthy boot. Waiting
for `login:` in it never returns.

Take a screenshot of the Amiberry window instead:

```sh
DISPLAY=:1 import -window $(DISPLAY=:1 xdotool search --name Amiberry | head -1) /tmp/emu.png
```

The kernel-side signal for "fully booted" is the `mainmarks.s` idle-hook proc
dump in the serial log — see `test-tools/README.md`.

## Console input

`xdotool` does not work: Amiberry is an SDL application and ignores
`XSendEvent`, and XTEST keystrokes are unreliable against it. Use the kernel
project's IPC helper, which talks to Amiberry's socket under
`$XDG_RUNTIME_DIR/amiberry.sock`:

```sh
python3 $KERNELSUPPORT/test-tools/sendkeys.py 'root' RET
```

**The guest console keymap is German.** `sendkeys.py` translates for it; typing
raw US positions garbles every special character. Log in as `root`, no password.

## Getting a real shell

Console typing plus screenshots is unusable for anything that prints as much as
Quake. Use telnet, which gives text straight back. Two obstacles:

1. Inbound telnet through slirp stalls until the guest has sent one packet
   outward. On the console, first: `ping 10.0.2.2`.
2. Python 3.13 removed `telnetlib`, and the guest's telnetd opens with option
   negotiation that a raw socket cannot ignore.

`tools/amixsh.py` in this repository handles both — it refuses every telnet
option, logs in as root, runs each argument as a command and prints the output:

```sh
python3 tools/amixsh.py 'uname -a' 'df -k'
```

It defaults to port **2325**, this project's guest. `AMIX_PORT` and `AMIX_HOST`
override it — `AMIX_PORT=2323` reaches the kernel project's guest, if one is
running.

It reads until the link goes quiet rather than matching a prompt, because
Quake's own output contains `#`. Tune with `AMIX_QUIET` (seconds of silence
before moving on, default 4) and `AMIX_LIMIT` (hard cap, default 120). A command
that prints nothing for longer than `AMIX_QUIET` — a long file transfer — will
be left running in the guest while the script moves on; that is usually what you
want, just poll for the result afterwards.

## Getting files in

TFTP, served from the host by the kernel project's slirp-compatible server (the
stock one stalls because slirp drops replies from an ephemeral port):

```sh
# host
python3 $KERNELSUPPORT/test-tools/tftp_onesock.py /tmp/quake-tftp   # serves UDP 1069
```

```sh
# guest, via amixsh.py
(echo binary; echo "get quake /quake/quake"; echo quit) | tftp 10.0.2.2 1069
```

Measured throughput is about **62-115 KB/s**, so the 369 KB binary takes 6
seconds.

**A single transfer cannot exceed 16,776,704 bytes.** AMIX's 1992-vintage tftp
client treats the TFTP block number as a *signed* 16-bit value, so it stops
acknowledging at block 32767 and the transfer times out with the file exactly
`32767 * 512` bytes long. `pak0.pak` is 18.7 MB, so it has to be split:

```sh
split -b 9000000 pak0.pak pakpart.          # host
```

```sh
for p in aa ab ac; do                        # guest
    (echo binary; echo "get pakpart.$p /quake/pakpart.$p"; echo quit) | tftp 10.0.2.2 1069
done
cat /quake/pakpart.aa /quake/pakpart.ab /quake/pakpart.ac > /quake/id1/pak0.pak
```

Quake verifies the result itself: `Added packfile ./id1/pak0.pak (339 files)`
means the directory survived reassembly.

**Run long transfers under `nohup`.** They outlive the telnet session that
started them; without it, closing the session SIGHUPs the transfer partway.

**Put game data under `/`, not `/tmp`** — AMIX clears `/tmp` on every boot. This
project uses `/quake` and `/quake/id1`.

### Game data across reboots

`pak0.pak` is 18.7 MB and the golden image is restored on every
`emu-reset-boot.sh`, so a naive loop re-transfers it every time. Once the guest
has a copy, snapshot the disk into a Quake-specific golden image and point the
harness at that — the kernel project already does this for wolf3d
(`amix_hardfileX11R5-wolf3d.hdf` next to the standard one).

## Running Quake

```sh
python3 tools/amixsh.py 'cd /quake; nohup ./quake -dedicated > /quake/run.log 2>&1 </dev/null &'
python3 tools/amixsh.py 'ps -ef | grep quak | grep -v grep'
```

`nohup` is not optional: the dedicated server never exits on its own, so
whatever started it will still be running when the telnet drain gives up and
disconnects — and that SIGHUPs it. A run that "crashes" without printing an
exit code is almost always this and not a real crash.

Two more things to know when reading the output:

- **stdout is block-buffered.** `run.log` will look like it stopped mid-load
  while the process is alive and well; the tail of the buffer is only lost if
  the process dies. Judge liveness from `ps`, not from the log.
- **Heavy disk I/O makes the guest look dead, and it isn't.** Copying the
  18.7 MB `pak0.pak` into place saturates the emulated machine for minutes:
  telnet connections are closed as soon as they are opened, and the console
  stops echoing. It comes back on its own once the copy finishes. During this
  port's bring-up that was mistaken three times for a kernel wedge — including
  a confident but wrong attribution to the kernel port's ISSUE-10 — before the
  simple test settled it: type into the Amiberry window, and if it responds
  later, nothing was ever broken. Judge liveness that way, and be patient
  rather than reaching for a diagnosis.

## Known-good reference points

Recorded so a future failure can be told apart from a moved baseline:

| | |
|---|---|
| Kernel | `68040-260812-06`, sha256 `955a5be7…`, the dual-silicon baseline |
| Guest | `UNIX_System_V amix 4.0 2.1c 0800430 Amiga (Unlimited) m68k` |
| Memory | `Available Unix memory = 30920704` (~30 MB), ample for Quake's 8 MB hunk |
| Disk | 816 MB filesystem, ~469 MB free |
| Login | `root`, no password |
