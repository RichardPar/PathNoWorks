#!/usr/bin/env bash
#
# build-deb.sh -- a .deb of PathNoWorks for Debian, Ubuntu, Mint and the
# like, with cppdecnet's decnetd, dnfal and dnping in it, as the Windows
# installer has.
#
#     packaging/debian/build-deb.sh [OUTDIR]
#
# Builds both for /usr (cppdecnet from ../Decnet/cppdecnet, or
# $CPPDECNET_DIR), works out the library packages it depends on with
# dpkg-shlibdeps, and writes pathnoworks_<version>_<arch>.deb to OUTDIR,
# by default ./Linux, as Windows/ has the Windows installer.  Needs what
# building needs, plus dpkg-dev.
#
# The package installs the programs, the desktop's menu entry and the
# docs.  It doesn't make this machine a DECnet node by itself: run
# decnetd-setup afterwards, which asks for the node's name, address and
# peer, and sets decnetd up as a service.

set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
src=$(cd "$here/../.." && pwd)
cppdecnet=$(realpath -m "${CPPDECNET_DIR:-$src/../Decnet/cppdecnet}")
out=$(realpath -m "${1:-$src/Linux}")

for t in dpkg-deb dpkg-shlibdeps cmake make; do
    command -v "$t" >/dev/null || { echo "build-deb.sh: needs $t (apt install dpkg-dev cmake make)" >&2; exit 1; }
done
[ -f "$cppdecnet/include/decnet/node.h" ] \
    || { echo "build-deb.sh: no cppdecnet at $cppdecnet (set CPPDECNET_DIR)" >&2; exit 1; }

version=$(sed -n 's/^project (PathNoWorks VERSION \([0-9.]*\).*/\1/p' "$src/CMakeLists.txt")
arch=$(dpkg --print-architecture)
maintainer="$(git -C "$src" config user.name 2>/dev/null || echo PathNoWorks) <$(git -C "$src" config user.email 2>/dev/null || echo root@localhost)>"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
root=$work/root

echo "== Building PathNoWorks $version and cppdecnet for /usr"
cmake -S "$src" -B "$work/build" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr -DCPPDECNET_DIR="$cppdecnet" -DPNW_TESTS=OFF >/dev/null
cmake --build "$work/build" -j "$(nproc)"
DESTDIR=$root cmake --install "$work/build" >/dev/null

# cppdecnet's programs, and its node setup as decnetd-setup.  Not its
# library and headers, which only builders want.
install -m 755 "$cppdecnet"/build/release/bin/{decnetd,dnfal,dnping} "$root/usr/bin/"
install -m 755 "$cppdecnet/tools/install-decnetd.sh" "$root/usr/bin/decnetd-setup"

# Docs and examples.
doc=$root/usr/share/doc/pathnoworks
install -d "$doc/examples" "$doc/cppdecnet"
install -m 644 "$src/README.md" "$doc/"
cp -r "$src/docs" "$doc/"
install -m 644 "$src/samples/decnetd.conf" "$src/samples/fal.users" "$doc/examples/"
install -m 644 "$cppdecnet/README.md" "$doc/cppdecnet/"
{
    echo "Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/"
    echo "Upstream-Name: PathNoWorks"
    echo "Source: https://github.com/RichardPar/PathNoWorks"
    echo
    echo "Files: *"
    echo "Copyright: 2026 RichardPar"
    echo "License: none chosen yet"
    echo
    echo "Files: usr/bin/decnetd usr/bin/dnfal usr/bin/dnping usr/bin/decnetd-setup"
    echo "Copyright: 2026 RichardPar; PyDECnet, from which it was ported, Paul Koning"
    echo "License: BSD-3-Clause"
    echo
    sed 's/^/ /; s/^ $/ ./' "$cppdecnet/LICENSE"
} > "$doc/copyright"

# Debug information makes decnetd 50 MB; nobody installing a package wants it.
find "$root/usr/bin" -type f -exec sh -c 'file -b "$1" | grep -q ELF' _ {} \; \
    -exec strip --strip-unneeded --remove-section=.comment --remove-section=.note {} \;

# The libraries the programs use, as package dependencies.
mkdir -p "$work/debian"
printf 'Source: pathnoworks\nMaintainer: %s\n\nPackage: pathnoworks\nArchitecture: any\n' \
    "$maintainer" > "$work/debian/control"
elves=$(find "$root/usr/bin" -type f -exec sh -c 'file -b "$1" | grep -q ELF' _ {} \; -print)
# shellcheck disable=SC2086
depends=$(cd "$work" && dpkg-shlibdeps -O $elves 2>/dev/null | sed -n 's/^shlibs:Depends=//p')

install -d "$root/DEBIAN"
cat > "$root/DEBIAN/control" <<EOF
Package: pathnoworks
Version: $version
Architecture: $arch
Maintainer: $maintainer
Installed-Size: $(du -sk --exclude=DEBIAN "$root" | cut -f1)
Depends: $depends
Recommends: xterm, fuse3, libcap2-bin
Section: net
Priority: optional
Homepage: https://github.com/RichardPar/PathNoWorks
Description: DECnet Phase IV desktop and tools, Pathworks style
 PathNoWorks talks DECnet Phase IV to VMS, RSX, PyDECnet and the rest of
 HECnet from an ordinary Linux machine: network management, file access,
 a network drive, remote login, LAT terminals, mail, MOP and X11 over
 DECnet, with a Qt desktop on top.
 .
 It includes cppdecnet's decnetd, the DECnet node the tools work through,
 and dnfal, its file server.  Run decnetd-setup to make this machine a
 node: it asks for the node's name, address and peer, and runs decnetd
 as a systemd service.
EOF

cat > "$root/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = configure ]; then
    # pnw-lat reaches the LAN through pnw-latsock, which needs raw sockets.
    if command -v setcap >/dev/null 2>&1; then
        setcap cap_net_raw+ep /usr/bin/pnw-latsock 2>/dev/null \
            || echo "pathnoworks: couldn't give pnw-latsock CAP_NET_RAW; pnw-lat won't reach the LAN" >&2
    fi
    if [ ! -e /etc/decnet/decnetd.conf ]; then
        echo "PathNoWorks is installed.  To make this machine a DECnet node, run"
        echo "decnetd-setup as yourself (not as root)."
    else
        # A node already set up: carry on with the new decnetd.
        if [ -d /run/systemd/system ] && systemctl is-active --quiet decnetd.service; then
            systemctl restart decnetd.service || true
        fi
    fi
fi
exit 0
EOF

cat > "$root/DEBIAN/prerm" <<'EOF'
#!/bin/sh
set -e
# Removing the package takes decnetd away: stop the service decnetd-setup
# made, rather than leave systemd restarting a program that's gone.  Its
# configuration stays in /etc/decnet.
if [ "$1" = remove ] && [ -d /run/systemd/system ] \
   && [ -e /etc/systemd/system/decnetd.service ]; then
    systemctl disable --now decnetd.service || true
fi
exit 0
EOF
chmod 755 "$root/DEBIAN/postinst" "$root/DEBIAN/prerm"

mkdir -p "$out"
deb=$out/pathnoworks_${version}_${arch}.deb
dpkg-deb --root-owner-group -Zxz --build "$root" "$deb" >/dev/null
echo "== $deb"
dpkg-deb --info "$deb" | sed -n '/Package:/,/Description:/p'
echo "Install with:  sudo apt install $deb"
echo "Then:          decnetd-setup"
