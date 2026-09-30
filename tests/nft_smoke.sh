#!/bin/sh
# Integration test: pnw-dir, pnw-type and pnw-copy against PyDECnet's FAL
# (decnet/applications/fal.py), which one decnetd runs as object 17.
#
#   nft_smoke.sh TOOLDIR DECNETD PYDECNET_DIR

set -u
TOOLS=$1
DECNETD=$2
PYDECNET=$3

dir=$(mktemp -d /tmp/pnw.XXXXXX) || exit 1
port=$(( 20000 + ($$ + 7) % 20000 ))
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
head -c 3000 /dev/urandom > "$root/random.bin"
: > "$root/empty.dat"
echo inner > "$root/sub/inner.txt"

cat > "$dir/a.conf" <<EOC
routing 1.1 --type l1router
node 1.1 NODEA
node 1.2 NODEB
circuit mul-0 Multinet 127.0.0.1:$port:listen --t3 2
object --number 17 --name FAL --file $PYDECNET/decnet/applications/fal.py --argument $root
EOC
cat > "$dir/b.conf" <<EOC
routing 1.2 --type l1router
node 1.1 NODEA
node 1.2 NODEB
circuit mul-0 Multinet 127.0.0.1:$port:connect --t3 2
api $dir/b.sock
EOC

PYTHONPATH=$PYDECNET "$DECNETD" "$dir/a.conf" > "$dir/a.log" 2>&1 & pids="$pids $!"
"$DECNETD" "$dir/b.conf" > "$dir/b.log" 2>&1 & pids="$pids $!"

S="-s $dir/b.sock"
i=0
until [ -S "$dir/b.sock" ] && "$TOOLS/pnw-dir" $S 'NODEA::hello.txt' >/dev/null 2>&1; do
    i=$((i + 1))
    if [ $i -gt 60 ]; then
        echo "FAIL  FAL did not answer"; cat "$dir/a.log"; exit 1
    fi
    sleep 0.5
done

fails=0
ok ()   { echo "ok    $1"; }
fail () { echo "FAIL  $1"; fails=$((fails + 1)); }

out=$("$TOOLS/pnw-dir" $S 'NODEA::*' 2>&1)
if echo "$out" | grep -q "hello.txt" && echo "$out" | grep -q "random.bin" \
   && echo "$out" | grep -q "Total of 4 files"; then ok "dir"
else fail "dir"; echo "$out" | sed 's/^/        /'; fi

out=$("$TOOLS/pnw-dir" $S 'NODEA::nosuch/*' 2>&1)
if echo "$out" | grep -q "directory not found"; then ok "dir of a missing directory"
else fail "dir of a missing directory"; echo "$out" | sed 's/^/        /'; fi

if "$TOOLS/pnw-type" $S 'NODEA::hello.txt' | cmp -s - "$root/hello.txt"; then
    ok "type"; else fail "type"; fi

if "$TOOLS/pnw-type" $S --text 'nodea::hello.txt' | cmp -s - "$root/hello.txt"; then
    ok "type as text records"; else fail "type as text records"; fi

if "$TOOLS/pnw-copy" $S 'NODEA::random.bin' "$dir/" 2>/dev/null \
   && cmp -s "$dir/random.bin" "$root/random.bin"; then
    ok "copy binary"; else fail "copy binary"; fi

if "$TOOLS/pnw-copy" $S 'NODEA::empty.dat' "$dir/e.dat" 2>/dev/null \
   && [ -f "$dir/e.dat" ] && [ ! -s "$dir/e.dat" ]; then
    ok "copy empty file"; else fail "copy empty file"; fi

if "$TOOLS/pnw-copy" $S 'NODEA::sub/inner.txt' "$dir/i.txt" 2>/dev/null \
   && cmp -s "$dir/i.txt" "$root/sub/inner.txt"; then
    ok "copy from a subdirectory"; else fail "copy from a subdirectory"; fi

out=$("$TOOLS/pnw-dir" $S 'NOWHERE::*' 2>&1)
if echo "$out" | grep -q "Unrecognized node name"; then ok "unknown node"
else fail "unknown node"; echo "$out" | sed 's/^/        /'; fi

if grep -q Traceback "$dir/a.log"; then
    fail "FAL raised an exception"; grep -A20 Traceback "$dir/a.log" | head -30
fi

if [ $fails -ne 0 ]; then echo "$fails failed"; exit 1; fi
echo "all passed"
