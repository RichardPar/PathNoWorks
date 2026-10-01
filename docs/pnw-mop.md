# pnw-mop

Uses MOP, the Maintenance Operation Protocol, to see what is on the
Ethernet. You can list the stations that announce themselves, ask one who
it is, read its Ethernet counters, and loop test messages through it.
VMS, RSX, DECservers and DECnet routers all speak MOP, whether or not
they run DECnet.

```
pnw-mop [options] list
pnw-mop [options] id STATION
pnw-mop [options] counters STATION
pnw-mop [options] loop [STATION[,STATION...]] [-n count] [-f]
pnw-mop [options] circuits
```

```sh
pnw-mop list                     # stations heard on the LAN
pnw-mop id BAJI                  # ask BAJI who it is
pnw-mop counters aa-00-04-00-9f-74
pnw-mop loop BAJI -n 5           # five messages to BAJI and back
pnw-mop loop                     # through whoever answers first
```

A station is an Ethernet address (`aa-00-04-00-9f-74` or
`aa:00:04:00:9f:74`), or a DECnet node name or address. A node name or
address stands for its DECnet Ethernet address, `AA-00-04-00-...`, which
any station running DECnet uses.

| Option | Meaning |
|---|---|
| `--socket s` | decnetd's API socket; default `$DECNETAPI`, then `/tmp/decnetapi.sock` |
| `-c circuit` | the MOP circuit, if decnetd has more than one |
| `-t secs` | how long to wait for each answer; default 3 |
| `-n count` | (loop) messages to send; default 1 |
| `-f` | (loop) no pause between messages; by default there is a second's pause |

## Setting up

MOP runs on an Ethernet, not over DECnet links, so pnw-mop works through
a decnetd that has an Ethernet circuit with `--mop`. That decnetd also
needs an `api` line:

```
circuit eth-0 Ethernet pcap:eth0 --mop
api /run/decnet/api.sock
```

A decnetd that runs only MOP, with no `routing` line, is enough.

### A decnetd on another machine

Often the decnetd on the LAN is not the one on your desktop. On Wi-Fi, for
example, DECnet usually has to run on a wired machine. SSH can carry its
API socket to you. On that machine, give the API to your user only:

```
api /home/richard/decnet-api.sock --mode 600
```

Then, on your desktop:

```sh
ssh -N -L ~/.cache/lan-decnet.sock:/home/richard/decnet-api.sock richard@gateway &
pnw-mop --socket ~/.cache/lan-decnet.sock list
```

## Commands

`list` shows the system IDs the circuit has heard. Stations announce
themselves every 8 to 12 minutes, so a decnetd started a moment ago has
heard nothing yet; `id` asks at once.

```
Station            DECnet     Software                  Device                  Heard
AA-00-04-00-9D-74  29.157     (maintenance system)      DESVA Microvax-2000, ~  3s ago
AA-00-04-00-9E-74  29.158                               DEUNA UNIBUS CSMA/CD ~  0s ago
AA-00-04-00-9F-74  29.159                               DEUNA UNIBUS CSMA/CD ~  0s ago
```

`id` asks one station for its system ID and shows all of it: software,
device, processor, the services it offers (loop, counters, console
carrier, boot...), and who holds its console, if anyone does.

`counters` shows the station's Ethernet counters, in NCP's words: bytes
and blocks sent and received, collisions, and failures.

`loop` sends messages that the station sends back. With several stations,
the message goes through each in turn, then back here; MOP allows three.
With none, it goes to the loopback assistance address, and the first
station to answer is the one used. Each message reports its round trip:

```
  1  reply from AA-00-04-00-9F-74 in 54.60 ms
  2  reply from AA-00-04-00-9F-74 in 12.16 ms
%PNW-S-LOOPED, 2 of 2 answered, round trip 12.16/33.38/54.60 ms (min/avg/max)
```

The exit status is 0 only if everything asked was answered.

## Tested

On a real LAN, through decnetd on a gateway, over SSH:

- **RSX-11M-PLUS on a real PDP-11 and under SIMH:** `id` and `loop`. RSX
  reports its device and MOP version, but no software or services, and
  does not answer `counters`.
- **OpenVMS VAX 6.2 under SIMH:** `id`, `counters` and `loop`. The
  answer says "maintenance system", with SIMH's controller address as the
  hardware address, so it probably comes from SIMH's emulated Ethernet
  controller rather than from VMS.

`ctest` runs every command against two decnetd stations on an Ethernet
carried over UDP.

## Limits

No console carrier (a remote console on a DECserver or a VAX), and no
downline load or upline dump. Counters come back as the station keeps
them. decnetd's own are only the ones software can count, so its error
counters are always zero.

The API is PyDECnet's `mop` API, with additions:

- a `dest` on `sysid`, to ask one station;
- node names as stations.
