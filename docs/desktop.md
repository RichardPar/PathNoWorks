# The PathNoWorks desktop

`pathnoworks` puts the command-line tools behind a window. You can see the
DECnet nodes, browse a node's files and copy them both ways, log in to a
node, and send mail.

```sh
pathnoworks                      # the network
pathnoworks --files VAXXY        # straight to VAXXY's files
pathnoworks --socket PATH        # another decnetd, for this run
```

It needs Qt 6 (`qt6-base-dev` to build it), and xterm for terminals. It
is built with the rest when CMake finds Qt 6, as `build/gui/pathnoworks`.
`cmake --install` also puts it in the desktop menu.

## The network

The main window lists the nodes decnetd knows, with each node's state,
hops and cost. *Reachable only* hides the rest. HECnet has well over a
thousand nodes, so type part of a name or address to find one.

An endnode knows only that its router is reachable. On an endnode,
PathNoWorks asks the router for the list instead, as
`TELL router SHOW KNOWN NODES` would, and says so in the status bar.
Names the router lacks come from this node's own list.

Double-click a node for its files. **Terminal** logs in to it, and
**Mail** writes to someone on it. Right-click a node for the same
actions. *PathNoWorks → Settings* sets decnetd's API socket; by default
it is `$DECNETAPI`, then `/tmp/decnetapi.sock`.

## Files

A node's files open in a window of their own:

- **Browse:** double-click a directory to open it. **Up** goes back,
  including above where you started on VMS: `[]` to `[-]`, `[A.B]` to
  `[A]`. Type a directory in the path box and press Enter:
  `DUA0:[USER]`, `SYS$LOGIN:`, `[.SUB]`, or `pub/` on a Unix FAL.
- **Look at a file:** double-click it. Text shows in a viewer; binary
  files say so.
- **Copy here** copies the selected files to a local folder. **Copy
  there** copies local files to the directory shown. So does dropping
  files on the window from the file manager. Text and binary are told
  apart as `pnw-copy` does.
- **Rename** (F2) and **Delete** (Del) act on the selected files.
- **Mount** mounts the directory shown at `~/DECnet/NODE` with `pnw-fs`,
  read and write, and opens it in the file manager. **Unmount** undoes
  that.
- **Terminal** logs in to the node.

The window starts in the login directory. It finds out whether the node
uses VMS names (`[DIR]FILE.TXT;1`) or Unix ones (`dir/file.txt`) by
looking.

A node that refuses a connection without a login asks for one, then tries
again. Leave the user empty for the node's default DECnet account. Tick
the proxy box to ask for proxy access as your own login name. The login
lasts as long as the window.

Everything runs in the background, one operation at a time per window,
and the status bar says how it went.

## Terminals

**Terminal** opens xterm as a VT340 running `pnw-sethost`. DEC graphics
work, including Sixel and ReGIS, and `SET TERMINAL/INQUIRE` sees a VT340.
See [pnw-sethost](pnw-sethost.md); `Ctrl-]` `q` leaves.

## Mail

**Mail** writes a message to `NODE::USER`, several separated by commas,
or `NODE"user password"::USER` for a node that wants a login (see
[pnw-mail](pnw-mail.md)). Each recipient is reported as sent or not.
Mail is sent from your login name. The window closes when everything has
gone, and stays open to say what failed if something has not.

To receive DECnet mail, run `pnw-mail listen`.

## Tested

`ctest` drives the windows offscreen against two decnetd nodes, one
running dnfal. It browses into a subdirectory and back, copies a file
here, copies a text and a binary file there, renames, deletes, views a
file, and tries a missing directory. The same test can run against a
real VMS node, so it has also been run against OpenVMS VAX 6.2:

```sh
QT_QPA_PLATFORM=offscreen PNW_TEST_SOCKET=/tmp/decnetapi.sock \
    PNW_TEST_VMS='VAXXY"user password"' build/tests/test_gui vms_node
```

## Limits

- **Mount puts the login on pnw-fs's command line,** where `ps` shows it
  to other local users for as long as it is mounted. Use proxy access, or
  a node's default account, on shared machines.
- **No dragging files out of the window yet.** Use Copy here.
- Copy here and Delete take files only, not whole directories.
- No LAT terminals or MOP from the desktop yet. Use `pnw-lat` and
  `pnw-mop`.
