# PathNoWorks

Pathworks-style DECnet tools for Linux, in C++20: network management, file
access, a network drive, remote login and mail for VMS, RSX and HECnet,
from a Linux desktop.

The DECnet node itself is [cppdecnet](../Decnet/cppdecnet)'s `decnetd`.
The DECnet tools talk to it through its API socket, so they need no
privileges and never touch the network directly. `pnw-lat` is the
exception: LAT is not DECnet, and it speaks it on the LAN itself.

```
pnw-ncp  pnw-sethost  pnw-mail  pnw-mop  pnw-dir  pnw-type  pnw-copy  pnw-delete  pnw-rename  pnw-fs
        |  JSON over a Unix socket (PyDECnet's API)          pnw-lat
        |                                                    |  LAT, straight on the LAN
decnetd (cppdecnet): routing, NSP, session control, MOP, NICE, FAL
        |  Ethernet (pcap/TAP), Multinet, DDCMP
DECnet: VMS, RSX, PyDECnet, HECnet
```

## Tools

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

To let other nodes reach files on this machine, cppdecnet has `dnfal`, a
FAL with its own access control; see "File access" in the cppdecnet
README.

New here? [Getting started](docs/getting-started.md) goes from building to
copying your first file.

## Quick look

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

Quote remote file specifications: the shell treats `[ ] * ; " $`
specially.

## Build

Needs CMake 3.20+, GCC 13+ or Clang 16+, and cppdecnet, by default at
`../Decnet/cppdecnet`; CMake builds it with its own Makefile. `pnw-fs`
also needs FUSE 3 (`fuse3` and `libfuse3-dev`), and the desktop needs Qt 6
(`qt6-base-dev`); each is left out without them.

```sh
cmake -S . -B build              # -DCPPDECNET_DIR=/path/to/cppdecnet
cmake --build build
ctest --test-dir build
```

The tests start real decnetd nodes on this machine and drive the tools
against them: NCP, file access against both `dnfal` and PyDECnet's
`fal.py`, access control, the FUSE mount, mail from one node to
another, MOP, and the desktop's windows, driven offscreen. The PyDECnet tests are skipped if PyDECnet is not at
`../Decnet/pydecnet/pydecnet` (`-DPYDECNET_DIR=...`), and the mount test
without FUSE.

`-DCPPDECNET_FLAVOUR=debug` links cppdecnet's sanitizer build and builds
PathNoWorks with the sanitizers too.

## Status and plans

Everything above is tested against OpenVMS VAX 6.2 (DECnet-VAX 6.1) as
well as cppdecnet and PyDECnet: NCP, directory, text and binary copies in
both directions, rename, delete, the FUSE mount, CTERM logins and mail
both ways. RSX has been tried for NCP, loopback, CTERM and LAT (up to
login).

Planned: the MOP console carrier (a remote console on a DECserver or
VAX; `pnw-mop` has everything else); in the desktop, dragging files out,
LAT and MOP.

## Layout

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
