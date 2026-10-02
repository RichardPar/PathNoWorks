#!/usr/bin/env python3
"""A stand-in X server and client for x11_smoke.sh.

    x11_fake.py server SOCKET      answer each connection's setup with the
                                   login it carried, then echo
    x11_fake.py xauth FILE COOKIE  write an .Xauthority with one wild entry
    x11_fake.py client SOCKET COOKIE
                                   connect, check the login the server saw,
                                   then round-trip 200 KB
    x11_fake.py refused SOCKET     connect and expect to be dropped
    x11_fake.py probe SOCKET       exit 0 if something is listening

A SOCKET is a path, or tcp:PORT on this machine: Python has no Unix
sockets on Windows, where X goes over TCP anyway.
"""
import os, socket, struct, sys, threading

def endpoint (where):
    if where.startswith ("tcp:"):
        return socket.AF_INET, ("127.0.0.1", int (where[4:]))
    return socket.AF_UNIX, where        # not on Windows: there is no AF_UNIX

def connect (where):
    family, addr = endpoint (where)
    s = socket.socket (family); s.settimeout (20); s.connect (addr)
    return s

def read_exact (s, n):
    b = b""
    while len (b) < n:
        c = s.recv (n - len (b))
        if not c: raise EOFError ("short read")
        b += c
    return b

def pad (n): return (n + 3) & ~3

def server (path):
    family, addr = endpoint (path)
    if family != socket.AF_INET and os.path.exists (path): os.unlink (path)
    l = socket.socket (family); l.bind (addr); l.listen (8)
    def one (c):
        try:
            hdr = read_exact (c, 12)
            fmt = ">" if hdr[0:1] == b"B" else "<"
            nlen, dlen = struct.unpack (fmt + "HH", hdr[6:10])
            rest = read_exact (c, pad (nlen) + pad (dlen))
            name = rest[:nlen]; data = rest[pad (nlen):pad (nlen) + dlen]
            c.sendall (b"AUTH " + name + b" " + data.hex ().encode () + b"\n")
            while True:
                b = c.recv (65536)
                if not b: break
                c.sendall (b)
        except Exception:
            pass
        c.close ()
    while True:
        c, _ = l.accept ()
        threading.Thread (target=one, args=(c,), daemon=True).start ()

def xauth (path, cookie):
    def f (b): return struct.pack (">H", len (b)) + b
    with open (path, "wb") as out:
        out.write (struct.pack (">H", 65535) + f (b"") + f (b"") +
                   f (b"MIT-MAGIC-COOKIE-1") + f (bytes.fromhex (cookie)))

def setup ():
    # Little-endian, protocol 11.0, a login the bridge must replace.
    name = b"XDM-AUTHORIZATION-1"; data = b"rubbish!"
    return (b"l\0" + struct.pack ("<HHHHH", 11, 0, len (name), len (data), 0) +
            name + b"\0" * (pad (len (name)) - len (name)) +
            data + b"\0" * (pad (len (data)) - len (data)))

def client (path, cookie):
    s = connect (path)
    s.sendall (setup ())
    line = b""
    while not line.endswith (b"\n"): line += read_exact (s, 1)
    want = b"AUTH MIT-MAGIC-COOKIE-1 " + cookie.encode () + b"\n"
    if line != want:
        sys.exit ("FAIL login: server saw %r, wanted %r" % (line, want))
    payload = bytes ((i * 7) & 255 for i in range (200000))
    s.sendall (payload)
    back = read_exact (s, len (payload))
    if back != payload: sys.exit ("FAIL echo: data changed on the way")
    print ("ok    login replaced, 200000 bytes round trip")

def refused (path):
    s = connect (path)
    try:
        s.sendall (setup ())
        got = s.recv (100)
    except socket.timeout:
        sys.exit ("FAIL refused: still connected")
    except (ConnectionResetError, BrokenPipeError):
        # Closed with our setup unread: that is a refusal too.
        got = b""
    if got: sys.exit ("FAIL refused: got data %r" % got)
    print ("ok    a node not allowed is turned away")

def probe (path):
    try:
        connect (path).close ()
    except OSError:
        sys.exit (1)

if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "server": server (sys.argv[2])
    elif cmd == "xauth": xauth (sys.argv[2], sys.argv[3])
    elif cmd == "client": client (sys.argv[2], sys.argv[3])
    elif cmd == "refused": refused (sys.argv[2])
    elif cmd == "probe": probe (sys.argv[2])
