#!/usr/bin/env bash
# 지갑 펌웨어를 빌드해 firmware/nuwallet/build/ 에 .hex, .zip, NUWALLET.UF2 를 만든다.
# 환경 준비는 docs/nu40-dk-firmware-installation.md 를 본다.
#
#   scripts/build-firmware.sh                       빌드만
#   scripts/build-firmware.sh /dev/cu.usbmodem1101  빌드하고 시리얼 DFU 로 올린다
set -euo pipefail

FQBN=nucode:nrf52:nu40dk
UF2_FAMILY=0xADA52840          # nRF52840. 코어 boards.txt 의 nu40dk.build.uf2_family
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SKETCH="$ROOT/firmware/nuwallet"
OUT="$SKETCH/build"
PORT=${1:-}

need() { command -v "$1" >/dev/null || { echo "✗ $1 이 없습니다. 설치 가이드의 '한 번만 할 준비'를 보세요." >&2; exit 1; }; }
need arduino-cli
need python3
need adafruit-nrfutil

DATA=$(arduino-cli config get directories.data)
CORE=$(ls -d "$DATA"/packages/nucode/hardware/nrf52/* 2>/dev/null | sort -V | tail -1 || true)
[ -n "$CORE" ] || { echo "✗ nucode:nrf52 코어가 없습니다. 'arduino-cli core install nucode:nrf52'" >&2; exit 1; }

# 코어가 macOS 용으로 싣고 오는 adafruit-nrfutil 은 실행 권한 없이 풀리고, x86_64
# 바이너리라 Apple Silicon 에서는 Rosetta 까지 필요하다. PATH 의 것(pip 설치)을 쓰게 한다.
# Linux 는 원래 PATH 의 것을 쓴다.
arduino-cli compile --fqbn "$FQBN" \
  --build-property "tools.nrfutil.cmd.macosx=adafruit-nrfutil" \
  --output-dir "$OUT" "$SKETCH"

# 코어의 platform.txt 는 UF2 생성 규칙을 주석으로 막아 두었다. 변환기를 직접 돌린다.
python3 "$CORE/tools/uf2conv/uf2conv.py" -f "$UF2_FAMILY" -c \
  -o "$OUT/NUWALLET.UF2" "$OUT/nuwallet.ino.hex"

echo "✓ $OUT/NUWALLET.UF2"

if [ -n "$PORT" ]; then
  # 1200bps 신호로 보드를 부트로더에 넣고 올린다. 첫 시도가 부트로더가 뜨기 전에
  # 붙어 실패하는 일이 있어 한 번 더 한다.
  for try in 1 2; do
    if adafruit-nrfutil dfu serial --package "$OUT/nuwallet.ino.zip" -p "$PORT" \
         -b 115200 --singlebank --touch 1200; then
      echo "✓ 올렸습니다. OS 블루투스 설정에서 기존 NuWallet-… 기기를 잊은 뒤 연결하세요."
      exit 0
    fi
    echo "… 다시 시도합니다 ($try/2)"; sleep 3
  done
  exit 1
fi
