#!/usr/bin/env bash
# Build Paperboat_nro, push it to NetLoader, stream its stdout/stderr, and
# symbolize any "Paperboat+0x..." addresses in the output (crash backtraces).
#
#   switch/run.sh [switch-ip]      (default 192.168.86.191; env SWITCH_IP also works)
#   NOBUILD=1 switch/run.sh        skip the build
#
# Output is also saved to build-switch/last-run.log.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-switch"
IP="${1:-${SWITCH_IP:-192.168.86.191}}"
DKP="${DEVKITPRO:-/opt/devkitpro}"
ADDR2LINE="$DKP/devkitA64/bin/aarch64-none-elf-addr2line"
NXLINK="$DKP/tools/bin/nxlink"
ELF="$BUILD/Paperboat.elf"

if [[ -z "${NOBUILD:-}" ]]; then
    cmake --build "$BUILD" --target Paperboat_nro
fi

# Keep the snapshot used for symbolizing in step with what gets sent.
cp "$ELF" "$BUILD/last-run.elf"

symbolize() {
    while IFS= read -r line; do
        printf '%s\n' "$line"
        if [[ "$line" =~ Paperboat\+0x([0-9a-fA-F]+) ]]; then
            sym="$("$ADDR2LINE" -f -C -i -p -e "$BUILD/last-run.elf" "0x${BASH_REMATCH[1]}" 2>/dev/null || true)"
            printf '        -> %s\n' "$sym"
        fi
    done
}

echo ">> sending to $IP (saved as sdmc:/switch/paperboat/paperboat.nro)"
# -p overwrites the copy in the game folder instead of leaving a second one in sdmc:/switch/.
stdbuf -oL -eL "$NXLINK" -a "$IP" -r 3 -p /switch/paperboat/paperboat.nro -s "$BUILD/paperboat.nro" 2>&1 \
    | symbolize | tee "$BUILD/last-run.log"
