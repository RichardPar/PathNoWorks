# File access: pnw-dir, pnw-type, pnw-copy, pnw-delete, pnw-rename

> *`COPY` over DECnet was a small miracle in 1985. It still feels like one.*

These five talk to a node's File Access Listener (FAL, object 17) using
DAP, the Data Access Protocol. That's the same machinery behind VMS's
`DIRECTORY`, `TYPE`, `COPY`, `DELETE` and `RENAME` when you put a node
name in front of the file. They work with VMS and RSX, with PyDECnet's
`fal.py`, and with cppdecnet's `dnfal`.

```
pnw-dir    [options] NODE::spec
pnw-type   [options] NODE::file
pnw-copy   [options] NODE::files local
pnw-copy   [options] local... NODE::destination
pnw-delete [options] NODE::spec
pnw-rename [options] NODE::file newname
```

## Remote file specifications

```
NODE::file
NODE"user password"::file
NODE"user password account"::file
```

Whatever follows the `::` goes to the remote node exactly as you typed
it. These tools don't try to understand it, so write it the way *that*
node expects:

| Node | Examples |
|---|---|
| VMS | `DUA0:[USER]LOGIN.COM;1`, `SYS$LOGIN:*.COM`, `[.SUB]NOTES.TXT` |
| RSX | `DU0:[200,1]LOGIN.CMD` |
| dnfal, PyDECnet | `notes.txt`, `sub/notes.txt`; dnfal also takes VMS-style `[SUB]NOTES.TXT;1` |

Wildcards (`*`, `%`, `?`) are handled by the remote node, too.

**Quote every remote spec.** I know, I keep saying it. The shell treats
`[ ] * ; " $` specially, and the quotes around a user and password have
to survive the trip to the tool:

```sh
pnw-dir 'VMSNOD"richard secret"::[RICHARD]*.TXT'
```

A small trap: VMS upper-cases a password typed without quotes. These
tools send it exactly as you typed it. dnfal accepts either case for a
lower-case password.

## Who are you, anyway?

- **A user and password in the spec** are sent to the remote node as a
  login.
- **Without them** the connection is anonymous. VMS uses its default
  DECnet account, dnfal its `*` entry, and some nodes simply refuse.
- **`--proxy`** asks for proxy access instead. The remote node is told
  your local user name and looks in its proxy database to decide whose
  files you may reach. This is what VMS does by default. It isn't the
  default here, because PyDECnet's FAL treats any user name as a login,
  and then stops confining itself to its directory. Better to opt in.

## Common options

| Option | Meaning |
|---|---|
| `-s socket` | decnetd's API socket; default `$DECNETAPI`, then `/tmp/decnetapi.sock` |
| `--proxy` | without a user in the spec, ask for proxy access as the local user |
| `--trace` | print each DAP message sent and received, on standard error |
| `-h`, `--help` | usage |

## pnw-dir

Lists files, with size in blocks, revision date, owner and protection,
when the node bothers to send them.

```
$ pnw-dir 'MIM::*'
Directory MIM::/

hello.txt                             1  30-SEP-26 14:16:08  [richard]  (,RWD,RWD,R)
sub/                                  8  30-SEP-26 14:16:08  [richard]  (,RWED,RWED,RE)

Total of 2 files, 9 blocks.
```

Protection is shown the VMS way: system, owner, group and world, each
with the rights granted. Unix-based FALs mark directories with a trailing
`/`. If nothing matched, the exit status is 1.

## pnw-type

Writes a remote file to standard output, converted just as `pnw-copy`
would convert it. Handy with `less`.

## pnw-copy

Copies files in either direction. One side has to be remote and the
other local; there's no copying from node to node without stopping here
first.

**From a remote node:**

```sh
pnw-copy 'MIM::HECNET.DAT' nodes.dat           # to a file
pnw-copy 'MIM::HECNET.DAT' .                   # into a directory
pnw-copy 'VMS"u p"::[U]*.COM' ./coms/          # every match
```

A wildcard needs a local directory to land in. Each file is named after
the remote one, minus its version number.

**To a remote node:**

```sh
pnw-copy notes.txt 'VMS"u p"::[U]'             # into a directory
pnw-copy notes.txt 'VMS"u p"::[U]OLD.TXT'      # under another name
pnw-copy *.txt 'VMS"u p"::[U]'                 # several files
```

A destination ending in `::`, `]`, `>`, `:` or `/` counts as a directory,
and the local file's name gets added. Several files need a destination
like that, and they all go over one connection.

**Text and binary.** This is the bit that used to cause arguments in the
computer room. DEC systems keep text as records; Linux keeps it as lines
with newlines on the end. Left to itself, pnw-copy does this:

- **Receiving:** if a file's records imply line ends (carriage return,
  FORTRAN or print control, as VMS text files have), it arrives with one
  newline per record. Anything else is copied as stored, cut to its real
  length.
- **Sending:** a file with no NUL bytes and almost nothing but printable
  characters goes as text. That means variable-length records with
  carriage return control, one per line, which is how VMS keeps text.
  Anything else goes as 512-byte fixed-length records, VMS's own binary
  format, when it's a whole number of blocks. Otherwise it goes as
  variable-length records of up to 512 bytes, since VMS refuses a short
  last fixed-length record. Either way, the file comes back exactly as it
  was sent.

`--text` or `--binary` overrides the guess, for the times it guesses
wrong.

**Partial files.** A file being received is written to
`name.pnw-partial` and only renamed once it's complete, so a failed copy
never leaves a half-written file pretending to be the real one. If the
remote end is dnfal, it only shows a file sent to it once the transfer
completes; other FALs do whatever they do.

Progress lines go to standard error:

```
MIM::HECNET.DAT -> ./HECNET.DAT (48213 bytes, Variable, text)
```

## pnw-delete

Deletes whatever files a spec matches. Wildcards are allowed, so
double-check that spec before pressing Enter.

```sh
pnw-delete 'VMS"u p"::[U]*.LIS;*'
```

## pnw-rename

Renames one file on one node. The new name may repeat the node, but it
can't name a different one; files don't fly between nodes on a rename.

```sh
pnw-rename 'VMS"u p"::[U]NOTES.TXT' OLDNOTES.TXT
```

## When it goes wrong

Errors from the remote node are its DAP status, in DAP's own slightly
formal words:

```
pnw-type: Open error: file not found.
pnw-copy: Open error: privilege violation (OS denies access).
pnw-dir: cannot connect to FAL: Access control rejected
```

The exit status is 0 on success, 2 for the wrong number of arguments, and
1 for any other failure.

## What it won't do (yet)

- Files are read and written whole, in order: no record or block access,
  and no appending to a remote file.
- Directories can't be created or removed.
- It's been tested against OpenVMS VAX 6.2, dnfal and PyDECnet's FAL. RSX
  follows the same protocol but hasn't been tried yet.
