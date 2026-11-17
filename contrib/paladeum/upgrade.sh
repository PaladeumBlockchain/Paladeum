#!/usr/bin/env bash
# Install or upgrade an Paladeum Core node on Linux (x86_64, glibc 2.34 or later:
# Ubuntu 22.04, Debian 12 and newer).
#
#   curl -fsSL <base url>/upgrade.sh | bash
#
# What it does:
#   1. downloads the release archive and checks its SHA256 against the value in this script
#   2. stops the running node (systemd unit "paladeumd" if there is one, else paladeum-cli stop)
#   3. keeps a copy of the old programs and of the wallet file
#   4. installs the new programs where the old ones are (default /usr/local/bin)
#   5. starts the node the way it was running and waits until it answers
#
# The data directory is used as it is: no reindex, no new download of the chain. An
# existing paladeum.conf is never changed; one is written only when there is none.
# The wallet is not encrypted, decrypted or otherwise changed.
#
# Settings (environment variables, all optional):
#   PLB_BASE      where the archive is downloaded from (default: where this script is published)
#   PLB_DATADIR   data directory (default: the -datadir of the running node or of the systemd unit,
#                 else ~/.paladeum). Needed when several nodes run on the machine.
#   PLB_BINDIR    where to install (default: the directory of the running or configured paladeumd,
#                 else of the paladeumd in PATH, else /usr/local/bin)
#   PLB_START=0   do not start the node after installing
#   PLB_REPAIR=0  do not run "repaircoins" after an upgrade from 3.0.x
#   PLB_SYSTEMD=1 on a machine without an paladeumd unit, install one (starts the node at boot)
#   PLB_SNAPSHOT  for a new installation: address (http://...) of the chain state snapshot and
#                 block archives. It is written into the new paladeum.conf as bootstrapurl, and
#                 the node starts from the snapshot instead of validating every block.
#   PLB_KEEP      number of backups kept under <datadir>/upgrade-backup (default 3)

set -euo pipefail

VERSION="@VERSION@"
ARCHIVE="paladeum-${VERSION}-linux-x86_64.tar.gz"
ARCHIVE_SHA256="@SHA256@"
DEFAULT_BASE="@BASE@"

main() {
    local base="${PLB_BASE:-$DEFAULT_BASE}"
    local start="${PLB_START:-1}"

    say()  { printf '\n== %s\n' "$*"; }
    fail() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

    # ---- the node as it runs now: its program and its options (a running process first,
    # then the systemd unit), so that a node with its own -datadir or program directory is
    # upgraded where it is
    local unit=""
    if command -v systemctl >/dev/null && systemctl cat paladeumd.service >/dev/null 2>&1; then
        unit="paladeumd.service"
    fi
    # the data directory of a running process (its -datadir, else the default)
    process_datadir() {
        local dir
        dir="$(tr '\0' '\n' < "/proc/$1/cmdline" 2>/dev/null | sed -n 's/^--\{0,1\}datadir=//p' | tail -n 1)"
        printf '%s\n' "${dir:-$HOME/.paladeum}"
    }
    local running_pid="" running_exe="" running_args="" pid
    local all_pids
    all_pids="$(pgrep -x paladeumd || true)"
    if [ -n "${PLB_DATADIR:-}" ]; then
        # the node of that data directory
        for pid in $all_pids; do
            if [ "$(readlink -f "$(process_datadir "$pid")")" = "$(readlink -f "$PLB_DATADIR")" ]; then
                running_pid="$pid"
            fi
        done
    elif [ "$(printf '%s\n' "$all_pids" | grep -c .)" -gt 1 ]; then
        fail "several nodes run on this machine; set PLB_DATADIR to the data directory of the one to upgrade"
    else
        running_pid="$all_pids"
    fi
    if [ -n "$running_pid" ]; then
        if [ "$(stat -c %u "/proc/$running_pid" 2>/dev/null)" != "$(id -u)" ]; then
            fail "the node runs as the user $(stat -c %U "/proc/$running_pid" 2>/dev/null); run this script as that user"
        fi
        running_exe="$(readlink "/proc/$running_pid/exe" 2>/dev/null | sed 's/ (deleted)$//' || true)"
        running_args="$(tr '\0' '\n' < "/proc/$running_pid/cmdline" 2>/dev/null || true)"
    elif [ -n "$unit" ]; then
        local unit_user
        unit_user="$(systemctl show -p User --value "$unit" 2>/dev/null || true)"
        if [ -n "$unit_user" ] && [ "$unit_user" != "$(id -un)" ]; then
            fail "the unit $unit runs the node as the user $unit_user; run this script as that user"
        fi
        local exec_start
        exec_start="$(systemctl show -p ExecStart --value "$unit" 2>/dev/null || true)"
        running_exe="$(printf '%s\n' "$exec_start" | sed -n 's/.*path=\([^ ;]*\).*/\1/p' | head -n 1)"
        running_args="$(printf '%s\n' "$exec_start" | sed -n 's/.*argv\[\]=\([^;]*\).*/\1/p' | head -n 1 | tr ' ' '\n')"
    fi
    local option_datadir
    option_datadir="$(printf '%s\n' "$running_args" | sed -n 's/^--\{0,1\}datadir=//p' | tail -n 1)"

    local datadir="${PLB_DATADIR:-${option_datadir:-$HOME/.paladeum}}"
    local conf="$datadir/paladeum.conf"
    # the systemd unit counts only when it runs this node (the data directory of its command)
    if [ -n "$unit" ]; then
        local unit_datadir
        unit_datadir="$(systemctl show -p ExecStart --value "$unit" 2>/dev/null | sed -n 's/.*argv\[\]=\([^;]*\).*/\1/p' | tr ' ' '\n' | sed -n 's/^--\{0,1\}datadir=//p' | tail -n 1)"
        if [ "$(readlink -f "${unit_datadir:-$HOME/.paladeum}")" != "$(readlink -f "$datadir")" ]; then
            unit=""
        fi
    fi

    # ---- requirements
    [ "$(uname -s)" = "Linux" ] && [ "$(uname -m)" = "x86_64" ] || fail "this archive is for Linux x86_64"
    [ "$(id -u)" != "0" ] || fail "run this as the user that runs the node, not as root (sudo is asked for when needed)"
    local tool
    for tool in curl tar sha256sum pgrep install; do
        command -v "$tool" >/dev/null || fail "'$tool' is needed"
    done
    local glibc
    glibc="$(ldd --version 2>/dev/null | head -n 1 | grep -o '[0-9]\+\.[0-9]\+$' || true)"
    if [ -n "$glibc" ] && [ "$(printf '%s\n2.34\n' "$glibc" | sort -V | head -n 1)" != "2.34" ]; then
        fail "glibc $glibc is too old; 2.34 or later is needed (Ubuntu 22.04, Debian 12 or newer)"
    fi

    # ---- where the programs go: where the running (or configured) program is
    local bindir="${PLB_BINDIR:-}"
    if [ -z "$bindir" ] && [ -n "$running_exe" ] && [ -x "$running_exe" ]; then
        bindir="$(dirname "$running_exe")"
    fi
    if [ -z "$bindir" ]; then
        if command -v paladeumd >/dev/null; then
            bindir="$(dirname "$(readlink -f "$(command -v paladeumd)")")"
        else
            bindir="/usr/local/bin"
        fi
    fi
    local as_root=""
    if ! { [ -d "$bindir" ] && [ -w "$bindir" ]; }; then
        as_root="sudo"
    fi
    local old_version="none"
    if [ -x "$bindir/paladeumd" ]; then
        old_version="$("$bindir/paladeumd" --version 2>/dev/null | head -n 1 | grep -o 'v[0-9][^ ]*' || echo unknown)"
    fi
    local cli=("$bindir/paladeum-cli" "-datadir=$datadir")

    say "Paladeum Core $VERSION"
    echo "programs:        $bindir (installed now: $old_version)"
    echo "data directory:  $datadir"

    # ---- download and check
    local work
    work="$(mktemp -d)"
    # (expanded now: "work" is gone when the trap runs)
    # shellcheck disable=SC2064
    trap "rm -rf -- '$work'" EXIT
    say "Downloading $ARCHIVE"
    curl -fL --retry 3 -o "$work/$ARCHIVE" "$base/$ARCHIVE"
    echo "$ARCHIVE_SHA256  $work/$ARCHIVE" | sha256sum -c - >/dev/null || fail "the SHA256 of the download is not $ARCHIVE_SHA256"
    echo "SHA256 ok"
    tar -xzf "$work/$ARCHIVE" -C "$work"
    local new="$work/paladeum-${VERSION}-linux-x86_64"
    local new_version
    new_version="$("$new/paladeumd" --version | head -n 1 | grep -o 'v[0-9][^ ]*')" || fail "the new paladeumd does not run on this machine"
    echo "new programs:    $new_version"

    # ---- configuration: an existing file is left alone
    mkdir -p "$datadir"
    if [ -f "$conf" ]; then
        echo "configuration:   $conf (kept as it is)"
    else
        ( umask 077
          printf 'server=1\nrpcuser=plbrpc\nrpcpassword=%s\nrpcbind=127.0.0.1\nrpcallowip=127.0.0.1\n' \
              "$(head -c 24 /dev/urandom | od -An -tx1 | tr -d ' \n')" > "$conf" )
        if [ -n "${PLB_SNAPSHOT:-}" ]; then
            printf 'bootstrapurl=%s\n' "$PLB_SNAPSHOT" >> "$conf"
        fi
        echo "configuration:   $conf (written, there was none)"
    fi

    # ---- how the node runs
    # (the process ids, not the name: while shutting down the program renames itself)
    local was_running=0 pids="$running_pid"
    if [ -n "$pids" ]; then
        was_running=1
    fi

    # ---- stop
    if [ "$was_running" = 1 ]; then
        say "Stopping the node"
        if [ -n "$unit" ] && systemctl is-active --quiet "$unit"; then
            sudo systemctl stop "$unit"
        else
            # (a node that is still starting answers no RPC call: SIGTERM stops it the same way)
            if ! "${cli[@]}" stop 2>/dev/null; then
                kill -TERM $pids 2>/dev/null || fail "the node does not answer 'paladeum-cli stop'; stop it and run this again"
            fi
        fi
        local waited=0 pid alive=1
        while [ "$alive" = 1 ]; do
            alive=0
            for pid in $pids; do
                if kill -0 "$pid" 2>/dev/null; then alive=1; fi
            done
            [ "$alive" = 0 ] && break
            [ "$waited" -lt 600 ] || fail "the node is still running after 10 minutes"
            sleep 2; waited=$((waited + 2))
        done
        echo "stopped"
    fi

    # ---- keep the old programs and the wallet
    local stamp keep
    stamp="$(date +%Y%m%d-%H%M%S)"
    keep="$datadir/upgrade-backup/$stamp"
    ( umask 077; mkdir -p "$keep" )
    local f
    for f in paladeumd paladeum-cli paladeum-tx paladeum-wallet; do
        if [ -f "$bindir/$f" ]; then cp -p "$bindir/$f" "$keep/"; fi
    done
    if [ -f "$datadir/wallet.dat" ]; then cp -p "$datadir/wallet.dat" "$keep/"; fi
    if [ -d "$datadir/wallets" ]; then cp -a "$datadir/wallets" "$keep/"; fi
    if [ -n "$(ls -A "$keep")" ]; then
        echo "backup:          $keep (old programs and wallet)"
        echo "                 The wallet copy is as (un)encrypted as the wallet. Move it somewhere safe"
        echo "                 or delete it when the new version runs well."
        # older backups: keep the newest few (names are times, so they sort by age)
        local old
        ls -1 "$datadir/upgrade-backup" | sort | head -n "-${PLB_KEEP:-3}" | while read -r old; do
            case "$old" in
                [0-9]*-[0-9]*) rm -rf -- "$datadir/upgrade-backup/$old" ;;
            esac
        done
    else
        rmdir "$keep"; keep=""
        echo "backup:          nothing to keep (new installation)"
    fi

    # ---- install
    say "Installing"
    $as_root mkdir -p "$bindir"
    $as_root install -m 755 "$new/paladeumd" "$new/paladeum-cli" "$new/paladeum-tx" "$new/paladeum-wallet" "$bindir/"
    echo "installed $("$bindir/paladeumd" --version | head -n 1)"

    if [ -z "$unit" ] && [ "${PLB_SYSTEMD:-0}" = 1 ] && command -v systemctl >/dev/null; then
        say "Installing the systemd unit paladeumd.service"
        sudo tee /etc/systemd/system/paladeumd.service >/dev/null <<UNIT
[Unit]
Description=Paladeum Core daemon
After=network-online.target
Wants=network-online.target

[Service]
Type=forking
User=$(id -un)
Group=$(id -gn)
PIDFile=$datadir/paladeumd.pid
ExecStart=$bindir/paladeumd -daemon -datadir=$datadir -pid=$datadir/paladeumd.pid
ExecStop=$bindir/paladeum-cli -datadir=$datadir stop
Restart=on-failure
RestartSec=30
TimeoutStartSec=300
TimeoutStopSec=600

[Install]
WantedBy=multi-user.target
UNIT
        sudo systemctl daemon-reload
        sudo systemctl enable paladeumd.service >/dev/null
        unit="paladeumd.service"
    fi

    # ---- start
    if [ "$start" != 1 ]; then
        say "Not started (PLB_START=0)"
        return 0
    fi
    say "Starting the node"
    if [ -n "$unit" ]; then
        sudo systemctl start "$unit"
    else
        # with the options of its command line as before (they may not be in paladeum.conf),
        # as a daemon
        local -a options=()
        local option
        if [ -n "$running_args" ]; then
            while IFS= read -r option; do
                [ -n "$option" ] && options+=("$option")
            done < <(printf '%s\n' "$running_args" | tail -n +2)
        fi
        if [ "${#options[@]}" -eq 0 ]; then
            options=("-datadir=$datadir")
        fi
        case " ${options[*]} " in
            *" -daemon "*|*" -daemon=1 "*|*" --daemon "*) ;;
            *) options+=("-daemon") ;;
        esac
        "$bindir/paladeumd" "${options[@]}" >/dev/null
    fi
    local waited=0 blocks=""
    sleep 3
    until blocks="$("${cli[@]}" getblockcount 2>/dev/null)"; do
        pgrep -f "^$bindir/paladeumd" >/dev/null || pgrep -x paladeumd >/dev/null || fail "the node stopped while starting; see $datadir/debug.log${keep:+. The old programs are in $keep}"
        [ "$waited" -lt 1800 ] || fail "the node does not answer after 30 minutes; see $datadir/debug.log"
        sleep 5; waited=$((waited + 5))
    done
    echo "running:         $("${cli[@]}" getnetworkinfo | grep '"subversion"' | tr -d ' ,')"
    # the process that answers must run the program that was installed
    local pid_now="" exe_now=""
    for pid in $(pgrep -x paladeumd || true); do
        if [ "$(readlink -f "$(process_datadir "$pid")")" = "$(readlink -f "$datadir")" ]; then
            pid_now="$pid"
        fi
    done
    if [ -n "$pid_now" ]; then
        exe_now="$(readlink "/proc/$pid_now/exe" 2>/dev/null | sed 's/ (deleted)$//' || true)"
    fi
    if [ -n "$exe_now" ] && [ "$exe_now" != "$bindir/paladeumd" ]; then
        echo "WARNING: the node that runs is $exe_now, not $bindir/paladeumd: check how it is started"
    fi
    echo "blocks:          $blocks"

    # ---- coins that 3.0.x left without a time (doc/paladeum/16-stake-coin-time.md)
    case "$old_version" in
        v3.*|v2.*|v1.*)
            if [ "${PLB_REPAIR:-1}" = 1 ]; then
                say "Checking the coins that 3.0.x stored without their time"
                local report
                if report="$("${cli[@]}" repaircoins true 2>&1)"; then
                    echo "$report" | grep -E '"scanned"|"damaged"' || true
                    if ! echo "$report" | grep -q '"damaged": 0'; then
                        "${cli[@]}" repaircoins | grep -E '"repaired"|"unreadable"' || true
                    fi
                else
                    echo "skipped: $report"
                fi
            fi
            ;;
    esac

    say "Done"
    echo "The node is synchronizing in the background:  $bindir/paladeum-cli getblockchaininfo"
    if [ -n "$keep" ]; then
        echo "To go back: stop the node, copy the programs from $keep to $bindir, start it."
    fi
}

main "$@"
