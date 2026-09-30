# Getting started

This takes you from nothing to listing files on another DECnet node. It
assumes a Linux machine and some familiarity with DECnet node names and
addresses.

## 1. Build cppdecnet and PathNoWorks

PathNoWorks expects cppdecnet next to it, at `../Decnet/cppdecnet`. CMake
builds cppdecnet for you.

```sh
cmake -S . -B build              # add -DCPPDECNET_DIR=... if it is elsewhere
cmake --build build
ctest --test-dir build           # optional: runs real nodes on this machine
```

For `pnw-fs` you also need FUSE 3 (`sudo apt install fuse3 libfuse3-dev`
on Debian and Ubuntu). Without it everything else still builds.

The tools end up under `build/tools/`, and decnetd under
`../Decnet/cppdecnet/build/release/bin/`.

## 2. Give decnetd an API socket

The tools never touch the network themselves. They ask your running
decnetd to make connections for them, through a Unix socket. Add an `api`
line to decnetd's configuration:

```
routing 29.150 --type l1router
node 29.150 CPPNOD
circuit eth-0 Ethernet pcap:eth0 --t3 10
api /run/decnet/api.sock --mode 660
```

The mode decides who may use the node: anyone who can open the socket can
make and accept DECnet connections as this node. With `--mode 660`, put
yourself in the socket's group.

Without a path the socket is `$DECNETAPI`, or `/tmp/decnetapi.sock`. The
tools look in the same places, or take `-s path`. To save typing:

```sh
export DECNETAPI=/run/decnet/api.sock
```

Start decnetd as described in the cppdecnet README.

## 3. Check that it works

```sh
pnw-ncp show executor
pnw-ncp show known nodes
pnw-ncp loop node MIM
```

The first shows your own node. The last loops a few messages through
MIM's loopback object, which shows the path works in both directions.

## 4. Look at some files

```sh
pnw-dir  'MIM::*'
pnw-type 'MIM::HECNET.DAT'
pnw-copy 'MIM::HECNET.DAT' .
```

On a VMS system you will usually need a user name and password:

```sh
pnw-dir 'VMSNOD"user password"::SYS$LOGIN:*.COM'
```

Always quote remote file specifications. The shell has its own ideas about
`[ ] * ; " $`.

## 5. Serve files from this machine

To let other nodes reach files here, run cppdecnet's `dnfal` as object 17.
See "File access" in the cppdecnet README, and give it a user file before
connecting it to HECnet.

## When something goes wrong

| Message | Meaning |
|---|---|
| `cannot reach decnetd at ... (is it running with an "api" line?)` | decnetd is not running, has no `api` line, or uses another socket path |
| `Unrecognized node name` | the name is not in decnetd's node database; use the address, or add a `node` line |
| `Node unreachable` | routing has no path to that node; `pnw-ncp show known nodes` shows what is reachable |
| `Unrecognized object` | the node has no FAL (object 17) or NML (object 19) running |
| `Access control rejected` | wrong user or password, or anonymous access is not allowed |
| `no answer from ...` | the node accepted nothing within the time allowed |

Adding `--trace` to any of the file tools prints each DAP message, which
is the first thing to look at when a file transfer misbehaves.
