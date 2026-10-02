#!/usr/bin/env bash
# Build every variant and refresh release/ (the binaries committed for
# flashing without a toolchain) and its SHA256SUMS.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$HERE"
for m in release locked nomenu app; do ./build.sh "$m" > "build-$m.log" 2>&1 || { cat "build-$m.log"; exit 1; }; rm "build-$m.log"; done
mkdir -p release
cp build/release/chgame_boot.bin release/chgame_sdboot.bin
cp build/locked/chgame_boot.bin  release/chgame_sdboot_locked.bin
cp build/nomenu/chgame_boot.bin  release/chgame_boot_nomenu.bin
cp build/app/chgame_boot.bin     release/chgame_menu_dryrun.bin
(cd release && sha256sum chgame_*.bin > SHA256SUMS && cat SHA256SUMS)
ls -l release
