# pnw-fs

> *Drive N: was always the VAX. Now it's `~/vms`.*

This was the whole point of Pathworks, really: a directory on the VAX
showing up on your PC as if it were a local disk. pnw-fs does the same
for Linux. It mounts a directory on another DECnet node so that ordinary
programs (your editor, `grep`, the file manager) can use it without ever
knowing DECnet is involved. Underneath, it speaks DAP to the node's FAL
like the other file tools, and talks to the kernel through FUSE.

```
pnw-fs [options] NODE::directory mountpoint
fusermount3 -u mountpoint
```

```sh
mkdir ~/vms
pnw-fs 'VMSNOD"user password"::DUA0:[USER]' ~/vms
ls -l ~/vms
cat ~/vms/LOGIN.COM
fusermount3 -u ~/vms
```

| Option | Meaning |
|---|---|
| `--rw` | allow writing, deleting and renaming; without it the mount is read only |
| `--proxy` | without a user in the spec, ask for proxy access as the local user |
| `-s socket` | decnetd's API socket; default `$DECNETAPI`, then `/tmp/decnetapi.sock` |
| `--trace` | print each DAP message on standard error (use with `-f`) |
| `-f` | stay in the foreground rather than running in the background |
| `-d` | FUSE debugging output; implies `-f` |
| `-o options` | FUSE mount options |

pnw-fs checks the directory before it mounts anything. A wrong spec,
password or node gets reported straight away, rather than leaving you
staring at an empty mount and wondering.

Read-only is the default on purpose. Add `--rw` when you mean it.

It needs FUSE 3 (`fuse3` and `libfuse3-dev` on Debian and Ubuntu), and
isn't built without it.

## What you'll see

- **Names.** VMS versions are hidden: `LOGIN.COM;3` shows up as
  `LOGIN.COM`, and it's always the highest version. `SUB.DIR` becomes the
  directory `SUB`. Subdirectories work the way you'd hope:
  `~/vms/SUB/X.TXT` is `DUA0:[USER.SUB]X.TXT`.
- **Dates.** The file's revision date, or its creation date if it hasn't
  got one.
- **Permissions.** Owner, group and world rights come from the file's
  protection. VMS's system class has no Unix equivalent, so it quietly
  drops out. Every file appears to belong to you.
- **Sizes.** Whatever FAL reports. For binary files that's exact. For VMS
  text files it's the size as stored, which isn't the size once records
  become lines, so a size shown may be a little off. Reading still
  returns every byte; it's only `ls` that's being optimistic.

## Reading and writing

A file is fetched whole when it's opened, converted as `pnw-copy` would.

On a `--rw` mount, a file that's written is kept in memory and sent back
whole when it's closed, as text or binary depending on its content. If
sending fails, you'll see it as a failed `close`, which most programs
report as an input/output error.

`rm` deletes, and `mv` renames on the node. `chmod`, `chown` and `touch`
are accepted politely but change nothing, so that `cp -p` and friends
don't fall over.

Directory listings are cached for five seconds, so a change made on the
node itself can take that long to appear.

## When it goes wrong

| You see | The node said |
|---|---|
| No such file or directory | file or directory not found |
| Permission denied | privilege violation, or access control rejected |
| File exists | the file already exists |
| Read-only file system | the mount is read only, or the device is write locked |
| No space left on device | device full |
| Input/output error | anything else; with `-f`, pnw-fs prints the reason |

## What it won't do

- Whole files live in memory. That's fine for the files DEC systems
  usually hold, and not for very large ones.
- Appending (`>>`) to a VMS text file may write at the wrong place, since
  the size it starts from is FAL's.
- Directories can't be created or removed.
- One request at a time. A slow node makes every program using the mount
  wait its turn, just like a shared terminal room.

A nice VMS habit survives: every file written is a new version. pnw-fs
only sends a file when something has actually been written to it since
it was last sent, so the shell's `> file` makes one new version, rather
than an empty one followed by the real one.

If pnw-fs stops while still mounted, programs will see "Transport
endpoint is not connected". `fusermount3 -u mountpoint` clears it.
