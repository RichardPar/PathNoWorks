# pnw-lat

Connects to a LAT service, as a DEC terminal server does. LAT (Local Area
Transport) is how terminals reached VMS and RSX hosts on an Ethernet LAN,
without DECnet.

```
pnw-lat [options] -l
pnw-lat [options] service [node]
```

```sh
pnw-lat -l                          # what is on offer
pnw-lat BAJI                        # connect
xterm -ti vt340 -e pnw-lat BAJI     # with a VT340's graphics
```

| Option | Meaning |
|---|---|
| `-l` | list the services announced on the LAN |
| `-i iface` | interface to use; default, the one with the default route |
| `-w secs` | how long to listen for announcements; default 45 |
| `-n` | ignore services remembered from before, and listen |
| `--8bit` | pass 8-bit characters and controls straight through |
| `Ctrl-]` `q` | leave |
| `Ctrl-]` `b` | send a break |
| `Ctrl-]` `Ctrl-]` | send a Ctrl-] |

## Setting up

LAT is not routed and does not use DECnet: it runs straight on Ethernet,
and needs no decnetd. It does need to send raw Ethernet frames, which
takes a privilege. That is given once to a small helper, `pnw-latsock`,
which opens the socket and hands it to `pnw-lat`; everything else runs
as you:

```sh
sudo setcap cap_net_raw+ep build/tools/pnw-lat/pnw-latsock
```

Anyone who can run the helper can also watch LAT traffic on the LAN.

Wi-Fi works if the access point bridges to the wired LAN, as most home
access points do. LAT, unlike DECnet, uses the station's own address, so
nothing on the way needs to change. Check with
`sudo tcpdump -i wlo1 ether proto 0x6004`: announcements from the LAN's
hosts should appear every 20 seconds or so.

## Finding services

Hosts announce their services every 20 seconds or so. Every service
heard is remembered, in `~/.cache/pnw-lat/services`, so connecting to one
seen before is immediate. Otherwise pnw-lat listens for up to 45
seconds. Wi-Fi loses multicast easily, and a missed announcement means
another 20 seconds, which is why the wait is that long. If a remembered
host no longer answers, pnw-lat forgets it; run it again, or run
`pnw-lat -l`.

A service offered by more than one node is reached at the first one
heard; name the node to choose.

## The terminal

As with pnw-sethost, the terminal you run pnw-lat in is the terminal the
host talks to. LAT carries characters both ways and does nothing to
them, so escape sequences, Sixel and ReGIS pass through. In a UTF-8
locale, 8-bit controls and Latin-1 text are translated as pnw-sethost
does; `--8bit` turns that off.

## Messages

```
%PNW-I-LOOKING, for LAT service BAJI on wlo1
%PNW-S-CONNECTING, to BAJI on BAJI (aa-00-04-00-9f-74); Ctrl-] q to leave
%PNW-S-CONNECTED
%PNW-S-DISCONNECTED, logged out
```

A host that refuses says why: "no such service", "service in use",
"access denied" and so on.

## Tested

Against RSX-11M-PLUS on a real PDP-11 and under SIMH, over Wi-Fi:
connecting, command output, idle circuits kept alive, and leaving. VMS
has not been tried; start LAT there with `@SYS$STARTUP:LAT$STARTUP`.

## Limits

One session per run, to a service; no queued access, no named ports on
terminal servers, no service passwords. DEC never published LAT: the
protocol is built from latd's description of it and checked against RSX.

`PNW_TRACE=file` records every LAT message in hex, for reporting
problems.
