#!/usr/bin/env bash

set -euo pipefail

echo "[check-no-filesystem] scanning tracked C/C++ sources..."

if ! command -v git >/dev/null 2>&1; then
  echo "[check-no-filesystem] git is required" >&2
  exit 2
fi

# Scan tracked C/C++ files only.
mapfile -t CXX_FILES < <(git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx')

if [[ ${#CXX_FILES[@]} -eq 0 ]]; then
  echo "[check-no-filesystem] no tracked C/C++ files found"
  exit 0
fi

TMP_OUT=""
cleanup() {
  if [[ -n "${TMP_OUT}" && -f "${TMP_OUT}" ]]; then
    rm -f "${TMP_OUT}"
  fi
}
trap cleanup EXIT

TMP_OUT="$(mktemp)"

# We block both include and namespace usage.
# Ignore the explanatory comment in PathHelper.h.
if ! grep -nH -E '^[[:space:]]*#[[:space:]]*include[[:space:]]*<filesystem>|std::filesystem' "${CXX_FILES[@]}" \
  | grep -v '^lib/include/PathHelper.h:16:' > "${TMP_OUT}"; then
  true
fi

if [[ -s "${TMP_OUT}" ]]; then
  echo ""
  echo "ERROR: std::filesystem usage is not allowed in this repository."
  echo ""
  echo "Use aiSocks helpers instead:"
  echo "  - PathHelper::normalizePath(path)"
  echo "  - PathHelper::joinPath(base, component)"
  echo "  - PathHelper::createDirectories(path)"
  echo "  - PathHelper::removeAll(path)"
  echo "  - PathHelper::tempDirectory()"
  echo "  - FileIO::File for file reads/writes"
  echo ""
  echo "Found occurrences:"
  sed 's/^/  /' "${TMP_OUT}"
  exit 1
fi

echo "[check-no-filesystem] OK: no std::filesystem usage found"
