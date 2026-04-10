#!/usr/bin/env bash
# package.sh — build and package Branchage as a Schwung custom module tarball
#
# Usage:
#   ./scripts/package.sh
#   ./scripts/package.sh docker
#   ./scripts/package.sh native
#
# Output:
#   dist/branchage/
#   dist/branchage-module.tar.gz

set -euo pipefail
cd "$(dirname "$0")/.."

# Prevent macOS tar metadata files (._*) from leaking into release archives.
export COPYFILE_DISABLE=1
export COPY_EXTENDED_ATTRIBUTES_DISABLE=1

MODULE_MANIFEST="src/module.json"
UI_SOURCE="src/ui/branchage_ui.js"
DSP_SOURCE="build/aarch64/dsp.so"
BUILD_MODE="${1:-auto}"

module_field() {
    local key="$1"
    sed -n "s/.*\"${key}\":[[:space:]]*\"\\([^\"]*\\)\".*/\\1/p" "${MODULE_MANIFEST}" | head -n1
}

MODULE_ID="$(module_field id)"
MODULE_VERSION="$(module_field version)"

if [ -z "${MODULE_ID}" ] || [ -z "${MODULE_VERSION}" ]; then
    echo "✗ Could not read module id/version from ${MODULE_MANIFEST}"
    exit 1
fi

./scripts/build.sh "${BUILD_MODE}"

if [ ! -f "${DSP_SOURCE}" ]; then
    echo "✗ Missing ${DSP_SOURCE} after build"
    exit 1
fi

if [ ! -f "${UI_SOURCE}" ]; then
    echo "✗ Missing ${UI_SOURCE}"
    exit 1
fi

DIST_DIR="dist"
STAGE_DIR="${DIST_DIR}/${MODULE_ID}"
ARCHIVE_PATH="${DIST_DIR}/${MODULE_ID}-v${MODULE_VERSION}-module.tar.gz"

rm -rf "${STAGE_DIR}" "${ARCHIVE_PATH}"
mkdir -p "${STAGE_DIR}"

cp "${MODULE_MANIFEST}" "${STAGE_DIR}/module.json"
cp "${UI_SOURCE}" "${STAGE_DIR}/ui.js"
# Use dd to strip macOS sparse-file metadata from dsp.so (prevents GNUSparseFile.0/ on Linux)
dd if="${DSP_SOURCE}" of="${STAGE_DIR}/dsp.so" bs=1 2>/dev/null

find "${STAGE_DIR}" \( -name '.DS_Store' -o -name '._*' \) -delete

# Use GNU tar if available (no sparse handling), else bsdtar with --no-xattrs
if command -v gtar &>/dev/null; then
    gtar -C "${DIST_DIR}" -czf "${ARCHIVE_PATH}" "${MODULE_ID}"
else
    tar --no-xattrs -C "${DIST_DIR}" -czf "${ARCHIVE_PATH}" "${MODULE_ID}"
fi

echo "✓ Packaged ${MODULE_ID} v${MODULE_VERSION}"
echo "  Stage dir: ${STAGE_DIR}"
echo "  Archive:   ${ARCHIVE_PATH}"
