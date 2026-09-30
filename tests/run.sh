#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_build=$(mktemp -d)
trap 'rm -rf "$test_build"' EXIT
${CC:-cc} -c tests/cJSON.c -o "$test_build/cJSON.o"
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror \
  -Itests/stubs -Itests -Imain -Icomponents/secp256k1/libsecp256k1/include \
  tests/storage_test.cpp main/papers3_storage.cpp "$test_build/cJSON.o" \
  -o "$test_build/storage_test"
"$test_build/storage_test"
${CC:-cc} -std=c11 -c components/M5GFX/src/lgfx/utility/lgfx_qrcode.c -o "$test_build/qrcode.o"
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror \
  -Imain -Icomponents/M5GFX/src tests/tokens_test.cpp "$test_build/qrcode.o" \
  -o "$test_build/tokens_test"
"$test_build/tokens_test" "$@"
