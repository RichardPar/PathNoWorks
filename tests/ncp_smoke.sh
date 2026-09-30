#!/bin/sh
# Integration test: two decnetd routers joined by Multinet, pnw-ncp talking
# to one of them through its API socket.
#
#   ncp_smoke.sh PNW_NCP DECNETD

set -u
NCP=$1
DECNETD=$2

dir=$(mktemp -d /tmp/pnw.XXXXXX) || exit 1
port=$(( 20000 + $$ % 20000 ))
pids=
cleanup () {
    [ -n "$pids" ] && kill $pids 2>/dev/null
    wait 2>/dev/null
    rm -rf "$dir"
}
trap cleanup EXIT

cat > "$dir/a.conf" <<EOF
routing 1.1 --type l1router
node 1.1 NODEA
node 1.2 NODEB
circuit mul-0 Multinet 127.0.0.1:$port:listen --t3 2
EOF
cat > "$dir/b.conf" <<EOF
routing 1.2 --type l1router
node 1.1 NODEA
node 1.2 NODEB
circuit mul-0 Multinet 127.0.0.1:$port:connect --t3 2
api $dir/b.sock
EOF

"$DECNETD" "$dir/a.conf" > "$dir/a.log" 2>&1 & pids="$pids $!"
"$DECNETD" "$dir/b.conf" > "$dir/b.log" 2>&1 & pids="$pids $!"

fails=0
check () {                  # check NAME PATTERN -- command...
    name=$1; pattern=$2; shift 3
    out=$("$@" 2>&1)
    if printf '%s\n' "$out" | grep -q -- "$pattern"; then
        echo "ok    $name"
    else
        echo "FAIL  $name: expected /$pattern/ in:"
        printf '%s\n' "$out" | sed 's/^/        /'
        fails=$((fails + 1))
    fi
}

# Wait for the API socket and for routing to reach the other node.
i=0
until [ -S "$dir/b.sock" ] && \
      "$NCP" -s "$dir/b.sock" loop node NODEA >/dev/null 2>&1; do
    i=$((i + 1))
    if [ $i -gt 60 ]; then
        echo "FAIL  nodes did not come up"; cat "$dir/b.log"; exit 1
    fi
    sleep 0.5
done

N="$NCP -s $dir/b.sock"
check "tell show executor"      "Node = 1.1 (NODEA)" -- $N tell nodea show executor
check "tell show exec char"     "Characteristics"             -- $N tell NODEA show exec char
check "tell show known nodes"   "1.2 (NODEB)"                 -- $N tell NODEA show known nodes
check "tell show known circuits" "MUL-0"                      -- $N tell NODEA show known circuits
check "tell show node by name"  "NODEB"                       -- $N tell NODEA show node NODEB
check "unknown node component"  "Unrecognized component"      -- $N tell NODEA show circuit NOSUCH
check "local show executor"     "Node = 1.2 (NODEB)"          -- $N show executor
check "local show known nodes"  "1.1 (NODEA)"                 -- $N show known nodes
check "local loop node"         "looped"                      -- $N loop node NODEB
check "loop node"               "looped"                      -- $N loop node nodea count 3 length 100
check "unknown target"          "Unrecognized node name"      -- $N tell NOWHERE show executor
check "bad syntax"              "unrecognized command"        -- $N frobnicate
check "interactive"             "Known Node"                  -- sh -c "printf 'tell nodea show known nodes\nexit\n' | $N"

if [ $fails -ne 0 ]; then
    echo "$fails failed"
    exit 1
fi
echo "all passed"
