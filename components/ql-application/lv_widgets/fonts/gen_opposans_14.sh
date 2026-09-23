#!/usr/bin/env bash
# 用 OPPO Sans 4.0 重出 opposans_*.c：ASCII + CJK 标点 + 汉字基本区全集。
# 默认字库：/home/gengtao/work_8910_LVGL/OPPO_Sans_4.0/OPPO_Sans_4.0/OPPO Sans 4.0.ttf
# 用法：FONT=/path/to/OPPO\ Sans\ 4.0.ttf ./gen_opposans_14.sh [size ...]
# 默认 size：14 18。14=正文，18=标题。
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "${HERE}/../../../.." && pwd)"
CONV="${REPO}/tools/lvgl/lv_font_conv/lv_font_conv.js"
FONT="${FONT:-/home/gengtao/work_8910_LVGL/OPPO_Sans_4.0/OPPO_Sans_4.0/OPPO Sans 4.0.ttf}"
BPP="${BPP:-4}"

if [[ ! -f "${FONT}" ]]; then
    echo "error: font not found: ${FONT}" >&2
    exit 1
fi
if [[ ! -f "${CONV}" ]]; then
    echo "error: missing ${CONV}" >&2
    exit 1
fi
if [[ ! -d "$(dirname "${CONV}")/node_modules" ]]; then
    echo "install lv_font_conv deps..."
    (cd "$(dirname "${CONV}")" && npm install --omit=dev)
fi

gen_one() {
    local size="$1"
    local out="${HERE}/opposans_${size}.c"
    echo "rasterize ${FONT} -> ${out} (size=${size} bpp=${BPP})"
    node "${CONV}" \
        --font "${FONT}" \
        --range 0x20-0x7F \
        --range 0x3000-0x303F \
        --range 0x4E00-0x9FA5 \
        --range 0xFF01-0xFF5E \
        --size "${size}" \
        --bpp "${BPP}" \
        --no-kerning \
        --format lvgl \
        --lv-include lvgl/lvgl.h \
        -o "${out}"

    OUT="${out}" python3 - <<'PY'
import os
from pathlib import Path
p = Path(os.environ["OUT"])
t = p.read_text()
old = '#include "lvgl/lvgl.h"\n'
new = (
    "#ifdef PLATFORM_EC600\n"
    "#include \"lvgl.h\"\n"
    "#else\n"
    "#include \"lvgl/lvgl.h\"\n"
    "#endif\n"
)
if t.startswith(old):
    p.write_text(new + t[len(old):])
print("ok:", p, "bytes", p.stat().st_size)
PY
}

if [[ $# -eq 0 ]]; then
    set -- 14 18
fi
for size in "$@"; do
    gen_one "${size}"
done
