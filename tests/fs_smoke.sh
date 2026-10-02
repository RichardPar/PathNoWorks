#!/bin/sh
# Integration test: pnw-fs mounting a directory served by dnfal, used with
# ordinary tools.
#
#   fs_smoke.sh PNW_FS DECNETD DNFAL

set -u
PNWFS=$1
DECNETD=$2
DNFAL=$3

dir=$(mktemp -d /tmp/pnw.XXXXXX) || exit 1
# Git Bash on Windows: the native programs need a Windows path, in the
# config files as well as on the command line.
case $(uname -s) in MINGW*|MSYS*) dir=$(cd "$dir" && pwd -W) ;; esac
port=$(( 20000 + ($$ + 19) % 20000 ))
pids=
mnt=$dir/mnt
cleanup () {
    fusermount3 -u "$mnt" 2>/dev/null
    [ -n "$pids" ] && kill $pids 2>/dev/null
    wait 2>/dev/null
    rm -rf "$dir"
}
trap cleanup EXIT

root=$dir/root
mkdir -p "$root/sub"
# WinFsp mounts on a directory that does not exist yet, and pnw-fs stays in
# the foreground there; libfuse wants the directory, and pnw-fs returns
# once it is mounted.
case $(uname -s) in MINGW*|MSYS*) winfsp=yes ;; *) winfsp=no; mkdir -p "$mnt" ;; esac
printf 'line one\nline two\n' > "$root/hello.txt"
head -c 20000 /dev/urandom > "$root/random.bin"
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

i=0
if [ $winfsp = yes ]; then
    until [ -e "$dir/b.sock" ]; do
        i=$((i + 1)); [ $i -gt 60 ] && { echo "FAIL  no API socket"; exit 1; }
        sleep 0.5
    done
    "$PNWFS" -s "$dir/b.sock" --rw 'NODEA::' "$mnt" 2>"$dir/mount.err" &
    pids="$pids $!"
    until [ -e "$mnt/hello.txt" ]; do
        i=$((i + 1))
        if [ $i -gt 60 ]; then
            echo "FAIL  could not mount"; cat "$dir/mount.err"; exit 1
        fi
        sleep 0.5
    done
else
until [ -e "$dir/b.sock" ] && \
      "$PNWFS" -s "$dir/b.sock" --rw 'NODEA::' "$mnt" 2>"$dir/mount.err"; do
    i=$((i + 1))
    if [ $i -gt 60 ]; then
        echo "FAIL  could not mount"; cat "$dir/mount.err"; exit 1
    fi
    sleep 0.5
done
fi

fails=0
ok ()   { echo "ok    $1"; }
fail () { echo "FAIL  $1"; fails=$((fails + 1)); }

[ "$(ls "$mnt" | tr '\n' ' ')" = "hello.txt random.bin sub " ] \
    && ok "ls" || { fail "ls"; ls -la "$mnt" | sed 's/^/        /'; }
[ -d "$mnt/sub" ] && ok "directories are directories" || fail "directories are directories"
cmp -s "$mnt/hello.txt" "$root/hello.txt" && ok "cat" || fail "cat"
cmp -s "$mnt/random.bin" "$root/random.bin" && ok "binary read" || fail "binary read"
cmp -s "$mnt/sub/inner.txt" "$root/sub/inner.txt" && ok "read in a subdirectory" \
    || fail "read in a subdirectory"
[ "$(stat -c %s "$mnt/random.bin")" = 20000 ] && ok "size" || fail "size"

printf 'new file\n' > "$mnt/new.txt"
[ "$(cat "$root/new.txt" 2>/dev/null)" = "new file" ] && ok "create" || fail "create"

cp "$root/random.bin" "$mnt/sub/copy.bin"
cmp -s "$root/random.bin" "$root/sub/copy.bin" && ok "cp into the mount" \
    || fail "cp into the mount"

echo "appended" >> "$mnt/hello.txt"
[ "$(tail -1 "$root/hello.txt")" = "appended" ] && ok "append" || fail "append"

mv "$mnt/new.txt" "$mnt/moved.txt"
[ -f "$root/moved.txt" ] && [ ! -e "$root/new.txt" ] && ok "mv" || fail "mv"

rm "$mnt/moved.txt"
[ ! -e "$root/moved.txt" ] && ok "rm" || fail "rm"

mkdir "$mnt/nodir" 2>/dev/null && fail "mkdir should be refused" \
    || ok "mkdir is refused"

[ ! -e "$mnt/nosuch" ] && ok "missing files are missing" \
    || fail "missing files are missing"

if [ $fails -ne 0 ]; then echo "$fails failed"; exit 1; fi
echo "all passed"
