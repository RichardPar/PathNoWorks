# PathNoWorks

Pathworks-style DECnet tools for Linux (Windows later), in C++20.

The network stack is [cppdecnet](../Decnet/cppdecnet): `decnetd` runs the
DECnet Phase IV node, and the PathNoWorks tools talk to it through its API
socket. The tools need no privileges and never touch the wire.

```
pnw-ncp, (later) pnw-sethost, pnw-dir, pnw-copy, pnw-fs, ...
        |  JSON over a Unix socket (PyDECnet's API)
decnetd (cppdecnet): routing, NSP, session control, MOP, NICE
        |  Ethernet (pcap/TAP), Multinet, DDCMP
DECnet: VMS, RSX, HECnet
```

## Status

| Tool | What it does |
|---|---|
| `pnw-ncp` | NCP subset: `SHOW`/`LIST` over NICE (object 19), `LOOP NODE` through MIRROR, `TELL` |
| `pnw-dir` | List remote files through FAL (object 17), with size, date, owner and protection |
| `pnw-type` | Write a remote file to standard output |
| `pnw-copy` | Copy a file from or to a remote node; `--text` or `--binary` to override the automatic choice |
| `pnw-delete` | Delete remote files (wildcards allowed) |
| `pnw-rename` | Rename a remote file |

To serve files from this machine, run cppdecnet's `dnfal` as object 17,
with a user file for access control; see the cppdecnet README.

Planned, in order: proxy access,
`pnw-sethost` (CTERM), LAT in decnetd, a FUSE mount of remote directories,
Mail-11, then Windows and a Qt GUI.

## Build

Needs CMake 3.20+, GCC 13+ or Clang 16+, and a cppdecnet checkout, by
default at `../Decnet/cppdecnet`. CMake builds cppdecnet with its own
Makefile.

```sh
cmake -S . -B build              # -DCPPDECNET_DIR=/path/to/cppdecnet
cmake --build build
ctest --test-dir build           # starts decnetd nodes and drives the tools
```

The file access tests run against both cppdecnet's `dnfal` and PyDECnet's
`fal.py`; the latter is skipped if PyDECnet is not at
`../Decnet/pydecnet/pydecnet` (`-DPYDECNET_DIR=...`).

`-DCPPDECNET_FLAVOUR=debug` links cppdecnet's sanitizer build and builds
PathNoWorks with the sanitizers too.

## Running

Give decnetd an API socket in its configuration:

```
api /tmp/decnetapi.sock --mode 660
```

Then:

```sh
pnw-ncp show known nodes
pnw-ncp tell MIM show executor characteristics
pnw-ncp loop node MIM count 5 length 100
pnw-ncp                          # NCP> prompt

pnw-dir  'VMS"user password"::SYS$LOGIN:*.COM'
pnw-type 'VMS"user password"::LOGIN.COM'
pnw-copy 'MIM::HECNET.DAT' nodes.dat
pnw-copy notes.txt 'VMS"user password"::[USER]'
pnw-copy 'VMS"user password"::[USER]*.COM' ./coms/     # every match
pnw-copy *.txt 'VMS"user password"::[USER]'             # several at once
pnw-delete 'VMS"user password"::NOTES.TXT;*'
```

Quote remote file specs: the shell treats `[ ] * ; "` specially. Text
files (records with carriage return control) arrive with one newline per
record; anything else is copied as stored and cut to its real length.

The socket is `$DECNETAPI`, `/tmp/decnetapi.sock` by default, or `-s path`.
Keywords abbreviate to three letters, as in NCP.

## Layout

```
lib/pnwclient/    client for the decnetd API: Api, Link
lib/pnwdap/       DAP client (NFT): directory, get, record conversion
tools/pnw-ncp/    Network Control Program
tools/pnw-nft/    pnw-dir, pnw-type, pnw-copy, pnw-delete, pnw-rename
tests/            integration tests against real decnetd nodes
```
