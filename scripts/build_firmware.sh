#!/usr/bin/env bash
# Required packages:
#   - qtbase5-dev
# Build the DHL Courier firmware for native simulation (native_sim).
#
# Builds are kept per-board inside firmware/build/:
#   firmware/build/native_sim/   <-- this script
#   firmware/build/esp32/        <-- built separately, NOT touched by this script
#
# Usage:
#   ./scripts/build_firmware.sh            # incremental build
#   ./scripts/build_firmware.sh --pristine # clean rebuild

set -euo pipefail

# Zephyr workspace — west must be invoked from here.
ZEPHYR_WORKSPACE="${HOME}/zephyrproject"

# Activate the Zephyr virtual environment so that 'west' is available.
ZEPHYR_VENV="${ZEPHYR_WORKSPACE}/.venv"
if [[ -f "${ZEPHYR_VENV}/bin/activate" ]]; then
    # shellcheck source=/dev/null
    source "${ZEPHYR_VENV}/bin/activate"
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIRMWARE_DIR="${REPO_ROOT}/firmware"
BOARD="native_sim/native/64"
BOARD_DIR_NAME="native_sim"
BUILD_DIR="${FIRMWARE_DIR}/build/${BOARD_DIR_NAME}"

PRISTINE_FLAG=""
if [[ "${1:-}" == "--pristine" ]]; then
    PRISTINE_FLAG="--pristine"
fi

echo "==> Building firmware for ${BOARD}"
echo "    source : ${FIRMWARE_DIR}"
echo "    output : ${BUILD_DIR}"
echo ""

# west must run from within the Zephyr workspace to locate its topology.
pushd "${ZEPHYR_WORKSPACE}" > /dev/null

west build \
    -s "${FIRMWARE_DIR}" \
    -d "${BUILD_DIR}" \
    ${PRISTINE_FLAG} \
    -b "${BOARD}"

popd > /dev/null

echo ""
echo "==> Build complete."
echo "    Run with: ${BUILD_DIR}/zephyr/zephyr.exe"
echo ""
echo "    Test commands:"
echo "      # Subscribe to all topics:"
echo "      docker exec -it dhl_mosquitto mosquitto_sub -t '#' -v"
echo ""
echo "      # Push a route:"
echo "      docker exec dhl_mosquitto mosquitto_pub -t 'routes/courier-001' \\"
echo "        -m '[\"Mercado Sur\",\"Mercado Norte\",\"FCEFYN\"]'"
