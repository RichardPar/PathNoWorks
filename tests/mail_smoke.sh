#!/bin/sh
# Integration test: pnw-mail sending to pnw-mail listening, between two
# decnetd nodes.
#
#   mail_smoke.sh PNW-MAIL DECNETD

set -u
MAIL=$1
DECNETD=$2

dir=$(mktemp -d /tmp/pnw.XXXXXX) || exit 1
port=$(( 20000 + ($$ + 29) % 20000 ))
pids=
cleanup () {
    [ -n "$pids" ] && kill $pids 2>/dev/null
    wait 2>/dev/null
    rm -rf "$dir"
}
trap cleanup EXIT

fail () { echo "FAIL: $*"; exit 1; }

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

i=0
until [ -S "$dir/a.sock" ] && [ -S "$dir/b.sock" ]; do
    i=$((i + 1)); [ $i -gt 50 ] && fail "decnetd did not start"
    sleep 0.2
done

"$MAIL" --socket "$dir/a.sock" listen -o "$dir/mbox" > "$dir/listen.log" 2>&1 &
pids="$pids $!"

# Until the circuit is up and the listener bound, sending fails.
i=0
until printf 'Hello from B.\n\nFrom here on, a trap.\nLast line\n' | \
      "$MAIL" --socket "$dir/b.sock" send -s "Test mail" NODEA::ALICE,NODEA::BOB \
      > "$dir/send.log" 2>&1; do
    i=$((i + 1)); [ $i -gt 60 ] && { cat "$dir/send.log"; fail "send"; }
    sleep 1
done

grep -q "SENT, to NODEA::ALICE" "$dir/send.log" || fail "no status for ALICE"
grep -q "SENT, to NODEA::BOB" "$dir/send.log" || fail "no status for BOB"

m=$dir/mbox
[ -f "$m" ] || fail "no mbox"
[ "$(grep -c '^From ' "$m")" = 1 ] || { cat "$m"; fail "not one message"; }
grep -q '^From: "NODEB::' "$m" || { cat "$m"; fail "sender"; }
grep -q '^To: NODEA::ALICE,NODEA::BOB$' "$m" || { cat "$m"; fail "To line"; }
grep -q '^Subject: Test mail$' "$m" || { cat "$m"; fail "subject"; }
grep -q '^Cc:' "$m" && { cat "$m"; fail "a CC line from nowhere"; }
# The body, after the header's blank line, with its own blank line kept.
body=$(sed '1,/^$/d' "$m")
expect=$(printf 'Hello from B.\n\n>From here on, a trap.\nLast line')
[ "$body" = "$expect" ] || { cat "$m"; fail "body"; }

echo "mail_smoke: all passed"
