# PathNoWorks

Pathworks-style DECnet tools for Linux, in C++20: network management, file
access and a network drive for VMS, RSX and HECnet, from a Linux desktop.

The DECnet node itself is [cppdecnet](../Decnet/cppdecnet)'s `decnetd`.
The tools talk to it through its API socket, so they need no privileges
and never touch the network directly.

```
pnw-ncp  pnw-sethost  pnw-dir  pnw-type  pnw-copy  pnw-delete  pnw-rename  pnw-fs
        |  JSON over a Unix socket (PyDECnet's API)
decnetd (cppdecnet): routing, NSP, session control, MOP, NICE, FAL
        |  Ethernet (pcap/TAP), Multinet, DDCMP
DECnet: VMS, RSX, PyDECnet, HECnet
```

## Tools

| Tool | What it does | Guide |
|---|---|---|
| `pnw-ncp` | NCP: `SHOW` and `LIST` network information from any node, `LOOP NODE` | [pnw-ncp](docs/pnw-ncp.md) |
| `pnw-dir` | List remote files | [file access](docs/file-access.md) |
| `pnw-type` | Show a remote file | [file access](docs/file-access.md) |
| `pnw-copy` | Copy files to and from remote nodes, wildcards included | [file access](docs/file-access.md) |
| `pnw-delete` | Delete remote files | [file access](docs/file-access.md) |
| `pnw-rename` | Rename a remote file | [file access](docs/file-access.md) |
| `pnw-fs` | Mount a remote directory as a local one (FUSE) | [pnw-fs](docs/pnw-fs.md) |
| `pnw-sethost` | Log in to a node, as `SET HOST` does (CTERM); VT300/VT340 and Sixel through your terminal | [pnw-sethost](docs/pnw-sethost.md) |

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
```

Quote remote file specifications: the shell treats `[ ] * ; " $`
specially.

## Build

Needs CMake 3.20+, GCC 13+ or Clang 16+, and cppdecnet, by default at
`../Decnet/cppdecnet`; CMake builds it with its own Makefile. `pnw-fs`
also needs FUSE 3 (`fuse3` and `libfuse3-dev`) and is left out without it.

```sh
cmake -S . -B build              # -DCPPDECNET_DIR=/path/to/cppdecnet
cmake --build build
ctest --test-dir build
```

The tests start real decnetd nodes on this machine and drive the tools
against them: NCP, file access against both `dnfal` and PyDECnet's
`fal.py`, access control, and the FUSE mount. The PyDECnet tests are
skipped if PyDECnet is not at `../Decnet/pydecnet/pydecnet`
(`-DPYDECNET_DIR=...`), and the mount test without FUSE.

`-DCPPDECNET_FLAVOUR=debug` links cppdecnet's sanitizer build and builds
PathNoWorks with the sanitizers too.

## Status and plans

Everything above is tested against OpenVMS VAX 6.2 (DECnet-VAX 6.1) as
well as cppdecnet and PyDECnet: NCP, directory, text and binary copies in
both directions, rename, delete, the FUSE mount and CTERM logins. RSX
has been tried for NCP and loopback only.

Planned: LAT in decnetd, Mail-11, then Windows and a Qt GUI.

## Layout

```
docs/             user guides
lib/pnwclient/    client for the decnetd API: Api, Link
lib/pnwdap/       DAP client (NFT): directory, get, put, erase, rename;
                  record conversion; remote path names
lib/pnwcterm/     CTERM terminal end: foundation, reads, editing, writes
tools/pnw-ncp/    Network Control Program
tools/pnw-nft/    pnw-dir, pnw-type, pnw-copy, pnw-delete, pnw-rename
tools/pnw-fs/     FUSE mount
tools/pnw-sethost/ CTERM remote login
tests/            unit tests, and integration tests against real nodes
```
