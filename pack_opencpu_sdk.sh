#!/usr/bin/env bash
# 从完整方案树打客户 OpenCPU SDK：只留编 APPIMG 所需，不含 kernel/hal/driver 源码。
# 须先：./build_all.sh
# 用法：./pack_opencpu_sdk.sh [输出目录]

set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

OUT_BUILD="${OUT_BUILD:-$ROOT/out/8915DM_cat1_open_release}"
TARGET_DIR="${TARGET_DIR:-$ROOT/target/C51}"
DEST="${1:-$ROOT/OpenCPU_SDK_Output/OpenCPU_SDK_C51}"

need() {
    [[ -e "$1" ]] || {
        echo "missing $1"
        echo "run ./build_all.sh first (release C56U_HM_SH8)"
        exit 1
    }
}

need "$OUT_BUILD/lib/core_stub.o"
need "$OUT_BUILD/include"
need "$OUT_BUILD/target.cmake"
need "$OUT_BUILD/partinfo.cmake"
need "$TARGET_DIR/prepack/fdl1.sign.img"
need "$TARGET_DIR/prepack/fdl2.sign.img"

echo "packing SDK -> $DEST"
rm -rf "$DEST"
mkdir -p "$DEST/sdk_prebuilt" "$DEST/components"

copy_tree() {
    local src="$1" dst="$2"
    mkdir -p "$(dirname "$dst")"
    # dst 已存在时 cp -a src dst 会变成 dst/src，cmake/ 曾因此套进一层
    if [[ -d "$src" && -d "$dst" ]]; then
        cp -a "$src"/. "$dst"/
    else
        cp -a "$src" "$dst"
    fi
}

# --- 客户可改 / 必须有的源 ---
copy_tree "$ROOT/components/ql-application" "$DEST/components/ql-application"
copy_tree "$ROOT/components/ql-kernel/inc" "$DEST/components/ql-kernel/inc"
copy_tree "$ROOT/projects" "$DEST/projects"

# --- 公开头（不要 src）---
copy_tree "$ROOT/components/kernel/include" "$DEST/components/kernel/include"
copy_tree "$ROOT/components/hal/include" "$DEST/components/hal/include"
copy_tree "$ROOT/components/driver/include" "$DEST/components/driver/include"
if [[ -d "$ROOT/components/fs/include" ]]; then
    copy_tree "$ROOT/components/fs/include" "$DEST/components/fs/include"
fi
if [[ -d "$ROOT/components/fs/fsmount/include" ]]; then
    mkdir -p "$DEST/components/fs/fsmount"
    copy_tree "$ROOT/components/fs/fsmount/include" "$DEST/components/fs/fsmount/include"
fi

# 应用/ql_api 会间接 include 的公开头（只要 include，不要 src）
copy_hdr() {
    local src="$1" dst="$2"
    [[ -d "$src" ]] || return 0
    copy_tree "$src" "$dst"
}
copy_hdr "$ROOT/components/cfw/include" "$DEST/components/cfw/include"
copy_hdr "$ROOT/components/net/include" "$DEST/components/net/include"
mkdir -p "$DEST/components/net/lwip"
copy_hdr "$ROOT/components/net/lwip/include" "$DEST/components/net/lwip/include"
copy_hdr "$ROOT/components/net/lwip/src/include" "$DEST/components/net/lwip/src/include"
copy_hdr "$ROOT/components/net/mbedtls/include" "$DEST/components/net/mbedtls/include"

# libc、链接脚本、芯片/模组配置（无内核 .c）
copy_tree "$ROOT/components/newlib" "$DEST/components/newlib"
mkdir -p "$DEST/components/apploader"
copy_tree "$ROOT/components/apploader/pack" "$DEST/components/apploader/pack"
copy_tree "$ROOT/components/chip" "$DEST/components/chip"
mkdir -p "$DEST/components/ql-config/build"
copy_tree "$ROOT/components/ql-config/build/EC600UCN_LB" "$DEST/components/ql-config/build/EC600UCN_LB"

# --- 工具链与脚本 ---
copy_tree "$ROOT/cmake" "$DEST/cmake"
copy_tree "$ROOT/tools" "$DEST/tools"
copy_tree "$ROOT/prebuilts/linux" "$DEST/prebuilts/linux"
# 客户不需要完整 win32 也能 Linux 编；需要 Windows 时再拷
[[ -f "$ROOT/image_gen.py" ]] && cp -a "$ROOT/image_gen.py" "$DEST/"

# --- 与底包锁死的预置 ---
cp -a "$OUT_BUILD/lib/core_stub.o" "$DEST/sdk_prebuilt/"
cp -a "$OUT_BUILD/target.cmake" "$DEST/sdk_prebuilt/"
cp -a "$OUT_BUILD/partinfo.cmake" "$DEST/sdk_prebuilt/"
cp -a "$OUT_BUILD/include" "$DEST/sdk_prebuilt/include"
# 不出整机 pac；FDL 只给客户打 APPIMG 下载包用
cp -a "$TARGET_DIR/prepack/fdl1.sign.img" "$DEST/sdk_prebuilt/"
cp -a "$TARGET_DIR/prepack/fdl2.sign.img" "$DEST/sdk_prebuilt/"
[[ -f "$OUT_BUILD/partinfo.bin" ]] && cp -a "$OUT_BUILD/partinfo.bin" "$DEST/sdk_prebuilt/"
[[ -f "$OUT_BUILD/hex/partinfo.bin" ]] && cp -a "$OUT_BUILD/hex/partinfo.bin" "$DEST/sdk_prebuilt/"

# 客户 CMake / 入口（覆盖拷过去的 cmake 里那份说明即可）
cp -a "$ROOT/cmake/sdk_app_only.cmake" "$DEST/CMakeLists.txt"
cp -a "$ROOT/sdk/build_app.sh" "$DEST/build_app.sh"
chmod +x "$DEST/build_app.sh"

# 只删内核三件套的实现，不要误伤 ql-application/lv_widgets/hal/src
rm -rf \
    "$DEST/components/kernel/src" \
    "$DEST/components/kernel/freertos" \
    "$DEST/components/hal/src" \
    "$DEST/components/driver/src" \
    "$DEST/components/bootloader" \
    "$DEST/components/ql-kernel/libs" \
    "$DEST/components/ql-kernel/drivers"

# 文档
mkdir -p "$DEST/doc"
if [[ -f "$ROOT/gengtao_doc/15_客户SDK交付.md" ]]; then
    cp -a "$ROOT/gengtao_doc/15_客户SDK交付.md" "$DEST/doc/"
    cp -a "$ROOT/gengtao_doc/12_客户开发说明.md" "$DEST/doc/" 2>/dev/null || true
    cp -a "$ROOT/gengtao_doc/14_OpenCPU开发说明.md" "$DEST/doc/" 2>/dev/null || true
fi

cat > "$DEST/README.txt" <<EOF
Quectel 8910 OpenCPU 客户 SDK（仅 APPIMG）
底包版本: C51   stub: sdk_prebuilt/core_stub.o

编应用:
  ./build_app.sh
  ./build_app.sh new

下载（只下应用分区 APPIMG）:
  target/C51/app/C51_APP.pac

整机底包由方案烧，本 SDK 不提供 merge.pac。
不要改 sdk_prebuilt/。换底包必须向方案重新要 SDK。
不含 kernel/hal/driver 源码。
EOF

# 体积提示
echo
echo "done: $DEST"
du -sh "$DEST" || true
echo "customer: cd $DEST && ./build_app.sh"
echo
echo "stripped check (kernel/hal/driver src should be empty):"
ls "$DEST/components/kernel/src" 2>/dev/null && echo "WARN: kernel/src still present" || echo "kernel/src: absent (ok)"
ls "$DEST/components/hal/src" 2>/dev/null && echo "WARN: components/hal/src still present" || echo "components/hal/src: absent (ok)"
ls "$DEST/components/ql-application/lv_widgets/hal/src/ic_hal_rtc.c" >/dev/null \
    && echo "lv_widgets/hal/src: kept (ok)" \
    || echo "WARN: missing ql-application/lv_widgets/hal/src/ic_hal_rtc.c"
ls "$DEST/components/ql-kernel/libs" 2>/dev/null && echo "WARN: ql-kernel/libs still present" || echo "ql-kernel/libs: absent (ok)"
