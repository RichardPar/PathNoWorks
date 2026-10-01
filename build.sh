#!/usr/bin/env bash
#
# build.sh -- check what PathNoWorks needs, offer to fetch what's missing,
# then build it.
#
#     ./build.sh [options]
#
#   -y, --yes           answer yes to every question (install, clone)
#   -n, --no            answer no: only check and build with what's there
#   --required-only     don't offer the optional packages
#   --test              run the tests after building
#   --debug             cppdecnet's sanitizer build (-DCPPDECNET_FLAVOUR=debug)
#   --build-dir DIR     where to build; default ./build
#   --cppdecnet DIR     cppdecnet's checkout; default ../Decnet/cppdecnet
#   --branch NAME       cppdecnet's branch to clone; default PathNoWorksAPI
#   --pydecnet DIR      PyDECnet's checkout; default ../Decnet/pydecnet
#   -j N                parallel jobs; default all CPUs
#   -h, --help          this
#
# System packages come from apt (Debian, Ubuntu, Mint), dnf (Fedora) or
# pacman (Arch), with sudo.  Nothing is installed or cloned without asking
# first, unless you said --yes.

set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
answer=ask
optional=yes
run_tests=no
flavour=release
build_dir=$here/build
cppdecnet=$here/../Decnet/cppdecnet
pydecnet=$here/../Decnet/pydecnet
jobs=$(nproc 2>/dev/null || echo 4)

CPPDECNET_URL=https://github.com/RichardPar/cppdecnet.git
# The API PathNoWorks uses is on this branch until it reaches main.
cppdecnet_branch=PathNoWorksAPI
PYDECNET_URL=https://github.com/pkoning2/pydecnet.git

while [ $# -gt 0 ]; do
    case $1 in
        -y|--yes)        answer=yes ;;
        -n|--no)         answer=no ;;
        --required-only) optional=no ;;
        --test)          run_tests=yes ;;
        --debug)         flavour=debug ;;
        --build-dir)     build_dir=$2; shift ;;
        --cppdecnet)     cppdecnet=$2; shift ;;
        --branch)        cppdecnet_branch=$2; shift ;;
        --pydecnet)      pydecnet=$2; shift ;;
        -j)              jobs=$2; shift ;;
        -j*)             jobs=${1#-j} ;;
        -h|--help)       sed -n '3,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)               echo "build.sh: unknown option $1 (try --help)" >&2; exit 2 ;;
    esac
    shift
done
cppdecnet=$(realpath -m "$cppdecnet")
pydecnet=$(realpath -m "$pydecnet")
build_dir=$(realpath -m "$build_dir")

# ------------------------------------------------------------------ output

if [ -t 1 ]; then
    bold=$'\e[1m' green=$'\e[32m' red=$'\e[31m' yellow=$'\e[33m' plain=$'\e[0m'
else
    bold= green= red= yellow= plain=
fi
say ()  { printf '%s\n' "$*"; }
section () { printf '\n%s%s%s\n' "$bold" "$*" "$plain"; }
die ()  { printf '%sbuild.sh: %s%s\n' "$red" "$*" "$plain" >&2; exit 1; }

# ask QUESTION DEFAULT(y|n) -- true for yes.  With nobody at the keyboard
# and no --yes, the answer is no.
ask ()
{
    local reply hint
    case $answer in
        yes) say "$1 yes (--yes)"; return 0 ;;
        no)  say "$1 no (--no)"; return 1 ;;
    esac
    if [ ! -t 0 ]; then
        say "$1 no (not a terminal; use --yes)"
        return 1
    fi
    [ "$2" = y ] && hint="[Y/n]" || hint="[y/N]"
    read -r -p "$1 $hint " reply || reply=
    reply=${reply:-$2}
    case $reply in [Yy]*) return 0 ;; *) return 1 ;; esac
}

# ------------------------------------------------------------ the packages

if command -v apt-get >/dev/null; then
    pm=apt
elif command -v dnf >/dev/null; then
    pm=dnf
elif command -v pacman >/dev/null; then
    pm=pacman
else
    pm=
fi

# package NAME -- the distribution's package(s) for one of our checks.
package ()
{
    case $pm:$1 in
        apt:compiler)    echo build-essential ;;
        dnf:compiler)    echo gcc-c++ make ;;
        pacman:compiler) echo base-devel ;;
        apt:pkgconfig)   echo pkg-config ;;
        dnf:pkgconfig)   echo pkgconf-pkg-config ;;
        pacman:pkgconfig) echo pkgconf ;;
        apt:crypt)       echo libcrypt-dev ;;
        dnf:crypt)       echo libxcrypt-devel ;;
        pacman:crypt)    echo libxcrypt ;;
        apt:pcap)        echo libpcap-dev ;;
        dnf:pcap)        echo libpcap-devel ;;
        pacman:pcap)     echo libpcap ;;
        apt:fuse)        echo libfuse3-dev fuse3 ;;
        dnf:fuse)        echo fuse3-devel fuse3 ;;
        pacman:fuse)     echo fuse3 ;;
        apt:qt)          echo qt6-base-dev ;;
        dnf:qt)          echo qt6-qtbase-devel ;;
        pacman:qt)       echo qt6-base ;;
        apt:setcap)      echo libcap2-bin ;;
        dnf:setcap|pacman:setcap) echo libcap ;;
        pacman:python)   echo python ;;
        *:make)          echo make ;;
        *:cmake)         echo cmake ;;
        *:git)           echo git ;;
        *:xterm)         echo xterm ;;
        *:python)        echo python3 ;;
    esac
}

# compiles CODE [LIBS] -- does the C++ compiler take this?
compiles ()
{
    local cxx=${CXX:-c++}
    command -v "$cxx" >/dev/null || return 1
    printf '%s\n' "$1" | "$cxx" -std=c++20 -x c++ - -o /dev/null ${2:-} >/dev/null 2>&1
}

version_at_least ()     # have want
{
    [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -1)" = "$2" ]
}

# Each check: name, required or optional, what it's for, and a test.
check_compiler ()  { compiles $'#include <format>\n#include <span>\nint main(){return (int)std::format("{}",1).size()-1;}'; }
check_make ()      { command -v make >/dev/null; }
check_cmake ()
{
    command -v cmake >/dev/null || return 1
    version_at_least "$(cmake --version | awk 'NR==1 {print $3}')" 3.20
}
check_pkgconfig () { command -v pkg-config >/dev/null; }
check_git ()       { command -v git >/dev/null; }
check_crypt ()     { compiles $'#include <crypt.h>\nint main(){return crypt("a","ab")==0;}' -lcrypt; }
check_pcap ()      { compiles $'#include <pcap.h>\nint main(){return 0;}' -lpcap; }
check_fuse ()      { pkg-config --exists fuse3 2>/dev/null; }
check_qt ()        { pkg-config --exists Qt6Widgets Qt6Concurrent Qt6Test 2>/dev/null; }
check_xterm ()     { command -v xterm >/dev/null; }
check_setcap ()    { command -v setcap >/dev/null || [ -x /usr/sbin/setcap ] || [ -x /sbin/setcap ]; }
check_python ()    { command -v python3 >/dev/null; }

checks=(
    "compiler  required  a C++20 compiler (GCC 13+ or Clang 16+)"
    "make      required  make, for cppdecnet"
    "cmake     required  CMake 3.20 or later"
    "pkgconfig required  pkg-config"
    "git       required  git, to fetch cppdecnet"
    "crypt     required  libcrypt, for dnfal's password hashes"
    "pcap      optional  libpcap: Ethernet circuits on a real LAN, and MOP"
    "fuse      optional  FUSE 3: pnw-fs, and Mount in the desktop"
    "qt        optional  Qt 6: the pathnoworks desktop"
    "xterm     optional  xterm: terminal windows from the desktop"
    "setcap    optional  setcap: lets pnw-lat send LAT frames"
    "python    optional  Python 3: some of the tests"
)

missing_required=()
missing_optional=()

run_checks ()
{
    local line name kind what
    missing_required=()
    missing_optional=()
    for line in "${checks[@]}"; do
        read -r name kind what <<<"$line"
        if "check_$name"; then
            printf '  %s✓%s %s\n' "$green" "$plain" "$what"
        elif [ "$kind" = required ]; then
            printf '  %s✗%s %s %s(required)%s\n' "$red" "$plain" "$what" "$red" "$plain"
            missing_required+=("$name")
        else
            printf '  %s-%s %s %s(optional, not found)%s\n' "$yellow" "$plain" "$what" "$yellow" "$plain"
            missing_optional+=("$name")
        fi
    done
}

packages_for ()
{
    local name out=()
    for name in "$@"; do
        # shellcheck disable=SC2207
        out+=($(package "$name"))
    done
    printf '%s\n' "${out[@]}" | awk '!seen[$0]++' | paste -sd ' '
}

install_packages ()
{
    local sudo=
    [ "$(id -u)" -eq 0 ] || sudo=sudo
    case $pm in
        apt)    $sudo apt-get update && $sudo apt-get install -y "$@" ;;
        dnf)    $sudo dnf install -y "$@" ;;
        pacman) $sudo pacman -S --needed --noconfirm "$@" ;;
    esac
}

section "Checking what PathNoWorks needs"
run_checks
pcap_was_missing=no
case " ${missing_optional[*]} " in *" pcap "*) pcap_was_missing=yes ;; esac

want=()
if [ ${#missing_required[@]} -gt 0 ]; then
    want+=("${missing_required[@]}")
fi
if [ ${#missing_optional[@]} -gt 0 ] && [ "$optional" = yes ]; then
    if [ -n "$pm" ]; then
        say ""
        if ask "Install the optional ones too ($(packages_for "${missing_optional[@]}"))?" y; then
            want+=("${missing_optional[@]}")
        fi
    fi
fi

if [ ${#want[@]} -gt 0 ]; then
    if [ -z "$pm" ]; then
        die "can't find apt, dnf or pacman to install the missing pieces; install them yourself and run me again"
    fi
    pkgs=$(packages_for "${want[@]}")
    say ""
    if ask "Install with $pm: ${pkgs}?" y; then
        # shellcheck disable=SC2086
        install_packages $pkgs || die "$pm couldn't install them"
        section "Checking again"
        run_checks
    fi
fi

if [ ${#missing_required[@]} -gt 0 ]; then
    die "still missing: ${missing_required[*]} ($(packages_for "${missing_required[@]}"))"
fi

# --------------------------------------------------------------- the code

section "Finding cppdecnet and PyDECnet"
if [ -f "$cppdecnet/include/decnet/node.h" ]; then
    printf '  %s✓%s cppdecnet at %s\n' "$green" "$plain" "$cppdecnet"
else
    say "  cppdecnet isn't at $cppdecnet. It's the DECnet node PathNoWorks runs on."
    if ask "Clone it from $CPPDECNET_URL ($cppdecnet_branch)?" y; then
        mkdir -p "$(dirname "$cppdecnet")"
        git clone -b "$cppdecnet_branch" "$CPPDECNET_URL" "$cppdecnet"
    else
        die "PathNoWorks can't build without cppdecnet (--cppdecnet DIR if it's elsewhere)"
    fi
fi

# Its build doesn't notice new compiler flags, so if libpcap arrived just
# now, build cppdecnet afresh to get the Ethernet circuits.
if [ "$pcap_was_missing" = yes ] && check_pcap && [ -d "$cppdecnet/build/$flavour" ]; then
    say "  libpcap is new: cleaning cppdecnet's $flavour build so it's rebuilt with it"
    make -C "$cppdecnet" BUILD="$flavour" clean >/dev/null
fi

pydecnet_tree=$pydecnet/pydecnet
if [ -f "$pydecnet_tree/decnet/applications/fal.py" ]; then
    printf '  %s✓%s PyDECnet at %s\n' "$green" "$plain" "$pydecnet"
elif [ "$optional" = yes ]; then
    say "  PyDECnet isn't at $pydecnet. It's only for the tests against its FAL."
    if ask "Clone it from $PYDECNET_URL?" n; then
        mkdir -p "$(dirname "$pydecnet")"
        git clone "$PYDECNET_URL" "$pydecnet"
    fi
fi

# -------------------------------------------------------------- the build

section "Building"
cmake_args=(-S "$here" -B "$build_dir"
            -DCPPDECNET_DIR="$cppdecnet"
            -DCPPDECNET_FLAVOUR="$flavour")
if [ -f "$pydecnet_tree/decnet/applications/fal.py" ]; then
    cmake_args+=(-DPYDECNET_DIR="$pydecnet_tree")
fi
cmake "${cmake_args[@]}"
cmake --build "$build_dir" -j "$jobs"

if [ "$run_tests" = yes ]; then
    section "Testing"
    ctest --test-dir "$build_dir" --output-on-failure
fi

section "Done"
say "  tools:    $build_dir/tools/"
[ -x "$build_dir/gui/pathnoworks" ] && say "  desktop:  $build_dir/gui/pathnoworks"
say "  decnetd:  $cppdecnet/build/$flavour/bin/"
say ""
say "  Install with:  sudo cmake --install \"$build_dir\""
say "                 sudo make -C \"$cppdecnet\" install"
say "  Then see samples/decnetd.conf and docs/getting-started.md."
