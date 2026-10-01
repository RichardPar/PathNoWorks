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
port=$(( 20000 + ($$ + 61) % 20000 ))
pids=
cleanup () {
    [ -n "$pids" ] && kill $pids 2>/dev/null
    wait 2>/dev/null
    rm -rf "$dir"
}
trap cleanup EXIT
fail () { echo "FAIL  $*"; for f in "$dir"/*.log; do echo "== $f"; cat "$f"; done; exit 1; }

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
python3 "$FAKE" server "$dir/x.sock" & pids="$pids $!"

i=0
until [ -S "$dir/a.sock" ] && [ -S "$dir/b.sock" ] && [ -S "$dir/x.sock" ]; do
    i=$((i + 1)); [ $i -gt 50 ] && fail "nothing started"
    sleep 0.2
done

# A serves display 0 to NODEB only, and display 1 to nobody it will meet.
XAUTHORITY=$dir/xauth "$X11" --socket "$dir/a.sock" serve --allow NODEB \
    --display "$dir/x.sock" > "$dir/serve0.log" 2>&1 & pids="$pids $!"
XAUTHORITY=$dir/xauth "$X11" --socket "$dir/a.sock" serve -n 1 --allow NODEZ \
    --display "$dir/x.sock" > "$dir/serve1.log" 2>&1 & pids="$pids $!"
# B makes two local displays, one for each.
"$X11" --socket "$dir/b.sock" connect NODEA::0 --listen "$dir/d0.sock" > "$dir/conn0.log" 2>&1 & pids="$pids $!"
"$X11" --socket "$dir/b.sock" connect NODEA::1 --listen "$dir/d1.sock" > "$dir/conn1.log" 2>&1 & pids="$pids $!"

# Until the circuit is up and the bridges are listening.
i=0
until grep -q XSERVING "$dir/serve0.log" 2>/dev/null && grep -q XSERVING "$dir/serve1.log" 2>/dev/null \
      && [ -S "$dir/d0.sock" ] && [ -S "$dir/d1.sock" ] \
      && python3 "$FAKE" client "$dir/d0.sock" $cookie > "$dir/first.out" 2>&1; do
    i=$((i + 1)); [ $i -gt 60 ] && { cat "$dir/first.out"; fail "no X connection over DECnet"; }
    sleep 0.5
done
cat "$dir/first.out"

python3 "$FAKE" refused "$dir/d1.sock" || fail "refusal"
grep -q "XREFUSED, NODEB::.*not in --allow" "$dir/serve1.log" || fail "refusal not logged"

echo "x11_smoke: all passed"
