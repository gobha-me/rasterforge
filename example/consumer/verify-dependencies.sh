#!/usr/bin/env bash
# Verify RasterForge's library-only installed package against either the
# maintained system codecs or the pinned FetchContent fallbacks.
#
#   example/consumer/verify-dependencies.sh system
#   example/consumer/verify-dependencies.sh fetched
set -euo pipefail

MODE="${1:-}"
case "${MODE}" in
  system)
    FORCE_FETCH=OFF
    ;;
  fetched)
    FORCE_FETCH=ON
    ;;
  *)
    echo "usage: $0 system|fetched" >&2
    exit 2
    ;;
esac

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CONSUMER_DIR="${REPO_ROOT}/example/consumer"
NAME="$(basename "${REPO_ROOT}")"
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

BUILD="${WORK}/build"
PREFIX="${WORK}/prefix"
CONSUMER_BUILD="${WORK}/consumer"

cmake -S "${REPO_ROOT}" -B "${BUILD}" \
  -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
  "-D${NAME}_BUILD_BIN=OFF" \
  "-D${NAME}_TESTS=OFF" \
  "-D${NAME}_FUZZERS=OFF" \
  "-D${NAME}_FORCE_FETCH_DEPS=${FORCE_FETCH}"

if [ "${MODE}" = system ]; then
  for source_dir in \
    "${BUILD}/_deps/zlib-src" \
    "${BUILD}/_deps/libpng-src" \
    "${BUILD}/_deps/libjpeg_turbo-src"; do
    if [ -e "${source_dir}" ]; then
      echo "FAIL system: codec dependency was populated through FetchContent:" >&2
      echo "  ${source_dir}" >&2
      exit 1
    fi
  done
else
  for source_dir in \
    "${BUILD}/_deps/zlib-src" \
    "${BUILD}/_deps/libpng-src" \
    "${BUILD}/_deps/libjpeg_turbo-src"; do
    if [ ! -d "${source_dir}" ]; then
      echo "FAIL fetched: expected FetchContent source is missing:" >&2
      echo "  ${source_dir}" >&2
      exit 1
    fi
  done
fi

cmake --build "${BUILD}" --parallel
cmake --install "${BUILD}"

if [ "${MODE}" = system ]; then
  for unexpected in \
    "${PREFIX}/include/png.h" \
    "${PREFIX}/include/zlib.h" \
    "${PREFIX}/include/jpeglib.h"; do
    if [ -e "${unexpected}" ]; then
      echo "FAIL system: dependency leaked into RasterForge's prefix:" >&2
      echo "  ${unexpected}" >&2
      exit 1
    fi
  done
else
  for expected in \
    "${PREFIX}/include/png.h" \
    "${PREFIX}/include/zlib.h" \
    "${PREFIX}/include/jpeglib.h"; do
    if [ ! -f "${expected}" ]; then
      echo "FAIL fetched: dependency install is incomplete: ${expected}" >&2
      exit 1
    fi
  done
fi

cmake -S "${CONSUMER_DIR}" -B "${CONSUMER_BUILD}" \
  -DCONSUMER_MODE=find_package \
  "-DRASTERFORGE_PROJECT_NAME=${NAME}" \
  -DCMAKE_PREFIX_PATH="${PREFIX}"
cmake --build "${CONSUMER_BUILD}" --parallel

GOT="$("${CONSUMER_BUILD}/consumer")"
if [ "${GOT}" != "${NAME}" ]; then
  echo "FAIL ${MODE}: installed consumer printed '${GOT}', expected '${NAME}'" >&2
  exit 1
fi

if [ "${MODE}" = fetched ]; then
  for key in PNG_LIBRARY_RELEASE ZLIB_LIBRARY_RELEASE JPEGTurbo_LIBRARY; do
    value="$(sed -n "s|^${key}:[^=]*=||p" "${CONSUMER_BUILD}/CMakeCache.txt")"
    case "${value}" in
      "${PREFIX}"/*) ;;
      *)
        echo "FAIL fetched: ${key} resolved outside the install prefix:" >&2
        echo "  ${value}" >&2
        exit 1
        ;;
    esac
  done
fi

echo "PASS ${MODE}: library-only install and downstream decode consumer"
