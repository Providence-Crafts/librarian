#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENDOR_DIR="${SCRIPT_DIR}/vendor"
mkdir -p "${VENDOR_DIR}"

# Pinned to the commits librarian is tested against; bump deliberately.
TOMLC99_REV="29076dfd095bbbbd50a3c1b2760d29f4b83e74ac"
LLAMA_REV="f872b591121761ac7b2af18283bd99bdc092a63a" # b11301-9

# Shallow-clone one commit of a repository into a directory.
fetch_rev() {
    git init -q "$3"
    git -C "$3" fetch -q --depth 1 "$1" "$2"
    git -C "$3" -c advice.detachedHead=false checkout -q FETCH_HEAD
}

echo "=== Librarian Vendor Setup ==="

# 1. SQLite3 amalgamation
SQLITE_DIR="${VENDOR_DIR}/sqlite"
if [ ! -f "${SQLITE_DIR}/sqlite3.c" ] || [ ! -f "${SQLITE_DIR}/sqlite3.h" ]; then
    echo "[1/4] Fetching SQLite3 amalgamation..."
    mkdir -p "${SQLITE_DIR}"
    SQLITE_YEAR="2024"
    SQLITE_VERSION="3460100" # SQLite 3.46.1
    TMP_ZIP="/tmp/sqlite-amalgamation-${SQLITE_VERSION}.zip"
    curl -fsSL "https://www.sqlite.org/${SQLITE_YEAR}/sqlite-amalgamation-${SQLITE_VERSION}.zip" -o "${TMP_ZIP}"
    unzip -q -j "${TMP_ZIP}" "sqlite-amalgamation-${SQLITE_VERSION}/sqlite3.*" -d "${SQLITE_DIR}"
    rm -f "${TMP_ZIP}"
    echo "  -> SQLite3 installed to ${SQLITE_DIR}"
else
    echo "[1/4] SQLite3 amalgamation present."
fi

# 2. sqlite-vec amalgamation
SQLITE_VEC_DIR="${VENDOR_DIR}/sqlite-vec"
if [ ! -f "${SQLITE_VEC_DIR}/sqlite-vec.c" ] || [ ! -f "${SQLITE_VEC_DIR}/sqlite-vec.h" ]; then
    echo "[2/4] Fetching sqlite-vec release..."
    mkdir -p "${SQLITE_VEC_DIR}"
    VEC_VERSION="v0.1.6"
    TMP_TAR="/tmp/sqlite-vec-${VEC_VERSION}.tar.gz"
    curl -fsSL "https://github.com/asg017/sqlite-vec/releases/download/${VEC_VERSION}/sqlite-vec-${VEC_VERSION}-amalgamation.tar.gz" -o "${TMP_TAR}"
    tar -xzf "${TMP_TAR}" -C "${SQLITE_VEC_DIR}" --strip-components=1 2>/dev/null || tar -xzf "${TMP_TAR}" -C "${SQLITE_VEC_DIR}"
    rm -f "${TMP_TAR}"
    echo "  -> sqlite-vec installed to ${SQLITE_VEC_DIR}"
else
    echo "[2/4] sqlite-vec amalgamation present."
fi

# 3. tomlc99
TOMLC99_DIR="${VENDOR_DIR}/tomlc99"
if [ ! -d "${TOMLC99_DIR}/.git" ] && [ ! -f "${TOMLC99_DIR}/toml.c" ]; then
    echo "[3/4] Cloning tomlc99..."
    fetch_rev https://github.com/cktan/tomlc99 "${TOMLC99_REV}" "${TOMLC99_DIR}"
    echo "  -> tomlc99 cloned to ${TOMLC99_DIR}"
else
    echo "[3/4] tomlc99 present."
fi

# 4. llama.cpp static compilation
LLAMA_DIR="${VENDOR_DIR}/llama.cpp"
if [ ! -d "${LLAMA_DIR}" ]; then
    echo "[4/4] Cloning llama.cpp..."
    fetch_rev https://github.com/ggml-org/llama.cpp "${LLAMA_REV}" "${LLAMA_DIR}"
fi

# The Windows build compiles its own llama.cpp (make windows).
if [ "${SKIP_LLAMA_BUILD:-0}" = 1 ]; then
    echo "=== Vendor sources fetched; llama.cpp build skipped ==="
    exit 0
fi

echo "[4/4] Building llama.cpp static libraries..."
cmake -B "${LLAMA_DIR}/build" -S "${LLAMA_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DLLAMA_BUILD_EXAMPLES=OFF \
    -DLLAMA_BUILD_TESTS=OFF \
    -DLLAMA_BUILD_SERVER=OFF \
    -DGGML_BUILD_EXAMPLES=OFF \
    -DGGML_BUILD_TESTS=OFF \
    -DGGML_CCACHE=OFF

cmake --build "${LLAMA_DIR}/build" --config Release --target llama -j"$(nproc)"

echo "=== All vendor dependencies successfully verified and compiled ==="
