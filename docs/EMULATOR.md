# Testing under Amiberry

Amiberry has no VA2000 emulation, so the emulator can run the `VIDEO=null`
build only — the dedicated server and the pure-rasterisation benchmark. That is
exactly milestone 1. Anything with a picture has to go to real hardware.

The emulator harness belongs to the kernel port, in
`~/kehitys/amix-playground/kernelsupport`. Its `test-tools/README.md` is the
authority; what follows is the Quake-specific path through it.

## Boot

```sh
cd ~/kehitys/amix-playground/kernelsupport
sh emu-reset-boot.sh 040 /tmp/quake-emu-serial.log
```

`040` or `060` picks the CPU config. The script restores the golden disk image
first, so **every boot starts from a clean guest** — nothing you copied in
survives. It also stages whatever kernel is in `build/unix-040-dbg`; check that
this is the image you meant, its sha256 is recorded in `build/.emu-image`.

Boot takes a couple of minutes.

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
project's IPC helper, which talks to `/run/user/12044/amiberry.sock`:

```sh
python3 test-tools/sendkeys.py 'root' RET
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
python3 test-tools/tftp_onesock.py /tmp/quake-tftp     # serves UDP 1069
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
- Sustained heavy tftp plus repeated execs is the documented reproducer for the
  kernel port's ISSUE-10 corruption (`kernelsupport/KNOWN-ISSUES.md`), which
  once left this guest unresponsive on both console and network. If the guest
  wedges after a lot of file transfer, suspect that before suspecting Quake.

## Known-good reference points

Recorded so a future failure can be told apart from a moved baseline:

| | |
|---|---|
| Kernel | `68040-260812-06`, sha256 `955a5be7…`, the dual-silicon baseline |
| Guest | `UNIX_System_V amix 4.0 2.1c 0800430 Amiga (Unlimited) m68k` |
| Memory | `Available Unix memory = 30920704` (~30 MB), ample for Quake's 8 MB hunk |
| Disk | 816 MB filesystem, ~469 MB free |
| Login | `root`, no password |
