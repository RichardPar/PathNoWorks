# pnw-sethost

Logs in to a DECnet node as VMS's `SET HOST` does, over CTERM (the Network
Command Terminal protocol, object 42).

```
pnw-sethost [options] node
```

```sh
pnw-sethost VAXXY
xterm -ti vt340 -xrm 'XTerm*decTerminalID: vt340' -e pnw-sethost VAXXY
```

| Option | Meaning |
|---|---|
| `-s socket` | decnetd's API socket; default `$DECNETAPI`, then `/tmp/decnetapi.sock` |
| `-t type` | terminal type to report; default `VT300` |
| `--8bit` | pass 8-bit characters and controls straight through (see below) |
| `Ctrl-]` `q` | leave the session from this end, if the node stops answering |
| `Ctrl-]` `Ctrl-]` | send a Ctrl-] |

## The terminal is yours

CTERM carries characters; it does not emulate a terminal. Whatever
terminal pnw-sethost runs in is the terminal the node talks to: escape
sequences go to it untouched and its answers go back. So:

- VMS sees a **VT300-series** terminal of your window's width and height,
  with 8-bit characters. `SHOW TERMINAL` says so.
- `SET TERMINAL/INQUIRE` asks your terminal what it is, and your
  terminal answers. Run in xterm as a VT340 (the command above), VMS then
  reports **SIXEL Graphics** and **ReGIS**, and graphics output from VMS
  displays as it would on a VT340.
- Full-screen programs (EVE, EDT, MAIL, MONITOR) work.

## Line editing

CTERM does line editing at the terminal end, as VMS asks for each read:
echo, Delete, Ctrl-U (erase the line), Ctrl-W (erase a word), Ctrl-R
(show the line again), Ctrl-X (erase the line and anything typed ahead).
Arrow keys and other escape sequences go to VMS, so DCL's command recall
works. Characters typed while VMS is busy are kept and used by its next
read.

Ctrl-Y interrupts, and so does Ctrl-C unless a program has asked for it,
as on a VMS terminal. Ctrl-T shows the process status, if VMS enables it.
Ctrl-O turns output off and on.

## Character sets

VMS sends a VT300 8-bit controls (CSI as the single byte 0x9B) and DEC
Multinational characters, which are almost exactly Latin-1. A terminal
running in UTF-8 cannot display either. When the locale is UTF-8,
pnw-sethost therefore sends 8-bit controls as their 7-bit equivalents,
turns Latin-1 into UTF-8 on the way out, and UTF-8 into Latin-1 on the
way in. Sixel and ReGIS data are 7-bit and pass unchanged.

`--8bit` turns this off, for a terminal set up for 8-bit characters, such
as xterm with `+u8`.

## Messages

```
%PNW-S-CONNECTED, to VAXXY; Ctrl-] q to leave
%PNW-S-DISCONNECTED, link closed by the far end
```

A node without CTERM refuses the connection with "Unrecognized object".

## Tested

Against OpenVMS VAX 6.2: login, DCL editing and command recall,
`SET TERMINAL/INQUIRE` as a VT340, Ctrl-Y and Ctrl-C, EVE, and 8-bit and
accented text through a UTF-8 terminal.

## Not supported

The older VMS-only remote terminal protocol (object 23), used by some
nodes that lack CTERM; modes other than command mode; input and output
flow control beyond what your terminal does itself.

`PNW_TRACE=file` records every CTERM message in hex, for reporting
problems.
