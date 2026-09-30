# File access: pnw-dir, pnw-type, pnw-copy, pnw-delete, pnw-rename

These talk to a node's File Access Listener (FAL, object 17) using DAP, as
VMS's `DIRECTORY`, `TYPE`, `COPY`, `DELETE` and `RENAME` do over DECnet.
They work with VMS and RSX, with PyDECnet's `fal.py`, and with cppdecnet's
`dnfal`.

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

What follows the `::` is passed to the remote node as it is, so write it
the way that node expects:

| Node | Examples |
|---|---|
| VMS | `DUA0:[USER]LOGIN.COM;1`, `SYS$LOGIN:*.COM`, `[.SUB]NOTES.TXT` |
| RSX | `DU0:[200,1]LOGIN.CMD` |
| dnfal, PyDECnet | `notes.txt`, `sub/notes.txt`; dnfal also takes VMS-style `[SUB]NOTES.TXT;1` |

Wildcards (`*`, `%`, `?`) are handled by the remote node.

**Quote every remote spec.** The shell treats `[ ] * ; " $` specially, and
the quotes around a user and password must reach the tool:

```sh
pnw-dir 'VMSNOD"richard secret"::[RICHARD]*.TXT'
```

VMS upper-cases a password typed without quotes. These tools send it as
you typed it; dnfal accepts either case for a lower-case password.

## Access control

- With a user and password in the spec, they are sent to the remote node
  as a login.
- Without them the connection is anonymous: VMS uses its default DECnet
  account, dnfal its `*` entry, and some nodes refuse.
- `--proxy` asks for proxy access instead: the remote node is told your
  local user name and decides, from its proxy database, whose files you
  may reach. This is what VMS does by default. It is not the default here,
  because PyDECnet's FAL treats any user name as a login and then no
  longer confines itself to its directory.

## Common options

| Option | Meaning |
|---|---|
| `-s socket` | decnetd's API socket; default `$DECNETAPI`, then `/tmp/decnetapi.sock` |
| `--proxy` | without a user in the spec, ask for proxy access as the local user |
| `--trace` | print each DAP message sent and received, on standard error |
| `-h`, `--help` | usage |

## pnw-dir

Lists files, with size in blocks, revision date, owner and protection when
the node supplies them.

```
$ pnw-dir 'MIM::*'
Directory MIM::/

hello.txt                             1  30-SEP-26 14:16:08  [richard]  (,RWD,RWD,R)
sub/                                  8  30-SEP-26 14:16:08  [richard]  (,RWED,RWED,RE)

Total of 2 files, 9 blocks.
```

Protection is shown as VMS does: system, owner, group and world, each with
the rights granted. Unix-based FALs mark directories with a trailing `/`.
The exit status is 1 if nothing matched.

## pnw-type

Writes a remote file to standard output, converted as for `pnw-copy`.

## pnw-copy

Copies files in either direction. One side must be remote and the other
local.

**From a remote node:**

```sh
pnw-copy 'MIM::HECNET.DAT' nodes.dat           # to a file
pnw-copy 'MIM::HECNET.DAT' .                   # into a directory
pnw-copy 'VMS"u p"::[U]*.COM' ./coms/          # every match
```

A wildcard needs a local directory to copy into. Each file is named after
the remote file, without its version.

**To a remote node:**

```sh
pnw-copy notes.txt 'VMS"u p"::[U]'             # into a directory
pnw-copy notes.txt 'VMS"u p"::[U]OLD.TXT'      # under another name
pnw-copy *.txt 'VMS"u p"::[U]'                 # several files
```

A destination ending in `::`, `]`, `>`, `:` or `/` is a directory, and the
local file's name is added. Several files need such a destination; they go
over one connection.

**Text and binary.** DEC systems keep text as records; Linux keeps it as
lines. Without an option:

- Receiving: a file whose records imply line ends (carriage return,
  FORTRAN or print control, as VMS text files have) arrives with one
  newline per record. Anything else is copied as stored, cut to its real
  length.
- Sending: a file with no NUL bytes and almost nothing but printable
  characters goes as text: variable-length records with carriage return
  control, one per line, which is how VMS keeps text. Anything else goes
  as 512-byte fixed-length records, VMS's own binary format, when it is a
  whole number of blocks; otherwise as variable-length records of up to
  512 bytes, since VMS refuses a short last fixed-length record. Either
  way the file comes back exactly as it was sent.

`--text` or `--binary` overrides the choice.

**Partial files.** A file being received is written to
`name.pnw-partial` and renamed when it is complete, so a failed copy
leaves nothing half-written. A remote node sees a file sent to it only
when the transfer completes, if it is dnfal; other FALs behave as they do.

Progress lines go to standard error:

```
MIM::HECNET.DAT -> ./HECNET.DAT (48213 bytes, Variable, text)
```

## pnw-delete

Deletes the files a spec matches; wildcards allowed.

```sh
pnw-delete 'VMS"u p"::[U]*.LIS;*'
```

## pnw-rename

Renames one file on one node. The new name may repeat the node, but may
not name another.

```sh
pnw-rename 'VMS"u p"::[U]NOTES.TXT' OLDNOTES.TXT
```

## Errors and exit status

Errors from the remote node are its DAP status, in DAP's words:

```
pnw-type: Open error: file not found.
pnw-copy: Open error: privilege violation (OS denies access).
pnw-dir: cannot connect to FAL: Access control rejected
```

The exit status is 0 on success, 2 for the wrong number of arguments, and
1 for any other failure.

## Limits

- Files are read and written whole, in order: no record or block access,
  no appending to a remote file.
- Directories cannot be created or removed.
- Tested against OpenVMS VAX 6.2, dnfal and PyDECnet's FAL. RSX follows
  the same protocol but has not been tried yet.
