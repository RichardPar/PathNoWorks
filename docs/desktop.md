# The PathNoWorks desktop

> *For when you'd rather double-click than type `DIR`.*

`pathnoworks` puts all the command-line tools behind a window. You can
see the DECnet nodes, browse a node's files and copy them in either
direction, log in to a node, and send mail, all with a mouse. That's
about as close to a Pathworks desk as Linux has got.

```sh
pathnoworks                      # the network
pathnoworks --files VAXXY        # straight to VAXXY's files
pathnoworks --socket PATH        # another decnetd, for this run
```

It needs Qt 6 (`qt6-base-dev` to build it), and xterm for terminals. It
gets built with everything else whenever CMake finds Qt 6, and ends up as
`build/gui/pathnoworks`. `cmake --install` also puts it in your desktop
menu.

## The network

![The network window](images/network.png)

The main window lists the nodes decnetd knows, with each node's state,
hops and cost. *Reachable only* hides the rest, and you'll want it:
HECnet has well over a thousand nodes. Type part of a name or address to
find the one you're after.

There's one wrinkle. An endnode only knows that its router is reachable,
which makes for a short and unhelpful list. So on an endnode, PathNoWorks
asks the router instead, just as you'd type
`TELL router SHOW KNOWN NODES`, and says so in the status bar. Any names
the router doesn't know are filled in from this node's own list.

Double-click a node to see its files. **Terminal** logs in to it, and
**Mail** writes to someone on it. Right-click a node and you get the same
actions. *PathNoWorks → Settings* sets decnetd's API socket; by default
it's `$DECNETAPI`, then `/tmp/decnetapi.sock`.

## Files

![A file window on a VMS node](images/files.png)

A node's files open in a window of their own:

- **Browsing.** Double-click a directory to open it. **Up** goes back,
  and on VMS it keeps going above where you started: `[]` to `[-]`,
  `[A.B]` to `[A]`. You can also type a directory in the path box and
  press Enter: `DUA0:[USER]`, `SYS$LOGIN:`, `[.SUB]`, or `pub/` on a Unix
  FAL.
- **Looking at a file.** Double-click it. Text shows up in a viewer;
  binary files just say so.
- **Copying.** **Copy here** copies the selected files to a local folder.
  **Copy there** copies local files to the directory shown, and so does
  dropping files on the window from your file manager. Text and binary
  are told apart the same way `pnw-copy` does it.
- **Rename** (F2) and **Delete** (Del) act on the selected files.
- **Mount** mounts the directory shown at `~/DECnet/NODE` with `pnw-fs`,
  read and write, and opens it in your file manager: your network drive,
  one click away. **Unmount** undoes that.
- **Terminal** logs in to the node.

The window starts in your login directory. It works out for itself
whether the node uses VMS names (`[DIR]FILE.TXT;1`) or Unix ones
(`dir/file.txt`), by having a look.

If a node won't talk to you without a login, the window asks for one and
then tries again. Leave the user empty to use the node's default DECnet
account, or tick the proxy box to ask for proxy access under your own
login name. The login lasts as long as the window does.

Everything runs in the background, so the window never freezes while a
slow VAX thinks it over. Each window does one thing at a time, and the
status bar tells you how it went.

## Terminals

**Terminal** opens xterm as a VT340, running `pnw-sethost`. DEC graphics
work, Sixel and ReGIS included, and `SET TERMINAL/INQUIRE` sees a VT340.
There's more in [pnw-sethost](pnw-sethost.md); `Ctrl-]` `q` gets you
out.

## Mail

![The mail window](images/mail.png)

**Mail** writes a message to `NODE::USER`, or several separated by
commas, or `NODE"user password"::USER` for a node that wants a login (see
[pnw-mail](pnw-mail.md)). Each recipient is reported as sent or not, and
the mail goes out under your login name. The window closes once
everything has gone. If something didn't, it stays open and tells you
what failed.

To receive DECnet mail, run `pnw-mail listen`.

## Tested

`ctest` drives the windows offscreen against two decnetd nodes, one
running dnfal. It:

- browses into a subdirectory and back;
- copies a file here;
- copies a text and a binary file there;
- renames, deletes, and views a file;
- tries a missing directory.

The same test can also run against a real VMS node, and it has, against
OpenVMS VAX 6.2:

```sh
QT_QPA_PLATFORM=offscreen PNW_TEST_SOCKET=/tmp/decnetapi.sock \
    PNW_TEST_VMS='VAXXY"user password"' build/tests/test_gui vms_node
```

## What it won't do (yet)

- **Mount puts the login on pnw-fs's command line,** where `ps` shows it
  to other local users for as long as it's mounted. On a shared machine,
  use proxy access or a node's default account instead.
- **You can't drag files out of the window yet.** Use Copy here.
- Copy here and Delete work on files only, not whole directories.
- There are no LAT terminals or MOP in the desktop yet; `pnw-lat` and
  `pnw-mop` are waiting on the command line.
