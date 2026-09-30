# pnw-mail

Sends and receives DECnet mail (Mail-11), the mail of VMS `MAIL` and RSX
`MAIL`: messages addressed `NODE::USER`.

```
pnw-mail [options] send [-s subject] NODE::USER[,NODE::USER...]
pnw-mail [options] listen [-o mbox]
```

```sh
echo "Hello" | pnw-mail send -s "Greetings" VAXXY::SYSTEM
pnw-mail send -s "Notes" VAXXY::SYSTEM,BAJI::RICHARD < notes.txt
pnw-mail listen                          # mail for this node -> ~/Mail/decnet
```

| Option | Meaning |
|---|---|
| `--socket s` | decnetd's API socket; default `$DECNETAPI`, then `/tmp/decnetapi.sock` |
| `--trace` | show every Mail-11 record, for reporting problems |
| `-s subject` | (send) the subject; default "No subject" |
| `-o mbox` | (listen) the mbox file to append to; default `~/Mail/decnet` |

## Sending

The message is standard input, one record a line. It goes from your login
name, as VMS MAIL's does: the far end sees it from `THISNODE::YOU`.
Recipients on the same node share one connection. Each recipient gets
its own answer:

```
%PNW-S-SENT, to VAXXY::SYSTEM
%PNW-E-NOTSENT, to VAXXY::NOBODY: %MAIL-E-NOSUCHUSR, no such user NOBODY at node VAXXY
```

A refused recipient does not stop the others. The exit status is 0 only
if every recipient got the message.

### Nodes that want a login

The far end's mail server runs as its default DECnet account. A VMS node
without one refuses anonymous mail:

```
%PNW-E-NOTSENT, to VAXXY: Access control rejected
```

Give a login in the address, as for file access. The message is still
from you; the login only lets the mail server run:

```sh
pnw-mail send 'VAXXY"pnwtest secret"::SYSTEM' < letter.txt
```

## Receiving

`pnw-mail listen` takes the MAIL object (27) on the node decnetd runs and
appends every message to one mbox file, whatever user at this node it is
addressed to. It runs until stopped; start it in the background or from
a user service. Read the file with any mbox reader: `mutt -f ~/Mail/decnet`.

```
%PNW-I-LISTENING, for DECnet mail to PNW::, into /home/richard/Mail/decnet
30-Sep 20:30  from VAXXY::PNWTEST  "Final from VMS"  (1 lines)
```

The Mail-11 sender, its To and CC lines and the subject become the
headers. Body lines starting "From " are written as ">From ", as mbox
requires.

From VMS:

```
MAIL> SEND
To:     PNW::RICHARD
```

VMS shows mail from a node it has no name for by address, as
`29847::RICHARD` (29 × 1024 + 151, which is 29.151). Name this node on
the VMS node to fix that:

```
$ MCR NCP DEFINE NODE 29.151 NAME PNW
$ MCR NCP SET NODE 29.151 NAME PNW
```

Only one program can take the MAIL object at a time. Do not also give
decnetd an `object` line for MAIL.

## Tested

With OpenVMS VAX 6.2, in both directions: several recipients, CC,
blank lines, and lines starting "From ". `ctest` runs pnw-mail to
pnw-mail between two decnetd nodes. RSX MAIL has not been tried.

## Limits

- Mail for every user goes to one file. There is no delivery to local
  users' own mailboxes, and no forwarding to SMTP.
- The listener takes one message at a time; a second sender waits.
- Only plain text: no foreign-format files, and no `MAIL/FOREIGN`.
- DEC never published Mail-11. This follows dnprogs' `sendvmsmail` and
  `vmsmaild` and what VMS does. The meaning of the option flags in a
  sender's connect data is not known. A sender that sets any of them, as
  VMS does, is taken to send VMS's full header.
