# pnw-ncp

> `NCP>` *The prompt that launched a thousand* `SHOW KNOWN NODES`.

This is the Network Control Program, or a fair-sized slice of it: it
shows what a DECnet node knows about the network, and tests the path to
one. If you've used NCP on VMS, your fingers already know how this works.

```
pnw-ncp [-s socket] [command]
```

Give it a command and it runs it and exits. Give it nothing and it sits
at an `NCP>` prompt until you type `EXIT` or end the input, just like the
real thing.

| Option | Meaning |
|---|---|
| `-s socket` | decnetd's API socket; default `$DECNETAPI`, then `/tmp/decnetapi.sock` |

## Commands

```
[TELL node] SHOW entity [information]
[TELL node] LIST entity [information]
LOOP NODE node [COUNT n] [LENGTH n]
HELP
EXIT
```

Keywords shorten to three letters, as they always have (`SHO KNO NOD`),
and case doesn't matter. Old habits are welcome here.

### SHOW and LIST

These read information from a node's network management listener (NML,
object 19). Without `TELL`, the node you're asking is your own decnetd.
With it, you can ask any node that runs NML.

Entities:

| Entity | Example |
|---|---|
| `EXECUTOR` | the node itself |
| `NODE name-or-address` | `NODE MIM`, `NODE 1.13` |
| `CIRCUIT name` | `CIRCUIT MUL-0` |
| `LINE name` | `LINE MUL-0` |
| `AREA number` | `AREA 1` |
| `MODULE name` | `MODULE X25-PROTOCOL` |
| `KNOWN`, `ACTIVE`, `ADJACENT`, `SIGNIFICANT` + `NODES`, `CIRCUITS`, `LINES` or `AREAS` | `KNOWN NODES`, `ACTIVE CIRCUITS` |

Information: `SUMMARY` (the default), `STATUS`, `CHARACTERISTICS`,
`COUNTERS`, `EVENTS`.

`LIST` asks for the permanent database. decnetd doesn't keep one, so it
answers "Unrecognized function or option". Other nodes may well have one.

```
$ pnw-ncp tell MIM show known circuits

Known Circuit Volatile Summary as of 30-SEP-2026 13:24:26

Circuit = MUL-0
    State = On
    Adjacent node = 1.2 (NODEB)
```

### LOOP NODE

This sends messages through a node's loopback object (MIRROR, object 25)
and checks they come back unchanged, which is the oldest and best answer
to "is it the network or is it me?" One difference from NCP: the loop
runs from this machine, not from the executor.

| Option | Default | |
|---|---|---|
| `COUNT n` | 1 | messages to send |
| `LENGTH n` | 40 | bytes in each; at most what the node's MIRROR accepts |

```
$ pnw-ncp loop node MIM count 5 length 100
Loop node MIM: 5 x 100 bytes looped, average 42.7 ms
```

## When it goes wrong

Errors come out in NCP's own style, so they'll look familiar:

```
%NCP-F-FAIL, Unrecognized component, ...      the node has no such entity
%NCP-F-CONNECT, Unrecognized node name        could not reach NML or MIRROR
%NCP-F-ERROR, ...                             anything else
```

The exit status is 0 on success, 1 if the command failed, and 2 if decnetd
couldn't be reached.

## What it won't do

`SET`, `DEFINE`, `CLEAR`, `PURGE` and `ZERO`; `LOOP CIRCUIT` and
`LOOP LINE`; access control on `TELL`; Phase II nodes. It looks, it
loops, and it leaves the knobs alone.
