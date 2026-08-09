#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

if printf '%s' "$PROJECT_ROOT" | LC_ALL=C grep -q '[^-A-Za-z0-9_./@() ]'; then
  TEMP_ROOT="$(mktemp -d /tmp/harmony-git-bash-build.XXXXXX)"
  trap 'rm -rf "$TEMP_ROOT"' EXIT
  TEMP_PROJECT="$TEMP_ROOT/project"
  rsync -a \
    --exclude '.git' \
    --exclude '.hvigor' \
    --exclude 'oh_modules' \
    --exclude 'build' \
    "$PROJECT_ROOT/" "$TEMP_PROJECT/"
  (
    cd "$TEMP_PROJECT"
    bash ./scripts/verify.sh
  )
  HAP_PATH="entry/build/default/outputs/default/entry-default-unsigned.hap"
  mkdir -p "$(dirname "$PROJECT_ROOT/$HAP_PATH")"
  cp "$TEMP_PROJECT/$HAP_PATH" "$PROJECT_ROOT/$HAP_PATH"
  echo "Copied unsigned HAP to $PROJECT_ROOT/$HAP_PATH"
  exit 0
fi

: "${DEVECO_SDK_HOME:=/Applications/DevEco-Studio.app/Contents/sdk}"
: "${JAVA_HOME:=/Applications/DevEco-Studio.app/Contents/jbr/Contents/Home}"
export DEVECO_SDK_HOME JAVA_HOME
export PATH="$JAVA_HOME/bin:$PATH"

HVIGOR="${HVIGOR:-/Applications/DevEco-Studio.app/Contents/tools/hvigor/bin/hvigorw}"
OHPM="${OHPM:-/Applications/DevEco-Studio.app/Contents/tools/ohpm/bin/ohpm}"

HOST_TEST_ROOT="$(mktemp -d /tmp/harmony-git-native-test.XXXXXX)"
trap 'rm -rf "$HOST_TEST_ROOT"' EXIT
"${CXX:-c++}" \
  -std=c++17 \
  -Wall \
  -Wextra \
  -Werror \
  -Ientry/src/main/cpp \
  entry/src/main/cpp/git_repository.cpp \
  entry/src/main/cpp/git_transport.cpp \
  tests/native/git_repository_test.cpp \
  -lz \
  -o "$HOST_TEST_ROOT/git_repository_test"
"$HOST_TEST_ROOT/git_repository_test"

"$OHPM" install
"$HVIGOR" --stop-daemon || true
"$HVIGOR" --mode module -p module=entry@default -p product=default test --analyze=normal
"$HVIGOR" --mode module -p module=entry@default -p product=default assembleHap --analyze=normal
