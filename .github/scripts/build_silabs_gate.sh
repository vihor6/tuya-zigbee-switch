#!/usr/bin/env bash

set -euo pipefail

BOARD="${1:?board is required}"
EXPECTED_SDK_LINE="${2:?expected sdk line is required}"
EXPECTED_SDK_VERSION="${3:?expected sdk version is required}"

DEVICE_TYPE="$(yq -r ".${BOARD}.device_type" device_db.yaml)"
MCU="$(yq -r ".${BOARD}.mcu" device_db.yaml)"
MCU_FAMILY="$(yq -r ".${BOARD}.mcu_family" device_db.yaml)"
BUILD_ENABLED="$(yq -r ".${BOARD}.build // true" device_db.yaml)"

LOG_DIR="${LOG_DIR:-build-logs}"
LOG_FILE="${LOG_DIR}/${BOARD}.log"

if [[ "${BUILD_ENABLED}" == "false" ]]; then
    echo "::error::${BOARD} is disabled in device_db.yaml"
    exit 1
fi

if [[ "${MCU_FAMILY}" != "Silabs" ]]; then
    echo "::error::${BOARD} is not a Silabs board (mcu_family=${MCU_FAMILY})"
    exit 1
fi

mkdir -p "${LOG_DIR}"
rm -f "${LOG_FILE}"

if [[ -f .venv/bin/activate ]]; then
    # shellcheck disable=SC1091
    source .venv/bin/activate
fi

echo "::group::${BOARD} provenance expectation"
echo "BOARD=${BOARD}"
echo "DEVICE_TYPE=${DEVICE_TYPE}"
echo "MCU=${MCU}"
echo "SDK_LINE=${EXPECTED_SDK_LINE}"
echo "SDK_VERSION=${EXPECTED_SDK_VERSION}"
echo "::endgroup::"

BOARD="${BOARD}" DEVICE_TYPE="${DEVICE_TYPE}" make board/build 2>&1 | tee "${LOG_FILE}"

for required in \
    "BOARD=${BOARD}" \
    "MCU=${MCU}" \
    "SDK_LINE=${EXPECTED_SDK_LINE}" \
    "SDK_VERSION=${EXPECTED_SDK_VERSION}"; do
    if ! grep -F "${required}" "${LOG_FILE}" >/dev/null; then
        echo "::error::Missing provenance token '${required}' in ${LOG_FILE}"
        tail -n 200 "${LOG_FILE}"
        exit 1
    fi
done

OUTPUT_DIR="bin/router/${BOARD}"
if [[ "${DEVICE_TYPE}" == "end_device" ]]; then
    OUTPUT_DIR="bin/end_device/${BOARD}_END_DEVICE"
fi

if ! find "${OUTPUT_DIR}" -maxdepth 1 -type f \( -name '*.s37' -o -name '*.zigbee' \) | grep -q .; then
    echo "::error::Expected build artifacts were not created under ${OUTPUT_DIR}"
    exit 1
fi

if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
    {
        echo "### ${BOARD}"
        echo "- MCU: \`${MCU}\`"
        echo "- Expected SDK: \`${EXPECTED_SDK_LINE} ${EXPECTED_SDK_VERSION}\`"
        echo "- Log: \`${LOG_FILE}\`"
    } >> "${GITHUB_STEP_SUMMARY}"
fi
