# pnw-fs

Mounts a directory on another DECnet node, so ordinary programs can use
it: the "network drive" of Pathworks. It speaks DAP to the node's FAL,
like the other file tools, through FUSE.

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

The directory is checked before mounting: a wrong spec, password or node
is reported at once rather than giving an empty mount.

It needs FUSE 3 (`fuse3` and `libfuse3-dev` on Debian and Ubuntu) and is
not built without it.

## What you see

- **Names.** VMS versions are hidden: `LOGIN.COM;3` is `LOGIN.COM`, and it
  is the highest version. `SUB.DIR` is the directory `SUB`. Subdirectories
  work as you would expect: `~/vms/SUB/X.TXT` is `DUA0:[USER.SUB]X.TXT`.
- **Dates.** The file's revision date, or its creation date if there is
  none.
- **Permissions.** The owner, group and world rights from the file's
  protection. The DEC system class has no Unix equivalent. Every file
  appears to belong to you.
- **Sizes.** What FAL reports. For binary files that is exact. For VMS
  text files it is the size as stored, which is not the size once records
  become lines, so a size shown may be a little off; reading still returns
  every byte.

## Reading and writing

A file is fetched whole when it is opened, converted as `pnw-copy` would.

On a `--rw` mount, a file that is written is kept in memory and sent back
whole when it is closed, as text or binary by its content. An error in
sending shows up as a failed `close`, which most programs report as an
input/output error.

`rm` deletes, and `mv` renames on the node. `chmod`, `chown` and `touch`
are accepted but change nothing, so that `cp -p` and similar work.

Directory listings are kept for five seconds, so a change made on the node
itself can take that long to appear.

## Errors

| You see | The node said |
|---|---|
| No such file or directory | file or directory not found |
| Permission denied | privilege violation, or access control rejected |
| File exists | the file already exists |
| Read-only file system | the mount is read only, or the device is write locked |
| No space left on device | device full |
| Input/output error | anything else; with `-f`, pnw-fs prints the reason |

## Limits

- Whole files in memory: fine for the files DEC systems usually hold, not
  for very large ones.
- Appending (`>>`) to a VMS text file may write at the wrong place, since
  the size it starts from is FAL's.
- Directories cannot be created or removed.
- One request at a time. A slow node makes every program using the mount
  wait.

On VMS every file written is a new version. pnw-fs sends a file only when
something has been written to it since it was last sent, so the shell's
`> file` makes one version, not an empty one and then the real one.

If pnw-fs stops while mounted, programs see "Transport endpoint is not
connected". `fusermount3 -u mountpoint` clears it.
