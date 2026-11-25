#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Build MONODRONE with fm1-base (base/, a git submodule).
#   ./build.sh [--release X.Y] [--host | --web]
#   JIELI_TOOLCHAIN  JieLi Linux toolchain (default: ~/.jieli/toolchain)
#   AC79_SDK         a JieLi AC79 SDK checkout, or just its three files (default: ~/fw-AC79_AIoT_SDK)
set -e
cd "$(dirname "$0")"
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
[ -f base/tools/build.py ] || { echo "build.sh: no fm1-base in base/ (git submodule update --init --recursive)"; exit 1; }
exec "${PYTHON:-python3}" base/tools/build.py app "$@"
