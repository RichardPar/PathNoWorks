#!/bin/sh
# Integration test: dnfal's access control, through the file tools.
#
#   fal_auth.sh TOOLDIR DECNETD DNFAL

set -u
TOOLS=$1
DECNETD=$2
DNFAL=$3

dir=$(mktemp -d /tmp/pnw.XXXXXX) || exit 1
port=$(( 20000 + ($$ + 13) % 20000 ))
pids=
cleanup () {
    [ -n "$pids" ] && kill $pids 2>/dev/null
    wait 2>/dev/null
    rm -rf "$dir"
}
trap cleanup EXIT

root=$dir/root
mkdir -p "$root/home" "$root/pub"
echo mine > "$root/home/private.txt"
echo shared > "$root/pub/readme.txt"
echo local > "$dir/up.txt"

# RICHARD may write his own directory; GUEST may only read pub.  No "*":
# connections without a user are refused.  Whoever runs the test, asking by
# proxy from NODEB, is let in as RICHARD.
me=$(id -un | tr '[:lower:]' '[:upper:]')
{
    echo "# test users"
    echo "richard $(echo secret | "$DNFAL" --hash) home rw"
    echo "guest   -   pub  ro"
    echo "proxy   NODEB::$me   richard"
} > "$dir/fal.users"

cat > "$dir/a.conf" <<EOC
routing 1.1 --type l1router
node 1.1 NODEA
node 1.2 NODEB
circuit mul-0 Multinet 127.0.0.1:$port:listen --t3 2
object --number 17 --name FAL --file $DNFAL --argument $root --argument users=$dir/fal.users
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

S="-s $dir/b.sock"
i=0
until [ -S "$dir/b.sock" ] && \
      "$TOOLS/pnw-dir" $S 'NODEA"guest"::*' >/dev/null 2>&1; do
    i=$((i + 1))
    if [ $i -gt 60 ]; then
        echo "FAIL  FAL did not answer"; cat "$dir/a.log"; exit 1
    fi
    sleep 0.5
done

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
T=$TOOLS
check "anonymous refused"        "Access control rejected" -- $T/pnw-dir $S 'NODEA::*'
check "proxy maps the local user" "private.txt"            -- $T/pnw-dir $S --proxy 'NODEA::*'
check "proxy login is logged"    "by proxy as RICHARD"     -- cat "$dir/a.log"
check "wrong password refused"   "Access control rejected" -- $T/pnw-dir $S 'NODEA"richard wrong"::*'
check "unknown user refused"     "Access control rejected" -- $T/pnw-dir $S 'NODEA"mallory x"::*'
check "user sees own directory"  "private.txt"             -- $T/pnw-dir $S 'NODEA"richard secret"::*'
check "VMS upper case password"  "mine"                    -- $T/pnw-type $S 'NODEA"RICHARD SECRET"::PRIVATE.TXT'
check "user may write"           "up.txt"                  -- $T/pnw-copy $S "$dir/up.txt" 'NODEA"richard secret"::'
check "guest sees only pub"      "readme.txt"              -- $T/pnw-dir $S 'NODEA"guest"::*'
check "guest cannot reach home"  "error in file name"      -- $T/pnw-type $S 'NODEA"guest"::../home/private.txt'
check "guest cannot write"       "privilege violation"     -- $T/pnw-copy $S "$dir/up.txt" 'NODEA"guest"::'
check "refusals are logged"      "access control rejected for user mallory" -- cat "$dir/a.log"

[ -f "$root/home/up.txt" ] || { echo "FAIL  upload did not land"; fails=$((fails + 1)); }
[ -f "$root/pub/up.txt" ] && { echo "FAIL  guest upload landed"; fails=$((fails + 1)); }

if [ $fails -ne 0 ]; then echo "$fails failed"; exit 1; fi
echo "all passed"
