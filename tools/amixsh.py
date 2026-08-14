#!/usr/bin/env python3
"""amixsh.py -- minimal telnet client for the AMIX guest under Amiberry slirp.

Python 3.13 dropped telnetlib, and the guest's telnetd opens with option
negotiation that a raw socket write cannot ignore.  This refuses every option
(WONT to DO, DONT to WILL), which is enough to reach a login prompt.

  amixsh.py 'cmd1' 'cmd2' ...     log in as root, run each command, print output

Reads until the shell prompt goes quiet rather than matching on '#', because
Quake's own output contains '#'.
"""
import os, socket, sys, time

HOST = os.environ.get("AMIX_HOST", "127.0.0.1")
# 2325 is this project's guest; the kernel work uses 2323 and wolf3d 2324.
PORT = int(os.environ.get("AMIX_PORT", "2325"))
IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240


def negotiate(sock, data):
    """Strip telnet control sequences, refusing every option."""
    out = bytearray()
    i = 0
    while i < len(data):
        b = data[i]
        if b != IAC:
            out.append(b)
            i += 1
            continue
        if i + 1 >= len(data):
            break
        cmd = data[i + 1]
        if cmd in (DO, DONT) and i + 2 < len(data):
            sock.sendall(bytes([IAC, WONT, data[i + 2]]))
            i += 3
        elif cmd in (WILL, WONT) and i + 2 < len(data):
            sock.sendall(bytes([IAC, DONT, data[i + 2]]))
            i += 3
        elif cmd == SB:
            j = data.find(bytes([IAC, SE]), i)
            i = len(data) if j < 0 else j + 2
        else:
            i += 2
    return bytes(out)


def drain(sock, quiet=1.5, limit=60):
    """Collect output until the link stays quiet for `quiet` seconds."""
    buf = bytearray()
    last = time.time()
    sock.settimeout(0.5)
    while time.time() - last < quiet and time.time() - last < limit:
        try:
            chunk = sock.recv(4096)
            if not chunk:
                break
            buf += negotiate(sock, chunk)
            last = time.time()
        except socket.timeout:
            pass
    return bytes(buf)


def main():
    cmds = sys.argv[1:]
    s = socket.create_connection((HOST, PORT), timeout=15)
    drain(s, quiet=2.0)
    s.sendall(os.environ.get("AMIX_USER", "root").encode() + b"\r\n")
    banner = drain(s, quiet=3.0)
    if b"assword" in banner:
        # Password from the environment only.  The emulator guest has none;
        # the real machine does, and it does not belong in this repository.
        s.sendall(os.environ.get("AMIX_PASS", "").encode() + b"\r\n")
        banner += drain(s, quiet=3.0)
    sys.stdout.write(banner.decode("latin1", "replace"))
    for c in cmds:
        s.sendall(c.encode() + b"\r\n")
        # Quake prints for a while; allow a long quiet window before giving up.
        out = drain(s, quiet=float(os.environ.get("AMIX_QUIET", "4.0")),
                    limit=float(os.environ.get("AMIX_LIMIT", "120")))
        sys.stdout.write(out.decode("latin1", "replace"))
        sys.stdout.flush()
    # Log out properly.  Dropping the socket instead leaves the login session
    # and its pty behind; enough of those and the guest stops accepting
    # connections on the console as well as over the network.
    try:
        s.sendall(b"exit\r\n")
        drain(s, quiet=1.0, limit=10)
    except OSError:
        pass
    s.close()


if __name__ == "__main__":
    main()
