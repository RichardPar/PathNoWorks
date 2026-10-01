# pnw-mail

> `You have 1 new mail message from VAXXY::PNWTEST.`
> *Still the best line a terminal ever printed.*

This sends and receives DECnet mail (Mail-11), the mail behind VMS
`MAIL` and RSX `MAIL`: messages addressed `NODE::USER`, long before
anyone had heard of an `@`.

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

The message comes from standard input, one record per line. It goes out
under your login name, as VMS MAIL's does, so the far end sees it from
`THISNODE::YOU`. Recipients on the same node share one connection, and
each recipient gets its own answer:

```
%PNW-S-SENT, to VAXXY::SYSTEM
%PNW-E-NOTSENT, to VAXXY::NOBODY: %MAIL-E-NOSUCHUSR, no such user NOBODY at node VAXXY
```

One refused recipient doesn't stop the others. The exit status is 0 only
if every recipient got the message.

### Nodes that want a login

The far end's mail server runs as its default DECnet account. A VMS node
without one turns away anonymous mail at the door:

```
%PNW-E-NOTSENT, to VAXXY: Access control rejected
```

In that case, give a login in the address, as you would for file access.
The message still comes from you; the login only lets the mail server
run:

```sh
pnw-mail send 'VAXXY"pnwtest secret"::SYSTEM' < letter.txt
```

## Receiving

`pnw-mail listen` takes the MAIL object (27) on the node decnetd runs,
and appends every message to one mbox file, whoever at this node it's
addressed to. It runs until you stop it, so start it in the background or
from a user service. Any mbox reader will do for reading it:
`mutt -f ~/Mail/decnet`.

```
%PNW-I-LISTENING, for DECnet mail to PNW::, into /home/richard/Mail/decnet
30-Sep 20:30  from VAXXY::PNWTEST  "Final from VMS"  (1 lines)
```

The Mail-11 sender, its To and CC lines and the subject become the
headers. Body lines starting "From " are written as ">From ", because
mbox insists, and has done since about the time VMS was new.

From VMS, it's the usual routine:

```
MAIL> SEND
To:     PNW::RICHARD
```

VMS shows mail from a node it has no name for by its number, as
`29847::RICHARD` (29 × 1024 + 151, which is 29.151). It's correct, but
not friendly. Tell the VMS node your name to fix that:

```
$ MCR NCP DEFINE NODE 29.151 NAME PNW
$ MCR NCP SET NODE 29.151 NAME PNW
```

Only one program can take the MAIL object at a time, so don't also give
decnetd an `object` line for MAIL.

## Tested

With OpenVMS VAX 6.2, in both directions: several recipients, CC, blank
lines, and lines starting "From ". `ctest` runs pnw-mail to pnw-mail
between two decnetd nodes. RSX MAIL hasn't been tried.

## What it won't do

- Mail for every user goes into one file. There's no delivery to local
  users' own mailboxes, and no forwarding to SMTP.
- The listener takes one message at a time; a second sender waits.
- Plain text only: no foreign-format files, and no `MAIL/FOREIGN`.
- DEC never published Mail-11. This follows dnprogs' `sendvmsmail` and
  `vmsmaild`, and what VMS actually does. Nobody knows what the option
  flags in a sender's connect data mean. A sender that sets any of them,
  as VMS does, is taken to send VMS's full header.
