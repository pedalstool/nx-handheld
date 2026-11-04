#!/usr/bin/env bash
# Host test for the audiomon speaker anti-pop guard (unit test only).
# The former tg5050 platform integration checks were removed with that platform.
set -euo pipefail
cd "$(dirname "$0")/../.."

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

( cd workspace/all/audiomon && cc -I. spk_guard.c tests/test_spk_guard.c -o "$TMP/test_spk_guard" )
"$TMP/test_spk_guard"
echo "audiomon speaker anti-pop guard checks passed"
