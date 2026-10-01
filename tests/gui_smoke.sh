#!/bin/sh
# Integration test: the desktop's windows (test_gui), offscreen, against
# two decnetd nodes, one with dnfal.
#
#   gui_smoke.sh TEST_GUI DECNETD DNFAL TOOLDIR

set -u
TEST=$1
DECNETD=$2
DNFAL=$3
TOOLS=$4

dir=$(mktemp -d /tmp/pnw.XXXXXX) || exit 1
port=$(( 20000 + ($$ + 53) % 20000 ))
pids=
cleanup () {
    [ -n "$pids" ] && kill $pids 2>/dev/null
    wait 2>/dev/null
    rm -rf "$dir"
}
trap cleanup EXIT

root=$dir/root
mkdir -p "$root/sub"
printf 'line one\nline two\nthird line\n' > "$root/hello.txt"
echo inner > "$root/sub/inner.txt"

cat > "$dir/a.conf" <<EOC
routing 1.1 --type l1router
node 1.1 NODEA
node 1.2 NODEB
circuit mul-0 Multinet 127.0.0.1:$port:listen --t3 2
object --number 17 --name FAL --file $DNFAL --argument $root --argument rw
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

# Until the circuit is up and A's FAL answers.
i=0
until [ -S "$dir/b.sock" ] && "$TOOLS/pnw-dir" -s "$dir/b.sock" 'NODEA::hello.txt' >/dev/null 2>&1; do
    i=$((i + 1))
    if [ $i -gt 60 ]; then echo "FAIL  FAL did not answer"; cat "$dir/a.log"; exit 1; fi
    sleep 0.5
done

QT_QPA_PLATFORM=offscreen PNW_TEST_SOCKET=$dir/b.sock PNW_TEST_NODE=NODEA \
    PNW_TEST_ROOT=$root "$TEST"
