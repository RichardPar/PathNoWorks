# pnw-x11

> `$ SET DISPLAY/CREATE/NODE=PNW/TRANSPORT=DECNET`
> *and a clock you didn't ask for appears on your Linux desk.*

Pathworks PCs had eXcursion, an X server that let VMS DECwindows programs
open their windows on the PC's screen, over DECnet. X was network
transparent from the start, and DEC made sure DECnet was one of the
networks it could use. pnw-x11 does the same job for Linux: it's a bridge
between DECnet and your ordinary X server.

It works in both directions:

- **`serve`** takes the DECnet object that X clients connect to for a
  display (`X$X0` for display 0), and hands each connection to your local
  X server. A program on VMS then thinks your node *is* a workstation.
- **`connect`** is the other way round. It makes a local display, `:20`
  by default, and sends whatever connects to it on to a DECnet node's X
  server.

```
pnw-x11 [options] serve --allow NODE[,NODE...] [-n display]
pnw-x11 [options] connect NODE[::n] [-d display]
```

```sh
pnw-x11 serve --allow VAXXY &            # VAXXY's programs on this screen
pnw-x11 connect VAXXY -d 20 &            # DISPLAY=:20 means VAXXY::0
```

| Option | Meaning |
|---|---|
| `--allow NODE,...` | (serve) the nodes allowed to open windows, by name or address |
| `--allow-any` | (serve) any node at all; see below before using it |
| `-n n` | (serve) the display to offer, object `X$Xn`; default 0 |
| `--display D` | (serve) the local X server; default `$DISPLAY` (`:0` on Windows). `:0`, or its socket's path |
| `-d n` | (connect) the local display to make; default 20 |
| `--listen PATH` | (connect) the socket to make instead of `/tmp/.X11-unix/Xn` (on Windows, instead of TCP port 6000 + n) |
| `--socket s` | decnetd's API socket; default `$DECNETAPI`, then `/tmp/decnetapi.sock` (`%TEMP%\decnetapi.sock` on Windows) |
| `--trace` | log each connection, with byte counts |

## A word about trust

An X client can do almost anything to your desktop. It can read every
key you press, take pictures of the screen and type into other windows.
X has always been like that. So `serve` won't start until you say who may
connect. `--allow VAXXY` lets VAXXY in and turns everyone else away,
logging it:

```
%PNW-W-XREFUSED, NODEB::PNW: not in --allow
```

`--allow-any` exists, and on HECnet it means *anyone on HECnet*. Please
don't.

## The X server's login

Your X server only accepts clients that bring this session's magic
cookie, and a program on VMS has never heard of it. `serve` deals with
that the way SSH's X forwarding does. It reads your cookie from
`$XAUTHORITY` (or `~/.Xauthority`) and swaps it into each connection's
first message, whatever the far end sent. If there's no cookie to be
found, it says so at startup and passes the connection on unchanged.

## On Windows: VcXsrv

Windows has no X server of its own, so you need one. Use
**[VcXsrv](https://github.com/marchaesen/vcxsrv)**: it's free, it's
maintained, and in its multi-window mode each DECwindows program becomes
an ordinary Windows window, much as eXcursion made them. Download the
installer, `vcxsrv-64.<version>.installer.exe`, from the project's
releases page:

<https://github.com/marchaesen/vcxsrv/releases>

or install it from a command prompt with winget:

```
winget install marha.VcXsrv
```

The defaults are fine. PathNoWorks was tried with 21.1.16.1. (Xming's free
version is years out of date; X410, from the Microsoft Store, also works
but isn't free. WSLg won't do: its X server lives inside WSL, out of
reach of Windows programs.)

**From the desktop, that's all.** The first time you start a DECwindows
program, the desktop looks for an X server on display `:0`. If there isn't
one, it starts VcXsrv (found through its registry entry) like this:

```
vcxsrv.exe :0 -multiwindow -clipboard -wgl -auth %USERPROFILE%\.Xauthority
```

and adds a cookie for display 0 to `%USERPROFILE%\.Xauthority` if there
isn't one already. VcXsrv carries on running after the desktop closes, as
an X server you'd started yourself would. If you've started an X server
yourself, the desktop uses that instead.

**Running pnw-x11 by hand**, start VcXsrv first. The simplest way is
XLaunch (it comes with VcXsrv): choose *Multiple windows*, display 0, and
on the last page add `-auth %USERPROFILE%\.Xauthority` under additional
parameters. You'll need a cookie in that file. Running a DECwindows
program from the desktop once makes one, or use VcXsrv's own `xauth.exe`.
Then:

```
pnw-x11 serve --allow VAXXY
```

`--display` defaults to `:0` on Windows, because VcXsrv doesn't set
`DISPLAY`. On Windows X goes over TCP: display *n* is port 6000 + *n* on
this machine. `connect` listens the same way, so its clients use
`DISPLAY=localhost:20`.

**Don't use `-ac`**, much as many guides suggest it. It switches off
VcXsrv's access control, and VcXsrv listens on every network interface,
not just this machine. With `-ac`, anyone who can reach your PC can open
windows on your screen and read your keystrokes. With `-auth` only holders
of the cookie get in, and `pnw-x11 serve` hands it only to nodes in
`--allow`. When VcXsrv first starts, Windows Firewall asks whether to let
it accept connections. You can say no: pnw-x11 reaches it from this
machine, which the firewall doesn't block.

DEC's fonts work on VcXsrv too; see [below](#decs-fonts).

## From VMS

With DECwindows installed on the VMS side, point a process's display at
your node, then run something:

```
$ SET DISPLAY/CREATE/NODE=PNW/TRANSPORT=DECNET/SERVER=0
$ RUN SYS$SYSTEM:DECW$CLOCK
$ CREATE/TERMINAL=DECTERM/DETACH
```

`PNW` is your node's name as the VMS node knows it. If it doesn't know
it yet, see the end of [pnw-mail](pnw-mail.md) for how to tell it.

![The DECwindows clock from OpenVMS VAX 6.2, on a Linux desktop](images/vms-clock.png)

That's `DECW$CLOCK` from OpenVMS VAX 6.2, running on a VAX in SIMH and
drawing on a Linux desktop over DECnet.

### If VMS says "can't open display"

If every DECwindows program fails at once with `Can't Open display` (or
`%DECW-E-CANT_OPEN_DISPL`), and pnw-x11 never sees a connection, check
which common transport VMS is using:

```
$ @SYS$UPDATE:DECW$VERSIONS *
```

If it says `DECwindows transport ident is DECWINDOWS V5.4`, you have the
stub VMS installs for systems without DECwindows. Every one of its entry
points returns `%SYSTEM-E-UNSUPPORTED`, so the X library gives up before
it tries the network. On OpenVMS VAX 6.2 the stub (`;2`, from the VMS
saveset) outranks the real one (`;1`, from the DECwindows savesets),
even after DECwindows is installed. Restore the real one as a newer
version, from the VMS CD, and replace the installed copy:

```
$ BACKUP DKA400:[000000]DECW062.C/SAVE/SELECT=[SYS0.SYSLIB]DECW$TRANSPORT_COMMON.EXE;1 -
        SYS$COMMON:[SYSLIB]DECW$TRANSPORT_COMMON.EXE/NEW_VERSION
$ INSTALL REPLACE SYS$SHARE:DECW$TRANSPORT_COMMON.EXE
```

`DECW$VERSIONS` should then show the transport as `DW V6.2-950419`.
It took a debugger trace through the X library to find this one, so I'm
writing it down.

A VAX with no screen of its own needs DECwindows base support (from
`DECW$TAILOR`) and DECwindows Motif. It runs no X server; your Linux
machine is the display.

### DEC's fonts

DECwindows programs ask for DEC's fonts by name: DECterm for
`-DEC-Terminal-…`, menus for `-Bigelow & Holmes-Menu-…`, and plenty of
others. A Linux X server has none of them, so without help the programs
take whatever they're given: italic prompts in DECterm, odd-sized menus.

DEC's font files can't be used directly. They're in DECwindows' own
compiled `.DECW$FONT` format, which X.Org doesn't read. What does work is
giving every DEC name to the nearest font your X server already has.
`tools/pnw-x11/decw-font-aliases.py` does that. It reads the names out of
DEC's font files, matches each one on face, weight, slant and size
(scaling an outline font where that's closer than a bitmap), and writes a
`fonts.alias`:

```sh
decw-font-aliases.py ~/vms-fonts --install
```

`~/vms-fonts` is a copy of the `.DECW$FONT` files. They're on the VMS CD
in the DECwindows savesets `DECW062.E` and `F` under
`[SYS0.SYSFONT.DECW...]`, or on a VMS system with DECwindows fonts
installed in `SYS$COMMON:[SYSFONT.DECW...]`. Copy them with `pnw-copy
--binary`. The aliases go in `~/.local/share/fonts/decwindows`, and
`--install` adds that to the X server's font path for this session. The
PathNoWorks desktop adds it every time it starts the bridge. If you run
`pnw-x11 serve` yourself, add it after logging in:

```sh
xset +fp ~/.local/share/fonts/decwindows/
```

**On Windows**, run the script with Python on the font files, with no
`--install`. VcXsrv has no `xlsfonts` or `xset`, so the script reads
VcXsrv's own font lists instead of asking the running server:

```
py tools\pnw-x11\decw-font-aliases.py C:\vms-fonts
```

The aliases go in the same place, `%USERPROFILE%\.local\share\fonts\decwindows`.
Leave that place alone: the Microsoft Store's Python quietly diverts
anything written under AppData into a private copy no other program sees.
The desktop adds the folder to VcXsrv's font path when it starts VcXsrv.
If VcXsrv is already running, quit it (its icon in the notification area)
and the next DECwindows program starts it again with the fonts. To start
VcXsrv by hand with them, give `-fp` VcXsrv's own font folders and then
this one. Use full paths with backslashes and no backslash at the end: on
a `-fp` VcXsrv silently drops `./fonts/...` and `C:/...` paths, and keeps
only a built-in fixed font.

Fonts in DEC's private character sets (DECtech, DECmath, the
presentation bullets) get no stand-in: a Latin-1 font would show the
wrong symbols in their place, such as an `Ö` on the calculator's
square-root key. Those few characters stay blank, as before.

DEC's fonts aren't free. The script reads only their names, and nothing
of theirs goes into PathNoWorks.

## Messages

```
%PNW-I-XSERVING, PNW::0 (object X$X0) on :0
%PNW-I-XCONNECT, VAXXY::SYSTEM to :0
%PNW-I-XCLOSED, VAXXY::SYSTEM: link closed by the far end
%PNW-I-XLISTENING, DISPLAY=:20 goes to VAXXY::0
```

## Tested

Against OpenVMS VAX 6.2 with DECwindows Motif 1.2-3, running under SIMH:
`xdpyinfo` from `DECW$UTILS` and `DECW$CLOCK` both opened on a Linux X.Org
server through `pnw-x11 serve`. DEC's fonts are missing on a stock Linux
X server, so the clock warns about its menu font and falls back to
another; it works regardless.

`ctest` runs both halves between two decnetd nodes, with a stand-in X
server. It checks that the cookie is swapped in, that 200 KB survive the
round trip unchanged, and that a node not on the list is turned away.

On a real desktop, both halves chained through one decnetd also ran real
X programs: `connect` made `:20`, and its clients went out over DECnet
and back in to `serve`, on to Xorg. `xdpyinfo`, `xdpyinfo -ext all` and
`xclock` all worked.

## What it won't do (yet)

- Local X servers only, through their Unix socket. No TCP displays.
- `connect` sends no X login to the far server. A DECwindows server that
  insists on one needs its access control opening up for your node.
- One process per display; run two to serve two.
