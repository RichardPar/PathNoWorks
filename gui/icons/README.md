# Icons

`tango/` holds the icons the PathNoWorks desktop asks for by their
freedesktop names, taken from the [Tango icon theme](http://tango.freedesktop.org)
0.8.90, which is in the public domain.  They are compiled into
`pathnoworks` and used on Windows, which has no icon theme of its own, and
on Linux for any icon the desktop's theme lacks.

A few names the GUI uses are not in Tango and are a Tango icon under that
name instead:

| Name                          | Tango icon                          |
|-------------------------------|-------------------------------------|
| `configure`                   | `preferences-system`                |
| `edit-rename`                 | `accessories-text-editor`           |
| `mail-client`                 | `internet-mail`                     |
| `preferences-desktop-display` | `preferences-desktop-remote-desktop` |
| `preferences-system-time`     | `appointment-new`                   |
| `starred`                     | `emblem-favorite`                   |

A new `QIcon::fromTheme` name in the GUI wants its icon added here too, at
16x16, 22x22, 32x32 and scalable.
