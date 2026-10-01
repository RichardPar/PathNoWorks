# pnw-sethost

> `$ SET HOST VAXXY`
> `Username:` *(and somewhere a VAX fan spins up)*

This logs you in to a DECnet node the way VMS's `SET HOST` does, over
CTERM, the Network Command Terminal protocol (object 42). You type, the
node answers, and for a while it's 1991 again.

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

## Your terminal is the terminal

CTERM carries characters; it doesn't emulate a terminal. Whatever
terminal pnw-sethost runs in *is* the terminal the node talks to. Escape
sequences go to it untouched, and its answers go straight back. That has
some pleasant consequences:

- VMS sees a **VT300-series** terminal of your window's width and height,
  with 8-bit characters. `SHOW TERMINAL` will tell you so.
- `SET TERMINAL/INQUIRE` asks your terminal what it is, and your terminal
  answers for itself. Run it in xterm as a VT340 (the command above) and
  VMS reports **SIXEL Graphics** and **ReGIS**. Graphics output from VMS
  then displays just as it would have on a real VT340, minus the heat and
  the hum.
- Full-screen programs (EVE, EDT, MAIL, MONITOR) work.

## Line editing

CTERM does line editing at the terminal end, as VMS asks for each read:

- echo;
- Delete;
- Ctrl-U (erase the line);
- Ctrl-W (erase a word);
- Ctrl-R (show the line again);
- Ctrl-X (erase the line and anything typed ahead).

Arrow keys and other escape sequences go to VMS, so DCL's command recall
works. Anything you type while VMS is busy is kept and handed to its next
read, so impatient typists lose nothing.

Ctrl-Y interrupts, and so does Ctrl-C unless a program has asked for it,
just as on a VMS terminal. Ctrl-T shows the process status, if VMS has it
enabled. Ctrl-O turns output off and on.

## Character sets

VMS sends a VT300 8-bit controls (CSI as the single byte 0x9B) and DEC
Multinational characters, which are almost exactly Latin-1. A terminal
running in UTF-8 can display neither. So when your locale is UTF-8,
pnw-sethost quietly translates:

- 8-bit controls go out as their 7-bit equivalents;
- Latin-1 becomes UTF-8 on the way out;
- UTF-8 becomes Latin-1 on the way in.

Sixel and ReGIS data are 7-bit and pass through unchanged.

`--8bit` turns all that off, for a terminal set up for 8-bit characters,
such as xterm with `+u8`.

## Messages

```
%PNW-S-CONNECTED, to VAXXY; Ctrl-] q to leave
%PNW-S-DISCONNECTED, link closed by the far end
```

A node without CTERM refuses the connection with "Unrecognized object".

## Tested

Against OpenVMS VAX 6.2:

- logging in;
- DCL editing and command recall;
- `SET TERMINAL/INQUIRE` as a VT340;
- Ctrl-Y and Ctrl-C;
- EVE;
- 8-bit and accented text through a UTF-8 terminal.

## Not supported

The older VMS-only remote terminal protocol (object 23), which some nodes
without CTERM use; modes other than command mode; and input and output
flow control beyond what your terminal does on its own.

`PNW_TRACE=file` records every CTERM message in hex, which is very useful
when reporting a problem and very dull otherwise.
