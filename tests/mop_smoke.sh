#!/bin/sh
# Integration test: pnw-mop against two decnetd stations on an Ethernet
# carried over UDP.
#
#   mop_smoke.sh PNW-MOP DECNETD

set -u
MOP=$1
DECNETD=$2

dir=$(mktemp -d /tmp/pnw.XXXXXX) || exit 1
pa=$(( 20000 + ($$ + 41) % 20000 ))
pb=$(( pa + 1 ))
pids=
cleanup () {
    [ -n "$pids" ] && kill $pids 2>/dev/null
    wait 2>/dev/null
    rm -rf "$dir"
}
trap cleanup EXIT

fail () { echo "FAIL: $*"; exit 1; }

cat > "$dir/a.conf" <<EOC
circuit eth-0 Ethernet udp:$pa:127.0.0.1:$pb --random-address --mop
api $dir/a.sock
EOC
cat > "$dir/b.conf" <<EOC
circuit eth-0 Ethernet udp:$pb:127.0.0.1:$pa --random-address --mop
api $dir/b.sock
EOC

"$DECNETD" "$dir/a.conf" > "$dir/a.log" 2>&1 & pids="$pids $!"
"$DECNETD" "$dir/b.conf" > "$dir/b.log" 2>&1 & pids="$pids $!"

i=0
until [ -S "$dir/a.sock" ] && [ -S "$dir/b.sock" ]; do
    i=$((i + 1)); [ $i -gt 50 ] && fail "decnetd did not start"
    sleep 0.2
done

M="$MOP --socket $dir/a.sock"

# B's address, from B itself.
b=$($MOP --socket "$dir/b.sock" circuits | sed -n 's/.*hardware \([0-9A-F-]*\).*/\1/p')
[ -n "$b" ] || fail "no circuit on B"

$M list > "$dir/out" || fail "list"
grep -q "no system IDs heard" "$dir/out" || { cat "$dir/out"; fail "heard something already"; }

$M id "$b" > "$dir/out" || { cat "$dir/out"; fail "id"; }
grep -q "Station $b" "$dir/out" || { cat "$dir/out"; fail "id: station"; }
grep -q "Software *DECnet/C++" "$dir/out" || { cat "$dir/out"; fail "id: software"; }
grep -q "Services *loop, counters" "$dir/out" || { cat "$dir/out"; fail "id: services"; }

# Asked once, now remembered.
$M list > "$dir/out" || fail "list"
grep -q "^$b .*DECnet/C++" "$dir/out" || { cat "$dir/out"; fail "list after id"; }

$M counters "$b" > "$dir/out" || { cat "$dir/out"; fail "counters"; }
grep -q "Counters from $b" "$dir/out" || { cat "$dir/out"; fail "counters: from"; }
grep -q "Data blocks received" "$dir/out" || { cat "$dir/out"; fail "counters: rows"; }

$M loop "$b" -n 3 -f > "$dir/out" || { cat "$dir/out"; fail "loop"; }
grep -q "3 of 3 answered" "$dir/out" || { cat "$dir/out"; fail "loop: count"; }

# Whoever answers the loopback multicast: B.
$M loop > "$dir/out" || { cat "$dir/out"; fail "loop to anyone"; }
grep -q "reply from $b" "$dir/out" || { cat "$dir/out"; fail "loop to anyone: who"; }

# Nobody there: a failure, said so.
$M loop AA-00-04-00-99-99 -t 1 > "$dir/out" && { cat "$dir/out"; fail "loop to nobody succeeded"; }
grep -q "0 of 1 answered" "$dir/out" || { cat "$dir/out"; fail "loop to nobody: report"; }
$M id AA-00-04-00-99-99 -t 1 2> "$dir/err" && fail "id of nobody succeeded"
grep -q "did not answer" "$dir/err" || { cat "$dir/err"; fail "id of nobody: report"; }

echo "mop_smoke: all passed"
