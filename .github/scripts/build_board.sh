#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
    echo "Usage: $0 <BOARD> <compile_only|full_build> [artifact-root]" >&2
    exit 64
fi

BOARD="$1"
BUILD_MODE="$2"
ARTIFACT_ROOT="${3:-artifacts/builds}"
BOARD_ARTIFACT_DIR="${ARTIFACT_ROOT}/${BOARD}"

mkdir -p "${BOARD_ARTIFACT_DIR}"

MCU_FAMILY="$(yq -r ".${BOARD}.mcu_family" device_db.yaml)"
DEVICE_TYPE="$(yq -r ".${BOARD}.device_type" device_db.yaml)"

case "${BUILD_MODE}" in
    compile_only)
        BUILD_COMMAND=(make -f board.mk BOARD="${BOARD}" drop-old-files build-firmware)
        ;;
    full_build)
        BUILD_COMMAND=(make board/build BOARD="${BOARD}")
        ;;
    *)
        echo "Unsupported build mode: ${BUILD_MODE}" >&2
        exit 64
        ;;
esac

if [[ "${MCU_FAMILY}" == "Silabs" ]]; then
    EXPECTED_PROVENANCE="$(make -s -f board.mk BOARD="${BOARD}" print-silabs-sdk-provenance)"
    make -s -f board.mk BOARD="${BOARD}" print-silabs-sdk-record > "${BOARD_ARTIFACT_DIR}/sdk-record.txt"
    printf '%s\n' "${EXPECTED_PROVENANCE}" > "${BOARD_ARTIFACT_DIR}/expected-provenance.txt"
    echo "Expected provenance: ${EXPECTED_PROVENANCE}"
fi

echo "==> ${BUILD_MODE}: ${BOARD}"

if ! "${BUILD_COMMAND[@]}" 2>&1 | tee "${BOARD_ARTIFACT_DIR}/build.log"; then
    echo "Build failed for ${BOARD}" >&2
    exit 1
fi

if [[ "${MCU_FAMILY}" == "Silabs" ]]; then
    grep -F "${EXPECTED_PROVENANCE}" "${BOARD_ARTIFACT_DIR}/build.log" >/dev/null || {
        echo "Missing provenance line in build log for ${BOARD}" >&2
        exit 1
    }
fi

if [[ "${DEVICE_TYPE}" == "end_device" ]]; then
    OUTPUT_DIR="bin/end_device/${BOARD}_END_DEVICE"
else
    OUTPUT_DIR="bin/router/${BOARD}"
fi

test -d "${OUTPUT_DIR}"

case "${BUILD_MODE}" in
    compile_only)
        find "${OUTPUT_DIR}" -maxdepth 1 -type f \
            \( -name '*.bin' -o -name '*.s37' \) | sort > "${BOARD_ARTIFACT_DIR}/artifacts.txt"
        ;;
    full_build)
        find "${OUTPUT_DIR}" -maxdepth 1 -type f \
            \( -name '*.bin' -o -name '*.s37' -o -name '*.zigbee' \) | sort > "${BOARD_ARTIFACT_DIR}/artifacts.txt"
        ;;
esac

if [[ ! -s "${BOARD_ARTIFACT_DIR}/artifacts.txt" ]]; then
    echo "No build artifacts were generated for ${BOARD}" >&2
    exit 1
fi

cat "${BOARD_ARTIFACT_DIR}/artifacts.txt"
