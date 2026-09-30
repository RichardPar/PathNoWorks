# pnw-ncp

Network Control Program: shows what a DECnet node knows about the network,
and tests the path to one. A subset of VMS NCP.

```
pnw-ncp [-s socket] [command]
```

With a command, runs it and exits. Without one, prompts with `NCP>` until
`EXIT` or end of input, as NCP does.

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

Keywords may be shortened to three letters (`SHO KNO NOD`), as in NCP.
Case does not matter.

### SHOW and LIST

Read information from a node's network management listener (NML,
object 19). Without `TELL` the node asked is your own decnetd; with it,
any node that runs NML.

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

`LIST` asks for the permanent database. decnetd has none and answers
"Unrecognized function or option"; other nodes may have one.

```
$ pnw-ncp tell MIM show known circuits

Known Circuit Volatile Summary as of 30-SEP-2026 13:24:26

Circuit = MUL-0
    State = On
    Adjacent node = 1.2 (NODEB)
```

### LOOP NODE

Sends messages through a node's loopback object (MIRROR, object 25) and
checks they come back unchanged. Unlike NCP, the loop is run from this
machine, not by the executor.

| Option | Default | |
|---|---|---|
| `COUNT n` | 1 | messages to send |
| `LENGTH n` | 40 | bytes in each; at most what the node's MIRROR accepts |

```
$ pnw-ncp loop node MIM count 5 length 100
Loop node MIM: 5 x 100 bytes looped, average 42.7 ms
```

## Errors and exit status

Errors are printed in NCP's style:

```
%NCP-F-FAIL, Unrecognized component, ...      the node has no such entity
%NCP-F-CONNECT, Unrecognized node name        could not reach NML or MIRROR
%NCP-F-ERROR, ...                             anything else
```

The exit status is 0 on success, 1 if the command failed, and 2 if decnetd
could not be reached.

## Not supported

`SET`, `DEFINE`, `CLEAR`, `PURGE` and `ZERO`; `LOOP CIRCUIT` and
`LOOP LINE`; access control on `TELL`; Phase II nodes.
