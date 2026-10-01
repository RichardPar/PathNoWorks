#!/bin/sh
# Integration test: pnw-x11 serve and connect, between two decnetd nodes,
# with a stand-in X server (x11_fake.py) so no display is needed.
#
#   x11_smoke.sh PNW-X11 DECNETD

set -u
X11=$1
DECNETD=$2
FAKE=$(dirname "$0")/x11_fake.py

dir=$(mktemp -d /tmp/pnw.XXXXXX) || exit 1
# Git Bash on Windows: the native programs need a Windows path, in the
# config files as well as on the command line.
case $(uname -s) in MINGW*|MSYS*) dir=$(cd "$dir" && pwd -W) ;; esac
port=$(( 20000 + ($$ + 61) % 20000 ))
pids=
cleanup () {
    [ -n "$pids" ] && kill $pids 2>/dev/null
    wait 2>/dev/null
    rm -rf "$dir"
}
trap cleanup EXIT
fail () { echo "FAIL  $*"; for f in "$dir"/*.log; do echo "== $f"; cat "$f"; done; exit 1; }

# Where the X server and the two bridged displays are.  On Windows X goes
# over TCP, display n on port 6000 + n; elsewhere, Unix sockets.
case $(uname -s) in
MINGW*|MSYS*)
    xd=$(( 100 + $$ % 200 ))
    xs=tcp:$((6000 + xd));       xdisplay=:$xd
    d0=tcp:$((6000 + xd + 1));   d0opt="-d $((xd + 1))"
    d1=tcp:$((6000 + xd + 2));   d1opt="-d $((xd + 2))" ;;
*)
    xs=$dir/x.sock;   xdisplay=$xs
    d0=$dir/d0.sock;  d0opt="--listen $d0"
    d1=$dir/d1.sock;  d1opt="--listen $d1" ;;
esac
# Is the X server up?  A display a bridge makes: has it said so?
up ()       { case $1 in tcp:*) python3 "$FAKE" probe "$1" ;; *) [ -e "$1" ] ;; esac; }
made ()     { case $1 in tcp:*) grep -q XLISTENING "$2" 2>/dev/null ;; *) [ -e "$1" ] ;; esac; }

cat > "$dir/a.conf" <<EOC
routing 1.1 --type l1router
node 1.1 NODEA
node 1.2 NODEB
circuit mul-0 Multinet 127.0.0.1:$port:listen --t3 2
api $dir/a.sock
EOC
cat > "$dir/b.conf" <<EOC
routing 1.2 --type l1router
node 1.1 NODEA
node 1.2 NODEB
circuit mul-0 Multinet 127.0.0.1:$port:connect --t3 2
api $dir/b.sock
EOC
"$DECNETD" "$dir/a.conf" > "$dir/a.log" 2>&1 & pids="$pids $!"
"$DECNETD" "$dir/b.conf" > "$dir/b.log" 2>&1 & pids="$pids $!"

cookie=00112233445566778899aabbccddeeff
python3 "$FAKE" xauth "$dir/xauth" $cookie
python3 "$FAKE" server "$xs" & pids="$pids $!"

i=0
until [ -e "$dir/a.sock" ] && [ -e "$dir/b.sock" ] && up "$xs"; do
    i=$((i + 1)); [ $i -gt 50 ] && fail "nothing started"
    sleep 0.2
done

# A serves display 0 to NODEB only, and display 1 to nobody it will meet.
XAUTHORITY=$dir/xauth "$X11" --socket "$dir/a.sock" serve --allow NODEB \
    --display "$xdisplay" > "$dir/serve0.log" 2>&1 & pids="$pids $!"
XAUTHORITY=$dir/xauth "$X11" --socket "$dir/a.sock" serve -n 1 --allow NODEZ \
    --display "$xdisplay" > "$dir/serve1.log" 2>&1 & pids="$pids $!"
# B makes two local displays, one for each.
"$X11" --socket "$dir/b.sock" connect NODEA::0 $d0opt > "$dir/conn0.log" 2>&1 & pids="$pids $!"
"$X11" --socket "$dir/b.sock" connect NODEA::1 $d1opt > "$dir/conn1.log" 2>&1 & pids="$pids $!"

# Until the circuit is up and the bridges are listening.
i=0
until grep -q XSERVING "$dir/serve0.log" 2>/dev/null && grep -q XSERVING "$dir/serve1.log" 2>/dev/null \
      && made "$d0" "$dir/conn0.log" && made "$d1" "$dir/conn1.log" \
      && python3 "$FAKE" client "$d0" $cookie > "$dir/first.out" 2>&1; do
    i=$((i + 1)); [ $i -gt 60 ] && { cat "$dir/first.out"; fail "no X connection over DECnet"; }
    sleep 0.5
done
cat "$dir/first.out"

python3 "$FAKE" refused "$d1" || fail "refusal"
grep -q "XREFUSED, NODEB::.*not in --allow" "$dir/serve1.log" || fail "refusal not logged"

echo "x11_smoke: all passed"
