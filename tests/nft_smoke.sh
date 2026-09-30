#!/bin/sh
# Integration test: the file tools against a FAL that one decnetd runs as
# object 17.  Either PyDECnet's (decnet/applications/fal.py), which only
# lists and reads, or cppdecnet's dnfal, which also writes.
#
#   nft_smoke.sh TOOLDIR DECNETD pydecnet PYDECNET_DIR
#   nft_smoke.sh TOOLDIR DECNETD dnfal DNFAL

set -u
TOOLS=$1
DECNETD=$2
KIND=$3
FAL=$4

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
EOC
if [ "$KIND" = pydecnet ]; then
    echo "object --number 17 --name FAL --file $FAL/decnet/applications/fal.py --argument $root" >> "$dir/a.conf"
    export PYTHONPATH=$FAL
else
    echo "object --number 17 --name FAL --file $FAL --argument $root --argument rw" >> "$dir/a.conf"
fi
cat > "$dir/b.conf" <<EOC
routing 1.2 --type l1router
node 1.1 NODEA
node 1.2 NODEB
circuit mul-0 Multinet 127.0.0.1:$port:connect --t3 2
api $dir/b.sock
EOC

"$DECNETD" "$dir/a.conf" > "$dir/a.log" 2>&1 & pids="$pids $!"
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

mkdir "$dir/many"
if "$TOOLS/pnw-copy" $S 'NODEA::*.txt' "$dir/many" 2>/dev/null \
   && cmp -s "$dir/many/hello.txt" "$root/hello.txt" \
   && [ "$(ls "$dir/many")" = "hello.txt" ]; then
    ok "wildcard copy into a directory"
else fail "wildcard copy into a directory"; ls "$dir/many" | sed 's/^/        /'; fi

out=$("$TOOLS/pnw-copy" $S 'NODEA::*.nomatch' "$dir/many" 2>&1)
if echo "$out" | grep -q "no files matched"; then ok "wildcard that matches nothing"
else fail "wildcard that matches nothing"; echo "$out" | sed 's/^/        /'; fi

out=$("$TOOLS/pnw-dir" $S 'NOWHERE::*' 2>&1)
if echo "$out" | grep -q "Unrecognized node name"; then ok "unknown node"
else fail "unknown node"; echo "$out" | sed 's/^/        /'; fi

if [ "$KIND" = dnfal ]; then
    head -c 5000 /dev/urandom > "$dir/up.bin"
    printf 'one\ntwo\r\nthree' > "$dir/up.txt"

    if "$TOOLS/pnw-copy" $S "$dir/up.bin" 'NODEA::[SUB]' 2>/dev/null \
       && cmp -s "$dir/up.bin" "$root/sub/up.bin"; then
        ok "upload binary into a VMS style directory"
    else fail "upload binary into a VMS style directory"; fi

    if "$TOOLS/pnw-copy" $S "$dir/up.txt" 'NODEA::' 2>/dev/null \
       && [ "$(cat "$root/up.txt")" = "$(printf 'one\ntwo\nthree')" ]; then
        ok "upload text"
    else fail "upload text"; fi

    if "$TOOLS/pnw-copy" $S 'NODEA::sub/up.bin' "$dir/back.bin" 2>/dev/null \
       && cmp -s "$dir/up.bin" "$dir/back.bin"; then ok "round trip"
    else fail "round trip"; fi

    cp "$dir/up.txt" "$dir/second.txt"
    if "$TOOLS/pnw-copy" $S "$dir/up.bin" "$dir/second.txt" 'NODEA::[SUB]' 2>/dev/null \
       && [ -f "$root/sub/second.txt" ] && cmp -s "$dir/up.bin" "$root/sub/up.bin"; then
        ok "several files in one copy"
    else fail "several files in one copy"; fi

    if "$TOOLS/pnw-rename" $S 'NODEA::up.txt' 'moved.txt' \
       && [ -f "$root/moved.txt" ] && [ ! -e "$root/up.txt" ]; then ok "rename"
    else fail "rename"; fi

    if "$TOOLS/pnw-delete" $S 'NODEA::[SUB]*.BIN' && [ ! -e "$root/sub/up.bin" ] \
       && [ -f "$root/sub/inner.txt" ]; then ok "delete by wildcard"
    else fail "delete by wildcard"; fi

    out=$("$TOOLS/pnw-type" $S 'NODEA::../etc/passwd' 2>&1)
    if echo "$out" | grep -q "error in file name"; then ok "cannot leave the root"
    else fail "cannot leave the root"; echo "$out" | sed 's/^/        /'; fi

    out=$("$TOOLS/pnw-type" $S 'NODEA::nosuch.txt' 2>&1)
    if echo "$out" | grep -q "file not found"; then ok "type a missing file"
    else fail "type a missing file"; echo "$out" | sed 's/^/        /'; fi
fi

if grep -q Traceback "$dir/a.log"; then
    fail "FAL raised an exception"; grep -A20 Traceback "$dir/a.log" | head -30
fi

if [ $fails -ne 0 ]; then echo "$fails failed"; exit 1; fi
echo "all passed"
