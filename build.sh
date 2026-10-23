#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${ROOT_DIR}"

# Prefer local ARM GCC 10.3 toolchain if installed
if [[ -d "${HOME}/.local/opt/gcc-arm-none-eabi-10.3-2021.10/bin" ]]; then
    export PATH="${HOME}/.local/opt/gcc-arm-none-eabi-10.3-2021.10/bin:${PATH}"
elif [[ -d "/opt/gcc-arm-none-eabi-10.3-2021.10/bin" ]]; then
    export PATH="/opt/gcc-arm-none-eabi-10.3-2021.10/bin:${PATH}"
fi

# Prefer local sysroot pkg-config if present
if [[ -d "${HOME}/.local/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig" ]]; then
    export PKG_CONFIG_PATH="${HOME}/.local/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig:${PKG_CONFIG_PATH:-}"
fi

build_tools() {
    echo "==> Building Linux host tools..."

    echo "  -> Building tools/sbtools (elftosb, sbloader)..."
    make -C "${ROOT_DIR}/tools/sbtools" -j"$(nproc)"
    cp -f "${ROOT_DIR}/tools/sbtools/elftosb" "${ROOT_DIR}/tools/elftosb"
    cp -f "${ROOT_DIR}/tools/sbtools/sbloader" "${ROOT_DIR}/tools/sbloader"

    echo "  -> Building tools/sys_signer (sysigner)..."
    cmake -S "${ROOT_DIR}/tools/sys_signer" -B "${ROOT_DIR}/tools/sys_signer/build"
    cmake --build "${ROOT_DIR}/tools/sys_signer/build" -j"$(nproc)"
    cp -f "${ROOT_DIR}/tools/sys_signer/build/sysigner" "${ROOT_DIR}/tools/sysigner"

    if [[ -f "${ROOT_DIR}/tools/edb-unix/CMakeLists.txt" ]]; then
        echo "  -> Building tools/edb-unix (edb)..."
        cmake -S "${ROOT_DIR}/tools/edb-unix" -B "${ROOT_DIR}/tools/edb-unix/build"
        cmake --build "${ROOT_DIR}/tools/edb-unix/build" -j"$(nproc)"
        cp -f "${ROOT_DIR}/tools/edb-unix/build/edb" "${ROOT_DIR}/tools/edb"
    fi

    echo "==> Host tools ready in ${ROOT_DIR}/tools/"
}

build_firmware() {
    if [[ ! -x "${ROOT_DIR}/tools/sbtools/elftosb" || ! -x "${ROOT_DIR}/tools/sysigner" || ! -x "${ROOT_DIR}/tools/edb" ]]; then
        build_tools
    fi

    echo "==> Configuring and building ExistOS firmware (OSLoader + System)..."
    cmake -S "${ROOT_DIR}" -B "${ROOT_DIR}/build"
    cmake --build "${ROOT_DIR}/build" -j"$(nproc)"

    echo ""
    echo "==> Build complete:"
    ls -lh "${ROOT_DIR}/build/OSLoader/OSLoader.sb" "${ROOT_DIR}/build/System/ExistOS.sys"
}

CMD="${1:-all}"
case "${CMD}" in
    all)
        build_firmware
        ;;
    tools)
        build_tools
        ;;
    firmware)
        build_firmware
        ;;
    clean)
        rm -rf "${ROOT_DIR}/build" \
               "${ROOT_DIR}/tools/sys_signer/build" \
               "${ROOT_DIR}/tools/edb-unix/build"
        make -C "${ROOT_DIR}/tools/sbtools" veryclean clean
        rm -f "${ROOT_DIR}/tools/elftosb" "${ROOT_DIR}/tools/sbloader" "${ROOT_DIR}/tools/sysigner" "${ROOT_DIR}/tools/edb"
        echo "==> Cleaned build artifacts."
        ;;
    flash-ram)
        "${ROOT_DIR}/tools/sbloader" "${ROOT_DIR}/build/OSLoader/OSLoader.sb" -d
        ;;
    flash-loader)
        "${ROOT_DIR}/tools/edb" -r -s -f "${ROOT_DIR}/build/OSLoader/OSLoader.sb" 1408 b
        ;;
    flash-sys)
        "${ROOT_DIR}/tools/edb" -r -s -f "${ROOT_DIR}/build/System/ExistOS.sys" 1984
        ;;
    *)
        echo "Usage: $0 {all|tools|firmware|clean|flash-ram|flash-loader|flash-sys}"
        exit 1
        ;;
esac
