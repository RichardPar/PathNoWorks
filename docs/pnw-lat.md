# pnw-lat

> `Local> CONNECT BAJI`
> *What every DECserver user typed a hundred times a day.*

This connects to a LAT service the way a DEC terminal server does. LAT
(Local Area Transport) was how rooms full of terminals reached VMS and
RSX hosts over Ethernet, without DECnet getting involved at all. It was
fast, chatty and refreshingly simple, and for years it just worked.

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

LAT isn't routed and doesn't use DECnet. It runs straight on the
Ethernet, so it doesn't need decnetd at all. What it does need is the
right to send raw Ethernet frames, and that takes a privilege. Rather
than run everything as root, that privilege goes once to a tiny helper,
`pnw-latsock`. It opens the socket and hands it over to `pnw-lat`, and
everything else runs as you:

```sh
sudo setcap cap_net_raw+ep build/tools/pnw-lat/pnw-latsock
```

Bear in mind that anyone who can run the helper can also watch LAT
traffic on the LAN.

Wi-Fi works too, if your access point bridges to the wired LAN, as most
home access points do. Unlike DECnet, LAT uses the station's own
address, so nothing along the way needs reconfiguring. To check, run
`sudo tcpdump -i wlo1 ether proto 0x6004`: announcements from the LAN's
hosts should turn up every 20 seconds or so.

## Finding services

Hosts announce their services every 20 seconds or so. Every service
pnw-lat hears is remembered, in `~/.cache/pnw-lat/services`, so
connecting to one it's seen before is immediate. Otherwise pnw-lat
listens for up to 45 seconds. That sounds long, but Wi-Fi loses multicast
easily, and every missed announcement costs another 20 seconds. If a
remembered host no longer answers, pnw-lat forgets it; run it again, or
run `pnw-lat -l`.

If more than one node offers a service, you get the first one heard. Name
the node if you want a particular one.

## The terminal

As with pnw-sethost, the terminal you run pnw-lat in is the terminal the
host talks to. LAT carries characters both ways and doesn't touch them,
so escape sequences, Sixel and ReGIS all pass through. In a UTF-8 locale,
8-bit controls and Latin-1 text are translated just as pnw-sethost does;
`--8bit` turns that off.

## Messages

```
%PNW-I-LOOKING, for LAT service BAJI on wlo1
%PNW-S-CONNECTING, to BAJI on BAJI (aa-00-04-00-9f-74); Ctrl-] q to leave
%PNW-S-CONNECTED
%PNW-S-DISCONNECTED, logged out
```

A host that refuses tells you why: "no such service", "service in use",
"access denied" and so on.

## Tested

Against RSX-11M-PLUS on a real PDP-11 and under SIMH, over Wi-Fi:
connecting, command output, idle circuits kept alive, and leaving. VMS
hasn't been tried yet; you can start LAT there with
`@SYS$STARTUP:LAT$STARTUP`.

## What it won't do

One session per run, to a service. No queued access, no named ports on
terminal servers, and no service passwords.

DEC never published LAT. The protocol here is pieced together from
latd's description of it and checked against RSX. It's been kept honest
by a real PDP-11 that doesn't forgive mistakes.

`PNW_TRACE=file` records every LAT message in hex, for reporting
problems.
