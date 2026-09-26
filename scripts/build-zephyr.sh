#!/usr/bin/env bash
# Zephyr 포트(firmware/wallet)를 빌드해 firmware/wallet/build/zephyr/zephyr.uf2 를 만든다.
# 작업 공간 준비는 docs/nu40-dk-firmware-installation.md 의 "Zephyr 로 빌드하기"를 본다.
#
#   ZEPHYR_WS=~/zephyr-nu scripts/build-zephyr.sh           증분 빌드
#   ZEPHYR_WS=~/zephyr-nu scripts/build-zephyr.sh pristine  처음부터 다시
#
# 올리는 것은 이 스크립트가 하지 않는다. zephyr.uf2 는 SoftDevice 자리까지 덮어쓰므로
# 사람이 확인하고 올려야 한다 (같은 문서의 "Zephyr 펌웨어 올리기").
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
APP="$ROOT/firmware/wallet"
OUT="$APP/build"
BOARD=nucode_nu40/nrf52840
WS=${ZEPHYR_WS:-}

[ -n "$WS" ] || { echo "✗ ZEPHYR_WS 에 west 작업 공간 경로를 넣어 주세요." >&2; exit 1; }
[ -d "$WS/zephyr" ] || { echo "✗ $WS/zephyr 가 없습니다. 'west update' 를 했는지 보세요." >&2; exit 1; }
if [ -f "$WS/.venv/bin/activate" ]; then
  # shellcheck disable=SC1091
  . "$WS/.venv/bin/activate"
fi
command -v west >/dev/null || { echo "✗ west 가 없습니다." >&2; exit 1; }

# 작업 공간의 Zephyr 가 west.yml 이 고정한 커밋인지 본다. 다르면 보드 정의나
# Kconfig 가 달라 여기서 확인한 결과와 어긋날 수 있다. 막지는 않고 알린다.
WANT=$(sed -n 's/^ *revision: *\([0-9a-f]\{40\}\).*/\1/p' "$APP/west.yml" | head -1)
HAVE=$(git -C "$WS/zephyr" rev-parse HEAD)
[ "$WANT" = "$HAVE" ] || echo "⚠ Zephyr 가 $HAVE 입니다. west.yml 은 $WANT 에 고정되어 있습니다." >&2

export ZEPHYR_BASE="$WS/zephyr"
PRISTINE=auto
[ "${1:-}" = "pristine" ] && PRISTINE=always
west build -p "$PRISTINE" -b "$BOARD" -d "$OUT" "$APP"

echo "✓ $OUT/zephyr/zephyr.uf2"
