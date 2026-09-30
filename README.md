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

Planned, in order: DAP file access (`pnw-dir`, `pnw-copy`, `pnw-type`),
`pnw-sethost` (CTERM), LAT in decnetd, a FUSE mount of remote directories,
Mail-11, then Windows and a Qt GUI.

## Build

Needs CMake 3.20+, GCC 13+ or Clang 16+, and a cppdecnet checkout, by
default at `../Decnet/cppdecnet`. CMake builds cppdecnet with its own
Makefile.

```sh
cmake -S . -B build              # -DCPPDECNET_DIR=/path/to/cppdecnet
cmake --build build
ctest --test-dir build           # starts two decnetd nodes and drives pnw-ncp
```

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
```

The socket is `$DECNETAPI`, `/tmp/decnetapi.sock` by default, or `-s path`.
Keywords abbreviate to three letters, as in NCP.

## Layout

```
lib/pnwclient/    client for the decnetd API: Api, Link
tools/pnw-ncp/    Network Control Program
tests/            integration tests against real decnetd nodes
```
