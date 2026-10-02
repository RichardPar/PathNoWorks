# PathNoWorks

> *Path-No-works, but it works. Mostly. On Linux.*

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

The DECnet node itself is [cppdecnet](https://github.com/RichardPar/cppdecnet)'s `decnetd`.
The tools don't touch the network at all; they ask decnetd to do it for
them over its API socket, so none of them needs root. The one rebel is
`pnw-lat`: LAT isn't DECnet, so it goes straight onto the LAN by itself.

Here's what it looks like. On the left is the network, as the router
sees it; on the right, a VAX's files.

<p>
<img src="docs/images/network.png" alt="The PathNoWorks network window, listing reachable HECnet nodes" width="49%">
<img src="docs/images/files.png" alt="A file window on VAXXY, showing a VMS directory" width="49%">
</p>

And here's how the pieces fit together:

```
┌────────────────────────── your Linux machine ──────────────────────────┐
│                                                                        │
│   pathnoworks        pnw-ncp      pnw-dir  pnw-type  pnw-copy          │
│   (the Qt desktop)   pnw-sethost  pnw-delete  pnw-rename               │
│                      pnw-mail     pnw-mop     pnw-fs (FUSE)  pnw-x11   │
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
| `pnw-x11` | takes `X$X0` here for VMS's X clients; or connects to a node's `X$X0` | X11 |
| `pnw-lat` | nothing: LAT hosts directly, through `pnw-latsock` | LAT |

The socket is the only door, so it's also the only lock: whoever can open
it can act as this DECnet node. Set its mode accordingly (`--mode 600` or
`660`).

## What's in the box

| Tool | What it does | Guide |
|---|---|---|
| `pathnoworks` | The desktop: the network and your favourite nodes, a node's files (browse, drag and drop both ways, mount), terminals, mail, and VMS DECwindows programs on your screen | [desktop](docs/desktop.md) |
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
| `pnw-x11` | X11 over DECnet: VMS DECwindows programs on your screen, as eXcursion did | [pnw-x11](docs/pnw-x11.md) |

Want other nodes to reach files on *this* machine? cppdecnet has `dnfal`,
a FAL with its own access control. See "File access" in the cppdecnet
README.

New here? [Getting started](docs/getting-started.md) takes you from
building it to copying your first file.

## Requirements

PathNoWorks is no use on its own: it needs a DECnet network to join, and
nodes on it worth talking to. What it takes to build is under
[Building it](#building-it); this is what it takes to *use*.

### A place on the network

- **A DECnet address and name for this machine.** The address is
  `area.node`, like `29.151`: area 1 to 63, node 1 to 1023. The name is
  up to six letters and digits, like `PNW`. On HECnet, your area's
  coordinator gives you an address (see the
  [HECnet page](http://mim.softjar.se/)). On a network of your own, pick
  any that nobody else is using. Two nodes with one address will confuse
  every router that sees them.
- **A way in.** One of:
  - **A Multinet peer**, which is what most people use: a DECnet router
    that will link to you over TCP. You need its IP address or host name,
    and its port, such as `192.168.10.151:7100`. Its owner adds a circuit
    for you, with your IP address in it, as `circuit mul-1 Multinet
    <your IP>:7100:listen`; your end connects to theirs. (It can be the
    other way round, but the end that listens must be reachable through
    firewalls and NAT.) On HECnet, ask the owner of the node you'll link
    to. Multinet runs over anything that carries TCP: Wi-Fi, a VPN, the
    internet.
  - **Ethernet**, straight onto a LAN that already has DECnet on it (a
    VAX or PDP-11, real or in SIMH, or a router). It has to be wired,
    since Wi-Fi drops DECnet's frames, and decnetd needs libpcap and the
    right to send raw frames. Linux only.
- **decnetd running**, with an `api` line for the tools.
  `tools/install-decnetd.sh` in cppdecnet asks for all of the above and
  sets it up as a service; on Windows, the installer does it.

### On the nodes you talk to

Each feature needs something at the other end:

| To use | the other node needs |
|---|---|
| Files, the network drive | a file server, FAL (object 17), and usually a login |
| The node list, `pnw-ncp` | network management, NML (object 19) |
| Terminal | CTERM (object 42), as VMS and RSX have, and a login |
| Mail | Mail-11 (object 27) |
| DECwindows programs | VMS with DECwindows Motif installed, and a login |
| New folder and deleting folders on VMS | a login: they run as DCL |
| LAT terminals | a LAT service on your own LAN (VMS with LAT started, for one) |
| MOP | stations on your Ethernet LAN; decnetd on that LAN with `--mop` |

### On this machine

| | Linux | Windows |
|---|---|---|
| The DECnet node | decnetd: `install-decnetd.sh` makes it a service | decnetd, which the installer sets to start when you log in |
| An X server, for DECwindows programs | the desktop's own; tried with X.Org | [VcXsrv](https://github.com/marchaesen/vcxsrv); the desktop starts it |
| Terminal windows | xterm | Windows Terminal, or a console window |
| The network drive (Mount, `pnw-fs`) | FUSE 3 | [WinFsp](https://winfsp.dev/) |
| LAT terminals | wired Ethernet; `pnw-latsock` with `CAP_NET_RAW` | not available |
| Ethernet circuits and MOP | libpcap and a wired interface | not available: Multinet only |

## A quick look

```sh
# decnetd running as an endnode, its "api" socket at /tmp/decnetapi.sock,
# where the tools look by default (see samples/decnetd.conf)

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

pnw-x11 serve --allow VMSNOD &                  # VMSNOD's DECwindows programs on this screen
```

A word of warning you'll see again: **quote remote file specifications.**
The shell has strong opinions about `[ ] * ; " $`, and VMS file names are
full of them.

## Sending mail from the desktop

`NODE::USER` mail, the way it was meant to be sent, with a mouse this
time.

1. In `pathnoworks`, click **Mail** on the toolbar, press **Ctrl+M**, or
   right-click a node and choose **Mail**. If a node is selected, **To:**
   starts with its name (`VMSNOD::`) and you only add the user.
2. Fill in **To:**. Separate several addresses with commas:
   - `VMSNOD::SYSTEM`
   - `VMSNOD::SYSTEM, BAJI::RICHARD`
   - `VMSNOD"user password"::SYSTEM`, for a node with no default DECnet
     account. Without the login it answers "Access control rejected". The
     login only lets the far end's mail server run; the mail is still
     from you.
3. Type a **Subject**, then the message.

   ![The mail window, addressed to VAXXY::SYSTEM and BAJI::RICHARD](docs/images/mail.png)

4. Click **Send**. Mail goes out under your login name, so the far end
   sees it from `YOURNODE::YOU`.
   - If everything got through, the window closes and the status bar says
     who it went to.
   - If anything didn't, the window stays open and lists each recipient
     as sent or "NOT sent", with the far end's reason (VMS's
     `%MAIL-E-NOSUCHUSR`, say). Fix the address and send again.

The desktop only sends. To receive, leave a listener running and read the
mbox with whatever you like:

```sh
pnw-mail listen &              # mail to this node goes into ~/Mail/decnet
mutt -f ~/Mail/decnet
```

If VMS shows your mail as from something like `29847::RICHARD`, it just
doesn't know your node's name yet. Log in there (the **Terminal** button
will do) and tell it, using your own address and name:

```
$ MCR NCP DEFINE NODE 29.151 NAME PNW
$ MCR NCP SET NODE 29.151 NAME PNW
```

There's more in [the desktop guide](docs/desktop.md) and in
[pnw-mail](docs/pnw-mail.md).

## Building it

### What you'll need

PathNoWorks is built and tested on Linux Mint 22 (Ubuntu 24.04 underneath).
Any distribution with a new enough compiler should do; the package names
below are Debian and Ubuntu's.

| Package | Needed for | |
|---|---|---|
| `build-essential` (GCC 13+, or Clang 16+), `make` | everything | required |
| `cmake` (3.20+), `pkg-config` | everything | required |
| `libcrypt-dev` | cppdecnet (dnfal's password hashes) | required |
| `libpcap-dev` | cppdecnet's Ethernet circuits on a real LAN (pcap), which MOP needs | optional |
| `libfuse3-dev`, `fuse3` | `pnw-fs`, and Mount in the desktop | optional |
| `qt6-base-dev` | the `pathnoworks` desktop and its test | optional |
| `xterm` | terminal windows from the desktop | optional, at run time |
| `libcap2-bin` | `setcap`, to let `pnw-lat` send LAT frames | optional, at run time |
| `python3` and a PyDECnet checkout | the tests against PyDECnet's FAL | optional |

All of it in one go:

```sh
sudo apt install build-essential cmake pkg-config libcrypt-dev libpcap-dev \
                 libfuse3-dev fuse3 qt6-base-dev xterm libcap2-bin
```

Leave out an optional piece and you only lose what depends on it. CMake
says what it skipped, and everything else still builds. One catch: if you
add `libpcap-dev` after building cppdecnet, run `make clean` in cppdecnet,
since it won't otherwise rebuild what's already built with Ethernet
support.

### The easy way

```sh
./build.sh            # check, offer to install and clone what's missing, build
./build.sh --test     # ...and run the tests
```

`build.sh` checks for everything above. It offers to install whatever's
missing with apt, dnf or pacman, and to clone cppdecnet (and, if you like,
PyDECnet) into the places described below. Then it builds. It asks before
doing anything to your system; `--yes` says yes to everything, `--no`
just checks and builds with what's there. `./build.sh --help` has the
rest. If you'd rather do it by hand, read on.

### Where things go

PathNoWorks needs cppdecnet, which lives in its own repository at
[github.com/RichardPar/cppdecnet](https://github.com/RichardPar/cppdecnet).
PyDECnet, Paul Koning's DECnet in Python, is only needed for some of the
tests; it's at [github.com/pkoning2/pydecnet](https://github.com/pkoning2/pydecnet).
Clone them side by side and everything finds everything else:

```sh
mkdir -p ~/Source/Decnet && cd ~/Source
git clone https://github.com/RichardPar/PathNoWorks.git
git clone https://github.com/RichardPar/cppdecnet.git  Decnet/cppdecnet
git clone https://github.com/pkoning2/pydecnet.git     Decnet/pydecnet   # optional
```

which gives you:

```
Source/
├── PathNoWorks/              this
└── Decnet/
    ├── cppdecnet/            the DECnet node: decnetd, dnfal
    └── pydecnet/pydecnet/    optional, for tests
```

Somewhere else is fine too: point CMake at it with
`-DCPPDECNET_DIR=/path/to/cppdecnet` (and `-DPYDECNET_DIR=...`).

### Build and test

```sh
cd PathNoWorks
cmake -S . -B build              # -DCPPDECNET_DIR=/path/to/cppdecnet
cmake --build build -j
ctest --test-dir build
```

You don't need to build cppdecnet first. CMake runs cppdecnet's own
Makefile as part of the build, and does it again whenever cppdecnet
changes. The tools end up under `build/tools/`, the desktop at
`build/gui/pathnoworks`, and decnetd and dnfal under
`../Decnet/cppdecnet/build/release/bin/`.

The tests are the real thing, not mocks. They start actual decnetd nodes
on your machine and drive the tools against them:

- NCP;
- file access, against both `dnfal` and PyDECnet's `fal.py`;
- access control;
- the FUSE mount;
- mail from one node to another;
- MOP;
- X11 over DECnet, with a stand-in X server;
- and the desktop's windows, driven offscreen.

The PyDECnet tests are skipped if PyDECnet isn't at
`../Decnet/pydecnet/pydecnet` (`-DPYDECNET_DIR=...`), and the mount test
is skipped without FUSE. Nothing in the tests touches your real network
or your running decnetd.

`-DCPPDECNET_FLAVOUR=debug` links cppdecnet's sanitizer build and builds
PathNoWorks with the sanitizers too. Slow, but it catches things.

### Install

```sh
sudo cmake --install build                       # tools, desktop and menu entry, under /usr/local
sudo make -C ../Decnet/cppdecnet install         # decnetd and dnfal, under /usr/local
sudo setcap cap_net_raw+ep /usr/local/bin/pnw-latsock    # only if you want pnw-lat
```

Prefer your home directory? `cmake --install build --prefix ~/.local`
installs PathNoWorks without `sudo`. Make sure `~/.local/bin` is on your
`PATH`.

### A Debian package

On Debian, Ubuntu, Mint and their relatives you can build a `.deb`
instead, with everything in it: the tools, the desktop and its menu
entry, and cppdecnet's decnetd, dnfal and dnping. It needs `dpkg-dev` as
well as what building needs.

```sh
packaging/debian/build-deb.sh            # writes Linux/pathnoworks_<version>_<arch>.deb
sudo apt install ./Linux/pathnoworks_0.1.0_amd64.deb
decnetd-setup                            # as yourself: make this machine a node
```

Installing it doesn't put anything on the network. `decnetd-setup` is
cppdecnet's `install-decnetd.sh` under another name. It asks for the
node's name and address, its Multinet peer and the rest, and runs
decnetd as a systemd service. The package gives `pnw-latsock` its raw
socket right as it installs, restarts decnetd when you upgrade, and stops
the service if you remove it; your configuration in `/etc/decnet` stays.

A package depends on the library versions of the system it was built
on, so build it on the oldest release you want it to install on.

### First run

[`samples/decnetd.conf`](samples/decnetd.conf) is a working endnode
configuration to start from. It has a Multinet link to a router and the
API socket at `/tmp/decnetapi.sock`, where every tool looks by default.
Commented-out sections cover an Ethernet circuit with MOP, serving files
with dnfal (with [`samples/fal.users`](samples/fal.users) to say who gets
in), and decnetd's web page. Copy it, change the addresses and names to
yours, and:

```sh
decnetd --log-level info decnetd.conf &
pnw-ncp show executor
pathnoworks
```

[Getting started](docs/getting-started.md) goes through it step by step.

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
X11 over DECnet has put the DECwindows clock from OpenVMS VAX 6.2 on a
Linux desktop.

Still on the list:

- the MOP console carrier (a remote console on a DECserver or VAX;
  `pnw-mop` has everything else);
- in the desktop: LAT and MOP.

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
tools/pnw-x11/    X11 over DECnet, both ways
gui/              pathnoworks, the Qt desktop
samples/          a decnetd configuration and dnfal users file to start from
tests/            unit tests, and integration tests against real nodes
```

---

*No VAXen were harmed in the making of this software. A few were woken up
rather earlier than they'd have liked.*
