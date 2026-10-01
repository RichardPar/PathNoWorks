# PathNoWorks

> *Pathworks, but it works. Mostly. On Linux.*

Back in the day, if you had a PC on a DEC site you probably had Pathworks
on it: DECnet for your desktop, so the PC could see the VAXen down the
hall, copy files off them, mount their disks as drive letters and `SET
HOST` into them when nobody was looking. Then the PCs won, the VAXen went
quiet, and in 6.1 Linux dropped what was left of its in-kernel DECnet.

PathNoWorks is my attempt to get that desk back. It's a set of C++20
tools, and a Qt desktop on top, that talk DECnet Phase IV to VMS, RSX,
PyDECnet and the rest of HECnet. You get network management, file
access, a network drive, remote login, LAT terminals, mail and MOP, all
from an ordinary Linux machine.

The DECnet node itself is [cppdecnet](../Decnet/cppdecnet)'s `decnetd`.
The tools don't touch the network at all; they ask decnetd to do it for
them over its API socket, so none of them needs root. The one rebel is
`pnw-lat`: LAT isn't DECnet, so it goes straight onto the LAN by itself.

Here's how the pieces fit together:

```
┌────────────────────────── your Linux machine ──────────────────────────┐
│                                                                        │
│   pathnoworks        pnw-ncp      pnw-dir  pnw-type  pnw-copy          │
│   (the Qt desktop)   pnw-sethost  pnw-delete  pnw-rename               │
│                      pnw-mail     pnw-mop     pnw-fs (FUSE)            │
│          │                │                                            │
│          └──────┬─────────┘                                            │
│                 │  the pnwclient library (+ pnwdap, pnwcterm, ...)     │
│                 │                                                      │
│                 ▼  JSON lines on a Unix socket                         │
│         /tmp/decnetapi.sock   (decnetd's "api" line)                   │
│                 │                                        pnw-lat       │
│ ┌────────────── decnetd (cppdecnet) ───────────────┐        │          │
│ │ session control   objects: NML 19, MIRROR 25,    │        ▼          │
│ │                   FAL 17 (dnfal), your own...    │   pnw-latsock     │
│ │ NSP               logical links                  │  (CAP_NET_RAW)    │
│ │ routing           endnode or router              │        │          │
│ │ MOP               on --mop Ethernet circuits     │        │          │
│ │ circuits          Ethernet (pcap/TAP),           │        │          │
│ │                   Multinet (TCP), DDCMP          │        │          │
│ └────────────────────────┬─────────────────────────┘        │          │
└──────────────────────────┼──────────────────────────────────┼──────────┘
                           │                                  │
                    DECnet Phase IV                      LAT (0x6004)
              VMS · RSX · PyDECnet · HECnet       terminal hosts on the LAN
```

Every tool except `pnw-lat` is a client of decnetd. It opens the socket,
asks for a logical link to an object on some node, and then exchanges
data over that link. decnetd does the DECnet part (routing, NSP, session
control) and owns the circuits to the outside world. MOP is a little
different, since it isn't a logical link at all: `pnw-mop` asks decnetd,
and decnetd speaks MOP itself on its Ethernet circuits.

| Tool | Talks to, through decnetd | Protocol |
|---|---|---|
| `pnw-ncp` | NML (object 19), MIRROR (object 25) | NICE, loopback |
| file tools, `pnw-fs`, the desktop's file windows | FAL (object 17) | DAP |
| `pnw-sethost`, the desktop's Terminal | CTERM (object 42) | CTERM |
| `pnw-mail`, the desktop's Mail | MAIL (object 27); `listen` takes object 27 here | Mail-11 |
| `pnw-mop` | stations on decnetd's `--mop` Ethernet circuit | MOP |
| `pnw-lat` | nothing: LAT hosts directly, through `pnw-latsock` | LAT |

The socket is the only door, so it's also the only lock: whoever can open
it can act as this DECnet node. Set its mode accordingly (`--mode 600` or
`660`).

## What's in the box

| Tool | What it does | Guide |
|---|---|---|
| `pathnoworks` | The desktop: the network, a node's files (browse, copy both ways, mount), terminals and mail | [desktop](docs/desktop.md) |
| `pnw-ncp` | NCP: `SHOW` and `LIST` network information from any node, `LOOP NODE` | [pnw-ncp](docs/pnw-ncp.md) |
| `pnw-dir` | List remote files | [file access](docs/file-access.md) |
| `pnw-type` | Show a remote file | [file access](docs/file-access.md) |
| `pnw-copy` | Copy files to and from remote nodes, wildcards included | [file access](docs/file-access.md) |
| `pnw-delete` | Delete remote files | [file access](docs/file-access.md) |
| `pnw-rename` | Rename a remote file | [file access](docs/file-access.md) |
| `pnw-fs` | Mount a remote directory as a local one (FUSE) | [pnw-fs](docs/pnw-fs.md) |
| `pnw-sethost` | Log in to a node, as `SET HOST` does (CTERM); VT300/VT340 and Sixel through your terminal | [pnw-sethost](docs/pnw-sethost.md) |
| `pnw-lat` | Connect to a LAT service on the LAN, as a terminal server does | [pnw-lat](docs/pnw-lat.md) |
| `pnw-mail` | Send DECnet mail to `NODE::USER`, and take in mail for this node (Mail-11) | [pnw-mail](docs/pnw-mail.md) |
| `pnw-mop` | MOP: stations on the LAN, their system IDs and counters, loop tests | [pnw-mop](docs/pnw-mop.md) |

Want other nodes to reach files on *this* machine? cppdecnet has `dnfal`,
a FAL with its own access control. See "File access" in the cppdecnet
README.

New here? [Getting started](docs/getting-started.md) takes you from
building it to copying your first file.

## A quick look

```sh
export DECNETAPI=/run/decnet/api.sock        # decnetd's "api" socket

pnw-ncp show known nodes
pnw-ncp tell MIM show executor characteristics
pnw-ncp loop node MIM count 5

pnw-dir  'VMSNOD"user password"::SYS$LOGIN:*.COM'
pnw-copy 'MIM::HECNET.DAT' .
pnw-copy *.txt 'VMSNOD"user password"::[USER]'

pnw-fs 'VMSNOD"user password"::DUA0:[USER]' ~/vms
fusermount3 -u ~/vms

pnw-sethost VMSNOD
xterm -ti vt340 -e pnw-sethost VMSNOD          # with Sixel graphics

pnw-lat -l                                      # LAT services on the LAN
pnw-lat BAJI

echo Hello | pnw-mail send -s Greetings VMSNOD::SYSTEM
pnw-mail listen                                 # mail for this node -> ~/Mail/decnet

pnw-mop list                                    # stations on the LAN (MOP)
pnw-mop loop BAJI -n 5
```

A word of warning you'll see again: **quote remote file specifications.**
The shell has strong opinions about `[ ] * ; " $`, and VMS file names are
full of them.

## Building it

You'll need CMake 3.20+, GCC 13+ or Clang 16+, and cppdecnet, which by
default lives next door at `../Decnet/cppdecnet`. CMake builds cppdecnet
for you with its own Makefile. `pnw-fs` also wants FUSE 3 (`fuse3` and
`libfuse3-dev`) and the desktop wants Qt 6 (`qt6-base-dev`). Leave either
out and you just don't get that piece; everything else still builds.

```sh
cmake -S . -B build              # -DCPPDECNET_DIR=/path/to/cppdecnet
cmake --build build
ctest --test-dir build
```

The tests are the real thing, not mocks. They start actual decnetd nodes
on your machine and drive the tools against them:

- NCP;
- file access, against both `dnfal` and PyDECnet's `fal.py`;
- access control;
- the FUSE mount;
- mail from one node to another;
- MOP;
- and the desktop's windows, driven offscreen.

The PyDECnet tests are skipped if PyDECnet isn't at
`../Decnet/pydecnet/pydecnet` (`-DPYDECNET_DIR=...`), and the mount test
is skipped without FUSE.

`-DCPPDECNET_FLAVOUR=debug` links cppdecnet's sanitizer build and builds
PathNoWorks with the sanitizers too. Slow, but it catches things.

## Where it's at

Everything above has been tested against OpenVMS VAX 6.2 (DECnet-VAX 6.1)
as well as cppdecnet and PyDECnet:

- NCP;
- directories, and text and binary copies in both directions;
- rename and delete;
- the FUSE mount;
- CTERM logins;
- mail both ways.

RSX has been tried for NCP, loopback, CTERM and LAT (up to login).

Still on the list:

- the MOP console carrier (a remote console on a DECserver or VAX;
  `pnw-mop` has everything else);
- in the desktop: dragging files out, LAT and MOP.

## Finding your way around the source

```
docs/             user guides
lib/pnwclient/    client for the decnetd API: Api, Link
lib/pnwdap/       DAP client (NFT): directory, get, put, erase, rename;
                  record conversion; remote path names
lib/pnwcterm/     CTERM terminal end: foundation, reads, editing, writes
lib/pnwlat/       LAT terminal end: announcements, circuit, session
lib/pnwmail/      Mail-11 sender and receiver; mbox
lib/pnwnice/      NICE queries: known nodes
lib/pnwterm/      the local terminal: raw mode, size, UTF-8 bridge
tools/pnw-ncp/    Network Control Program
tools/pnw-nft/    pnw-dir, pnw-type, pnw-copy, pnw-delete, pnw-rename
tools/pnw-fs/     FUSE mount
tools/pnw-sethost/ CTERM remote login
tools/pnw-lat/    LAT terminal, and pnw-latsock, its privileged helper
tools/pnw-mail/   DECnet mail
tools/pnw-mop/    MOP: system IDs, counters, loop
gui/              pathnoworks, the Qt desktop
tests/            unit tests, and integration tests against real nodes
```

---

*No VAXen were harmed in the making of this software. A few were woken up
rather earlier than they'd have liked.*
