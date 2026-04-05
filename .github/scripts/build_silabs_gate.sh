#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "Usage: $0 <BOARD> [artifact-root]" >&2
    exit 64
fi

BOARD="$1"
ARTIFACT_ROOT="${2:-artifacts/silabs-gates}"
BOARD_ARTIFACT_DIR="${ARTIFACT_ROOT}/${BOARD}"

mkdir -p "${BOARD_ARTIFACT_DIR}"

MCU_FAMILY="$(yq -r ".${BOARD}.mcu_family" device_db.yaml)"
DEVICE_TYPE="$(yq -r ".${BOARD}.device_type" device_db.yaml)"
BUILD_ENABLED="$(yq -r ".${BOARD}.build" device_db.yaml)"

if [[ "${MCU_FAMILY}" != "Silabs" ]]; then
    echo "Board ${BOARD} is not a Silabs target" >&2
    exit 1
fi

if [[ "${BUILD_ENABLED}" != "yes" ]]; then
    echo "Board ${BOARD} is disabled in device_db.yaml" >&2
    exit 1
fi

EXPECTED_PROVENANCE="$(make -s -f board.mk BOARD="${BOARD}" print-silabs-sdk-provenance)"
make -s -f board.mk BOARD="${BOARD}" print-silabs-sdk-record > "${BOARD_ARTIFACT_DIR}/sdk-record.txt"
printf '%s\n' "${EXPECTED_PROVENANCE}" > "${BOARD_ARTIFACT_DIR}/expected-provenance.txt"

echo "==> Building ${BOARD}"
echo "Expected provenance: ${EXPECTED_PROVENANCE}"

if ! BOARD="${BOARD}" make board/build 2>&1 | tee "${BOARD_ARTIFACT_DIR}/build.log"; then
    echo "Build failed for ${BOARD}" >&2
    exit 1
fi

grep -F "${EXPECTED_PROVENANCE}" "${BOARD_ARTIFACT_DIR}/build.log" >/dev/null || {
    echo "Missing provenance line in build log for ${BOARD}" >&2
    exit 1
}

if [[ "${DEVICE_TYPE}" == "end_device" ]]; then
    OUTPUT_DIR="bin/end_device/${BOARD}_END_DEVICE"
else
    OUTPUT_DIR="bin/router/${BOARD}"
fi

test -d "${OUTPUT_DIR}"

find "${OUTPUT_DIR}" -maxdepth 1 -type f \( -name '*.s37' -o -name '*.zigbee' \) | sort > "${BOARD_ARTIFACT_DIR}/artifacts.txt"

if [[ ! -s "${BOARD_ARTIFACT_DIR}/artifacts.txt" ]]; then
    echo "No OTA/build artifacts were generated for ${BOARD}" >&2
    exit 1
fi

cat "${BOARD_ARTIFACT_DIR}/artifacts.txt"
