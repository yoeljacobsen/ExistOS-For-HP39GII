#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EDB="${ROOT_DIR}/tools/edb"
SBLOADER="${ROOT_DIR}/tools/sbloader"
OSLOADER_SB="${ROOT_DIR}/build/OSLoader/OSLoader.sb"
EXISTOS_SYS="${ROOT_DIR}/build/System/ExistOS.sys"

if [[ -d "${HOME}/.local/sysroot/usr/lib/x86_64-linux-gnu" ]]; then
    export LD_LIBRARY_PATH="${HOME}/.local/sysroot/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}"
fi

ensure_built() {
    if [[ ! -x "${EDB}" || ! -x "${SBLOADER}" || ! -f "${OSLOADER_SB}" || ! -f "${EXISTOS_SYS}" ]]; then
        echo "==> Building tools and firmware first..."
        "${ROOT_DIR}/build.sh" all
    fi
}

wait_for_eosrecdisk() {
    echo "==> Waiting for calculator EDB interface (EOSRECDISK)..."
    for _ in {1..15}; do
        if lsblk -d -P -p -o HOTPLUG,VENDOR,LABEL,KNAME 2>/dev/null | grep -q 'VENDOR="ExistOS'; then
            return 0
        fi
        sleep 1
    done
    echo "Error: Calculator (cafe:4003 / EOSRECDISK) not detected over USB." >&2
    exit 1
}

CMD="${1:-all}"
case "${CMD}" in
    all)
        ensure_built
        wait_for_eosrecdisk
        echo "==> Flashing OSLoader.sb (page 1408, boot) and ExistOS.sys (page 1984)..."
        "${EDB}" -r -f "${OSLOADER_SB}" 1408 b -f "${EXISTOS_SYS}" 1984
        echo ""
        echo "==> Firmware flash complete! Calculator is rebooting into ExistOS."
        ;;
    loader)
        ensure_built
        wait_for_eosrecdisk
        echo "==> Flashing OSLoader.sb (page 1408, boot)..."
        "${EDB}" -r -f "${OSLOADER_SB}" 1408 b
        echo ""
        echo "==> OSLoader flash complete!"
        ;;
    sys)
        ensure_built
        wait_for_eosrecdisk
        echo "==> Flashing ExistOS.sys (page 1984)..."
        "${EDB}" -r -f "${EXISTOS_SYS}" 1984
        echo ""
        echo "==> ExistOS.sys flash complete!"
        ;;
    ram)
        ensure_built
        echo "==> Loading OSLoader.sb into calculator SRAM via USB HID recovery (066f:3770)..."
        "${SBLOADER}" "${OSLOADER_SB}" -d
        echo "==> OSLoader loaded into SRAM."
        ;;
    msc)
        ensure_built
        wait_for_eosrecdisk
        echo "==> Switching calculator to USB Mass Storage (FAT16) mode..."
        "${EDB}" -m
        ;;
    reboot)
        ensure_built
        wait_for_eosrecdisk
        echo "==> Rebooting calculator..."
        "${EDB}" -r
        ;;
    *)
        echo "Usage: $0 {all|loader|sys|ram|msc|reboot}"
        echo ""
        echo "Commands:"
        echo "  all     Flash both OSLoader.sb and ExistOS.sys, then reboot (default)"
        echo "  loader  Flash only OSLoader.sb (NAND page 1408)"
        echo "  sys     Flash only ExistOS.sys (NAND page 1984)"
        echo "  ram     Bootstrap OSLoader.sb into RAM via USB HID recovery (066f:3770)"
        echo "  msc     Switch calculator into USB Mass Storage mode"
        echo "  reboot  Reboot calculator"
        exit 1
        ;;
esac
