#!/usr/bin/env python3
"""decw-font-aliases.py -- DECwindows font names for a Linux X server.

VMS DECwindows programs ask the X server for DEC's fonts by name:
-DEC-Terminal-Medium-R-Normal--14-140-75-75-C-80-ISO8859-1 for DECterm,
-Bigelow & Holmes-Menu-... for menus, and so on.  A Linux X server has
none of them, so the programs fall back to whatever they get, or do
without.  This makes a font directory whose fonts.alias gives every one of
those names to the nearest font the server does have: the same kind of
face, weight and slant, at the same pixel size (scaling an outline font to
it where there is one).

    decw-font-aliases.py FONTS [DIR] [--install]

FONTS is a directory of DECwindows .DECW$FONT files (from a VMS system's
SYS$COMMON:[SYSFONT.DECW...] or the DECwindows savesets on the VMS CD); only
the names inside them are read.  DIR is where to write fonts.dir and
fonts.alias, by default ~/.local/share/fonts/decwindows.  --install adds
DIR to the X server's font path for this session (xset +fp); the
PathNoWorks desktop does that itself when it starts pnw-x11.

On Windows, for VcXsrv: the server's fonts are read from its own font
directories rather than asked of it (VcXsrv has no xlsfonts or xset), DIR
defaults to %LOCALAPPDATA%\\PathNoWorks\\fonts\\decwindows, and the
PathNoWorks desktop adds it to the font path when it starts VcXsrv.

    py decw-font-aliases.py FONTS
"""

import glob
import os
import re
import shutil
import subprocess
import sys

# DEC's families, and what to stand in for them, in order of preference.
# Monospaced ones say so: they want a fixed-width face.
FAMILIES = [
    (r"terminal|vt330|fixed", True,  ["fixed", "nimbus mono l", "courier"]),
    (r"lucidatypewriter",     True,  ["courier", "nimbus mono l", "fixed"]),
    (r"courier",              True,  ["courier", "nimbus mono l", "fixed"]),
    (r"menu|helvetica|lucida$|interim|present", False,
                                     ["helvetica", "nimbus sans l"]),
    (r"lucidabright|times|dutch", False, ["times", "nimbus roman no9 l"]),
    (r"new century schoolbook", False, ["new century schoolbook", "newcenturyschlbk",
                                        "century schoolbook l", "times"]),
    (r"avant garde",          False, ["itc avant garde gothic", "avant garde gothic",
                                      "avantgarde", "urw gothic l", "helvetica"]),
    (r"bookman",              False, ["itc bookman", "bookman", "urw bookman l", "times"]),
    (r"zapf chancery",        False, ["itc zapf chancery", "zapf chancery",
                                      "urw chancery l", "times"]),
    (r"zapf dingbats",        False, ["itc zapf dingbats", "dingbats"]),
    (r"symbol",               False, ["symbol", "standard symbols l"]),
    (r"open look",            False, ["open look glyph", "open look cursor"]),
]

BOLD = {"bold", "demi", "demibold", "black", "heavy"}


def xlfd (name):
    """The fields of an XLFD name, or nothing if it is not one."""
    f = name.split ("-")
    return f[1:] if len (f) == 15 and f[0] == "" else None


def decw_names (top):
    """Every font name in the .DECW$FONT files under top."""
    names = set ()
    for p in glob.glob (os.path.join (top, "**", "*"), recursive=True):
        if not p.upper ().endswith ("DECW$FONT"):
            continue
        with open (p, "rb") as f:
            d = f.read (512)
        end = d.find (b"\0", 0x60)
        name = d[0x60:end].decode ("latin-1", "replace")
        # A name with a quote in it could not be written to fonts.alias.
        if xlfd (name) and '"' not in name:
            names.add (name)
    return sorted (names)


def vcxsrv_font_dirs ():
    """VcXsrv's own font directories, on Windows; else nothing."""
    if os.name != "nt":
        return []
    top = None
    try:
        import winreg
        with winreg.OpenKey (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\VcXsrv") as k:
            top = winreg.QueryValueEx (k, "Install_Dir_64")[0]
    except OSError:
        top = os.path.join (os.environ.get ("ProgramFiles", r"C:\Program Files"), "VcXsrv")
    # Its default font path, in its order.
    dirs = [os.path.join (top, "fonts", d)
            for d in ("misc", "TTF", "OTF", "Type1", "100dpi", "75dpi")]
    return [d for d in dirs if os.path.isdir (d)]


def server_fonts ():
    """The X server's fonts, as XLFD field lists, lower case."""
    if shutil.which ("xlsfonts"):
        out = subprocess.run (["xlsfonts"], capture_output=True, text=True,
                              check=True).stdout
        lines = out.splitlines ()
    else:
        # No xlsfonts (VcXsrv): what its font directories say they hold.
        # fonts.dir and fonts.scale are a count, then "file name" lines.
        lines = []
        for d in vcxsrv_font_dirs ():
            for index in ("fonts.dir", "fonts.scale"):
                p = os.path.join (d, index)
                if os.path.exists (p):
                    with open (p, encoding="latin-1") as f:
                        lines += [l.split (" ", 1)[1] for l in f.read ().splitlines ()[1:]
                                  if " " in l]
        if not lines:
            sys.exit ("cannot list the X server's fonts: no xlsfonts, and no VcXsrv")
    fonts = []
    for line in lines:
        f = xlfd (line.strip ().lower ())
        if f:
            fonts.append (f)
    return fonts


def stand_in (dec, fonts):
    """The nearest server font to a DEC one, as a name to alias to."""
    f = [x.lower () for x in dec]
    family, weight, slant = f[1], f[2], f[3]
    pixel = int (f[6]) if f[6].isdigit () else 0
    charset = (f[12], f[13])
    mono = f[10] in ("c", "m")
    choices = None
    for pattern, is_mono, cands in FAMILIES:
        if re.search (pattern, family):
            choices, mono = cands, is_mono or mono
            break
    if choices is None:
        choices = ["fixed", "courier"] if mono else ["helvetica", "times"]

    want_bold = weight in BOLD
    want_slanted = slant in ("i", "o")
    best, best_score = None, None
    for rank, fam in enumerate (choices):
        for s in fonts:
            if s[1] != fam:
                continue
            # The same character set.  DEC's own sets (DECtech, DECmath,
            # DEC-FontSpecific) have no match on a Linux server; a Latin-1
            # stand-in would show the wrong symbols -- an O-umlaut on the
            # calculator's square root key -- so they get none, and the
            # program does what it does without them.
            if (s[12], s[13]) != charset:
                continue
            score = 0
            score += rank * 100
            score += 0 if s[4] == f[4] else 20     # normal, narrow, wide...
            score += 0 if (s[2] in BOLD) == want_bold else 90
            score += 0 if (s[3] in ("i", "o")) == want_slanted else 30
            scalable = s[6] == "0"
            if scalable and s[1] == "fixed":
                score += 150                # a bitmap blown up: blocky; an outline is better
            elif scalable:
                score += 2                  # a bitmap of the right size is better
            elif s[6].isdigit ():
                # Size matters: a 28 pixel terminal font is better scaled
                # from an outline than taken as an 18 pixel bitmap.
                score += 12 * abs (int (s[6]) - pixel)
            else:
                continue
            if best_score is None or score < best_score:
                best, best_score = s, score
    if best is None:
        return None
    t = list (best)
    if t[6] == "0" and pixel:
        # Scale the outline to the pixel size asked for.
        t[6], t[7], t[8], t[9], t[11] = str (pixel), "0", dec[8], dec[9], "0"
    return "-" + "-".join (t)


def main ():
    args = [a for a in sys.argv[1:] if not a.startswith ("--")]
    install = "--install" in sys.argv
    if not args:
        print (__doc__.strip (), file=sys.stderr)
        return 2
    if os.name == "nt":
        default = os.path.join (os.environ.get ("LOCALAPPDATA", os.path.expanduser ("~")),
                                "PathNoWorks", "fonts", "decwindows")
    else:
        default = os.path.expanduser ("~/.local/share/fonts/decwindows")
    out = args[1] if len (args) > 1 else default

    names = decw_names (args[0])
    if not names:
        print ("no DECwindows fonts found under %s" % args[0], file=sys.stderr)
        return 1
    # What the server has without this directory's aliases: run again, it
    # would otherwise find every DEC name there already.  (Without xset --
    # VcXsrv -- the fonts come from its own directories, not ours.)
    have_xset = shutil.which ("xset") is not None
    fp = subprocess.run (["xset", "q"], capture_output=True, text=True).stdout if have_xset else ""
    ours = out.rstrip ("/") + "/"
    was_on = have_xset and (ours in fp or out.rstrip ("/") + "," in fp)
    if was_on:
        subprocess.run (["xset", "-fp", ours], check=False)
    try:
        fonts = server_fonts ()
    finally:
        if was_on and not install:
            subprocess.run (["xset", "+fp", ours], check=False)
    have = {"-" + "-".join (f) for f in fonts}
    lines, missing = [], []
    for n in names:
        # The server already has some (the Misc-Fixed ones, say); an alias
        # of a font to itself would make it refuse the whole directory.
        if n.lower () in have:
            continue
        t = stand_in (xlfd (n), fonts)
        if t:
            lines.append ('"%s" "%s"' % (n, t))
        else:
            missing.append (n)

    os.makedirs (out, exist_ok=True)
    with open (os.path.join (out, "fonts.dir"), "w") as f:
        f.write ("0\n")
    with open (os.path.join (out, "fonts.alias"), "w") as f:
        f.write ("! DECwindows font names, given to this X server's nearest fonts.\n")
        f.write ("! Made by PathNoWorks decw-font-aliases.py; safe to remake.\n")
        f.write ("\n".join (lines) + "\n")
    print ("%d DECwindows font names: %d given stand-ins, %d the server has already; in %s"
           % (len (names), len (lines), len (names) - len (lines) - len (missing),
              os.path.join (out, "fonts.alias")))
    if missing:
        print ("%d use DEC's own character sets (DECtech, DECmath, ...), which have "
               "no stand-in here" % len (missing))

    if (install or was_on) and not have_xset:
        print ("no xset: the PathNoWorks desktop adds it to VcXsrv's font path "
               "when it starts VcXsrv")
    elif install or was_on:
        fp = subprocess.run (["xset", "q"], capture_output=True, text=True).stdout
        if out.rstrip ("/") not in fp:
            subprocess.run (["xset", "+fp", ours], check=True)
        subprocess.run (["xset", "fp", "rehash"], check=True)
        print ("added to the X server's font path")
    return 0


if __name__ == "__main__":
    sys.exit (main ())
