# Getting started

> *Every DECnet journey begins with `SHOW EXECUTOR`.*

This gets you from a bare checkout to listing files on another DECnet
node. I'm assuming a Linux machine and that DECnet node names and
addresses (`MIM`, `1.13`, that sort of thing) aren't a complete mystery.
If they are, the HECnet folks are friendly, and this will make more sense
after an evening of reading.

## 1. Build cppdecnet and PathNoWorks

PathNoWorks expects cppdecnet to live next door, at `../Decnet/cppdecnet`.
It has its own repository, on
[GitHub](https://github.com/RichardPar/cppdecnet); PathNoWorks needs its
`PathNoWorksAPI` branch for now:

```sh
git clone -b PathNoWorksAPI https://github.com/RichardPar/cppdecnet.git ../Decnet/cppdecnet
```

You don't need to build it yourself; CMake does that.

```sh
cmake -S . -B build              # add -DCPPDECNET_DIR=... if it is elsewhere
cmake --build build
ctest --test-dir build           # optional: runs real nodes on this machine
```

For `pnw-fs` you'll also want FUSE 3 (`sudo apt install fuse3 libfuse3-dev`
on Debian and Ubuntu). Without it everything else still builds; you just
don't get the network drive.

The tools land under `build/tools/`, and decnetd under
`../Decnet/cppdecnet/build/release/bin/`.

## 2. Give decnetd an API socket

Here's the trick that makes it all work. The tools never touch the
network themselves. They ask your running decnetd to make connections on
their behalf, over a Unix socket. So decnetd's configuration needs an
`api` line:

```
routing 29.151 --type endnode
node 29.151 PNW
node 29.150 CPPNOD
circuit mul-0 Multinet 192.168.10.151:7100:connect --t3 15
api /tmp/decnetapi.sock --mode 600
```

That's a desktop's usual setup: an **endnode**. It has one circuit, to a
router (CPPNOD here, reached over Multinet, which is DECnet carried in
TCP, the way HECnet nodes link up), and it leaves the rest of the network
to that router. Put in your own node's address and name and your
router's; on HECnet, your area's coordinator gives you an address. The
router needs a matching `circuit … :listen` line for you.

Think about the mode for a moment. Anyone who can open that socket can
make and accept DECnet connections *as this node*. `--mode 600` keeps it
to the user decnetd runs as; use `660` and a group to share it.

`/tmp/decnetapi.sock` is where every tool looks by default, so nothing
else needs setting. Put the socket elsewhere and tell the tools with
`$DECNETAPI` or `-s path`. On Windows the default is `decnetapi.sock` in
`%TEMP%`, and a plain `api` line puts it there.

For a desktop machine, consider `--on-demand`:

```
api /tmp/decnetapi.sock --mode 600 --on-demand
```

decnetd then starts without connecting to the router, brings the circuit
up when a tool or the desktop first connects, and takes it down again two
hours after the last one has gone (`--idle seconds` to change that). So
decnetd can start when you log in and stay off the network until you use
it.

The same configuration, with comments and the usual additions (DECnet
straight on an Ethernet, serving files with dnfal), is in
[`samples/decnetd.conf`](../samples/decnetd.conf). Copy it, put in your
own addresses and names, and start decnetd as the cppdecnet README
describes.

## 3. Is anybody out there?

```sh
pnw-ncp show executor
pnw-ncp show known nodes
pnw-ncp loop node MIM
```

The first shows your own node; if that works, the plumbing is in. The
last one bounces a few messages off MIM's loopback object, which proves
the path works in both directions. Sending is easy; it's getting a reply
that counts.

## 4. Look at some files

```sh
pnw-dir  'MIM::*'
pnw-type 'MIM::HECNET.DAT'
pnw-copy 'MIM::HECNET.DAT' .
```

A VMS system will usually want a user name and password:

```sh
pnw-dir 'VMSNOD"user password"::SYS$LOGIN:*.COM'
```

And yes, I'll say it again: always quote remote file specifications. The
shell has its own ideas about `[ ] * ; " $`, and they're never the ones
you wanted.

## 5. Serve files from this machine

To let other nodes reach files here, run cppdecnet's `dnfal` as object 17.
See "File access" in the cppdecnet README. Please give it a user file
before you connect it to HECnet; the whole hobby is very nice, but there's
no need to be *that* nice.

## When something goes wrong

It will. DECnet error messages are terse but honest. Here's what they're
trying to tell you:

| Message | Meaning |
|---|---|
| `cannot reach decnetd at ... (is it running with an "api" line?)` | decnetd is not running, has no `api` line, or uses another socket path |
| `Unrecognized node name` | the name is not in decnetd's node database; use the address, or add a `node` line |
| `Node unreachable` | routing has no path to that node; `pnw-ncp show known nodes` shows what is reachable |
| `Unrecognized object` | the node has no FAL (object 17) or NML (object 19) running |
| `Access control rejected` | wrong user or password, or anonymous access is not allowed |
| `no answer from ...` | the node accepted nothing within the time allowed |

When a file transfer misbehaves, add `--trace` to any of the file tools.
It prints every DAP message as it goes by. It's the first thing to look
at, and usually the last thing you need.
