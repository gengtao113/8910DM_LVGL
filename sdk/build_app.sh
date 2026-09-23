#!/usr/bin/env bash
# 客户 SDK：只编 APPIMG，烧到 APPIMG 分区。不出整机包。
# 用法:
#   ./build_app.sh              # 增量
#   ./build_app.sh new          # 清 out 后编
#   ./build_app.sh clean

set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

[[ -f "$ROOT/sdk_prebuilt/core_stub.o" ]] || {
    echo "not a customer SDK (missing sdk_prebuilt/core_stub.o)"
    echo "internal tree: use ./build_all.sh then ./pack_opencpu_sdk.sh"
    exit 1
}

export BUILD_TARGET="${BUILD_TARGET:-8915DM_cat1_open}"
export BUILD_RELEASE_TYPE="${BUILD_RELEASE_TYPE:-release}"
export ql_app_ver="${ql_app_ver:-C51_APP}"
export _ccsdk_build=ON
export PROJECT_ROOT="$ROOT"

add_path() {
    case ":$PATH:" in
        *":$1:"*) ;;
        *) PATH="$1:$PATH" ;;
    esac
}
add_path "$ROOT/prebuilts/linux/bin"
add_path "$ROOT/prebuilts/linux/cmake/bin"
add_path "$ROOT/prebuilts/linux/gcc-arm-none-eabi/bin"
add_path "$ROOT/tools"
add_path "$ROOT/tools/linux"

CMAKE_BIN="$ROOT/prebuilts/linux/cmake/bin/cmake"
NINJA_BIN="$ROOT/prebuilts/linux/bin/ninja"
[[ -x "$CMAKE_BIN" ]] || CMAKE_BIN=cmake
[[ -x "$NINJA_BIN" ]] || NINJA_BIN=ninja

cmd="${1:-r}"
out="out/${BUILD_TARGET}_${BUILD_RELEASE_TYPE}"

case "$cmd" in
    clean)
        rm -rf "$ROOT/out" "$ROOT/target"
        echo "cleaned"
        exit 0
        ;;
    new)
        rm -rf "$ROOT/out"
        ;;
    r|""|*)
        ;;
esac

if [[ -d "$ROOT/projects/C56U_HM_SH8" ]]; then
    cp -f "$ROOT/projects/C56U_HM_SH8/ql_target.cmake" "$ROOT/components/ql-application/" 2>/dev/null || true
    cp -f "$ROOT/projects/C56U_HM_SH8/ql_app_feature_config.h.in" "$ROOT/components/ql-application/" 2>/dev/null || true
fi

mkdir -p "$out"
if [[ ! -f "$out/build.ninja" ]]; then
    "$CMAKE_BIN" -S "$ROOT" -B "$out" -G Ninja
fi
"$NINJA_BIN" -C "$out"

dest="target/C51/app"
mkdir -p "$dest"
# add_appimg_flash_ql_example 把产物放在 hex/examples
shopt -s nullglob
for f in "$out"/hex/examples/C51_APP.* "$out"/hex/examples/"${ql_app_ver}".*; do
    [[ -f "$f" ]] && cp -f "$f" "$dest/"
done
rm -f target/C51/*_merge.pac 2>/dev/null || true

echo
echo "APPIMG: $dest"
echo "QFlash 只下应用分区: $dest/*_APP.pac"
echo "整机底包由方案烧，客户不要 merge。"
