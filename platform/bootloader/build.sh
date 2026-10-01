#!/usr/bin/env bash
# Build the CHGame bootloader (Linux, macOS, or Git Bash on Windows).
#
#   ./build.sh [MODE] [--nolto]
#
# MODE
#   release   the SD game menu bootloader (default)
#   dev       release + developer self-update (DEV_UNLOCK/DEV_WRITE_BOOT)
#   nomenu    no SD menu: the old boot decision on the new update path (HW2a)
#   app       the menu as an ordinary program at 0x3000 (dry run, no USB and no
#             flash writes) for testing the card and the panel under ANY
#             bootloader
# --nolto     build without LTO, for a per-object size breakdown
#
# Output: build/<MODE>/chgame_boot.{elf,bin,map,lst} and a size report.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODE=release
LTO=-flto
for a in "$@"; do
  case "$a" in
    release|dev|nomenu|app) MODE="$a" ;;
    --nolto) LTO= ;;
    *) echo "unknown argument: $a" >&2; exit 1 ;;
  esac
done
OUT="$HERE/build/$MODE"
[ -z "$LTO" ] && OUT="$HERE/build/$MODE-nolto"

# The toolchain ships with the CHGame board package (arduino-cli core install
# CHGame:ch32v); it is found in the usual Arduino data folders. Override with
# CHGAME_TOOLCHAIN=<dir containing riscv-none-embed-gcc>.
TC="${CHGAME_TOOLCHAIN:-}"
if [ -z "$TC" ]; then
  for d in "$HOME/.arduino15" "$HOME/Library/Arduino15" "${LOCALAPPDATA:-/nonexistent}/Arduino15"; do
    for t in "$d"/packages/CHGame/tools/riscv-none-embed-gcc/*/bin "$d"/packages/CH32_Arduino/tools/riscv-none-embed-gcc/*/bin; do
      [ -d "$t" ] && TC="$t"
    done
  done
fi
CC="$TC/riscv-none-embed-gcc"
OBJCOPY="$TC/riscv-none-embed-objcopy"
OBJDUMP="$TC/riscv-none-embed-objdump"
SIZE="$TC/riscv-none-embed-size"
[ -x "$CC" ] || [ -x "$CC.exe" ] || { echo "toolchain not found (set CHGAME_TOOLCHAIN)" >&2; exit 1; }

SPL="$HERE/vendor/spl"
SRC="$HERE/src"
USB="$HERE/vendor/usbcdc"
SHARED="$HERE/shared"

SELFUPDATE=0; MENU=1; APPDEF=; LD="$HERE/ld/link_boot.ld"
case "$MODE" in
  dev)    SELFUPDATE=1 ;;
  nomenu) MENU=0; SELFUPDATE=1 ;;
  app)    APPDEF="-DCHBOOT_APP=1"; LD="$HERE/ld/link_app.ld" ;;
esac

ARCH="-march=rv32imacxw -mabi=ilp32"
DEFS="-DCH32X035 -DSYSCLK_FREQ_48MHz_HSI=48000000 -DF_CPU=48000000 -DCHGAME_IMAGE_ID=1"
DEFS="$DEFS -DCHGAME_ALLOW_SELFUPDATE=$SELFUPDATE -DCHBOOT_MENU=$MENU $APPDEF"
INC="-I$SHARED -I$SRC -I$USB -I$SPL -I$SPL/Core -I$SPL/Peripheral/inc"
WARN="-Wall -Wextra -Wundef -Werror=implicit-function-declaration"
OPT="-Os $LTO -ffunction-sections -fdata-sections -fno-common -msmall-data-limit=8 -msave-restore"
CFLAGS="$ARCH $DEFS $INC $WARN $OPT -std=gnu11 -g"

CSRC=(
  "$SRC/main.c" "$SRC/boot.c" "$SRC/bootreq.c" "$SRC/appmeta.c" "$SRC/crc32.c"
  "$SRC/sys.c" "$SRC/jump.c" "$SRC/fault.c" "$SRC/startup_glue.c" "$SRC/flash.c"
  "$SPL/system_ch32x035.c" "$SPL/Peripheral/src/ch32x035_misc.c"
)
if [ "$MODE" != app ]; then
  CSRC+=( "$SRC/update.c" "$SRC/crc16.c" "$SRC/proto.c" "$SRC/usb.c"
          "$USB/wch_usbcdc_cdc.c" "$USB/wch_usbcdc_descr.c" "$USB/wch_usbcdc_handler.c" )
fi
if [ "$MENU" = 1 ]; then
  CSRC+=( "$SRC/sd.c" "$SRC/fat.c" "$SRC/chg.c" "$SRC/install.c" "$SRC/lcd.c" "$SRC/menu.c" )
fi
ASRC=( "$SRC/startup_chgame_boot.S" )

rm -rf "$OUT"; mkdir -p "$OUT/obj"
if [ -z "$LTO" ]; then
  # Size analysis only: without LTO the image does not fit, so link against a
  # roomier copy of the script. Never flash a --nolto build.
  sed -e 's/LENGTH = 12288/LENGTH = 16384/' -e 's/_etext <= 0x3000/_etext <= 0x4000/' "$LD" > "$OUT/analysis.ld"
  LD="$OUT/analysis.ld"
fi
OBJS=()
for f in "${ASRC[@]}"; do
  o="$OUT/obj/$(basename "$f").o"; OBJS+=("$o")
  "$CC" $ARCH $DEFS -I"$SPL/Startup" -x assembler-with-cpp -c "$f" -o "$o"
done
for f in "${CSRC[@]}"; do
  o="$OUT/obj/$(basename "$f").o"; OBJS+=("$o")
  "$CC" $CFLAGS -c "$f" -o "$o"
done

"$CC" $ARCH $OPT -T "$LD" -nostartfiles -Xlinker --gc-sections \
      --specs=nano.specs --specs=nosys.specs \
      -Wl,-Map,"$OUT/chgame_boot.map" -o "$OUT/chgame_boot.elf" "${OBJS[@]}"
"$OBJCOPY" -O binary "$OUT/chgame_boot.elf" "$OUT/chgame_boot.bin"
"$OBJDUMP" -d -S "$OUT/chgame_boot.elf" > "$OUT/chgame_boot.lst"

echo "== $MODE$([ -z "$LTO" ] && echo " (no LTO)")"
python3 "$HERE/tools/size_report.py" "$OUT/chgame_boot.elf" --size-tool "$SIZE" \
        ${LTO:+} $([ -z "$LTO" ] && echo --objects) \
        $([ "$MODE" = app ] && echo --margin -999999)
