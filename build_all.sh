#!/usr/bin/env bash
# Linux 唯一编译入口（对照 Windows 一堆 bat）。
# 用法:
#   ./build_all.sh                      # 默认产品 C56U_HM_SH8，增量 release
#   ./build_all.sh C56U_HM_SH8
#   ./build_all.sh r|new|debug C56U_HM_SH8
#   ./build_all.sh r|new EC600UCN_LB C51 VOLTE SINGLESIM
#   ./build_all.sh resgen | clean | -h
# 发布类型: debug 子命令，或 BUILD_RELEASE_TYPE=debug|release（默认 release）

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

DEF_PROJ="EC200UCN_AA"
BUILD_TARGET="8915DM_cat1_open"
START_AT="$(date '+%H:%M:%S')"

usage() {
    cat <<EOF
Usage: $0 [产品名]                    # 默认 C56U_HM_SH8，套配置后增量 release
       $0 r|new|debug [产品名]        # r=增量 new=清 out debug=清 out + debug
       $0 r|new <模组> <版本> [VOLTE] [DSIM] [EXTFLASH]
       $0 resgen | clean | h/-h

Example:
       $0
       $0 C56U_HM_SH8
       $0 r C56U_HM_SH8
       $0 new C56U_HM_SH8
       $0 debug C56U_HM_SH8
       $0 resgen
       $0 new EC600UCN_LB C51 VOLTE SINGLESIM

产品名见 projects/ ；模组见 components/ql-config/build/ 。
手表默认：模组 EC600UCN_LB，版本 C51（debug 为 C51V），VOLTE + 单卡。

官方长参数 [VOLTE] [DSIM] [EXTFLASH]，后一项依赖前一项写全。
  VOLTE    默认 NOVOLTE；要 VoLTE 写 VOLTE
  DSIM     默认 SINGLESIM；双卡写 DOUBLESIM
  EXTFLASH 默认无外挂；有则写 EXTFLASH

模组目录:
EOF
    ls -1 "$ROOT/components/ql-config/build" 2>/dev/null || true
    echo
    echo "产品目录:"
    ls -1 "$ROOT/projects" 2>/dev/null || true
}

die() {
    echo
    echo "********************        ERROR        ***********************"
    echo "$*"
    echo "****************************************************************"
    exit 1
}

add_path() {
    local p="$1"
    case ":$PATH:" in
        *":$p:"*) ;;
        *) PATH="$p:$PATH" ;;
    esac
}

# zip / Windows 拷贝后 ELF 常是 644，CMake 3.28 会报 readable but not executable。
# gcc 的 cc1/collect2 在 lib/gcc/... 下，不在 libexec。
ensure_linux_tools_exec() {
    local f d need=0
    for f in \
        "$ROOT/prebuilts/linux/bin/ninja" \
        "$ROOT/prebuilts/linux/cmake/bin/cmake" \
        "$ROOT/prebuilts/linux/gcc-arm-none-eabi/bin/arm-none-eabi-gcc"
    do
        [[ -e "$f" ]] || die "missing linux tool:
    $f"
        [[ -x "$f" ]] || need=1
    done
    for f in \
        "$ROOT"/prebuilts/linux/gcc-arm-none-eabi/lib/gcc/arm-none-eabi/*/cc1 \
        "$ROOT/prebuilts/linux/gcc-arm-none-eabi/arm-none-eabi/bin/as"
    do
        if [[ -e "$f" && ! -x "$f" ]]; then
            need=1
        fi
    done

    if [[ "$need" -eq 1 ]]; then
        echo "linux prebuilts lack +x (Windows/zip copy), chmod u+x ELF ..."
        for d in \
            "$ROOT/prebuilts/linux/bin" \
            "$ROOT/prebuilts/linux/cmake/bin" \
            "$ROOT/prebuilts/linux/gcc-arm-none-eabi/bin" \
            "$ROOT/prebuilts/linux/gcc-arm-none-eabi/lib/gcc" \
            "$ROOT/prebuilts/linux/gcc-arm-none-eabi/arm-none-eabi/bin" \
            "$ROOT/prebuilts/linux/nanopb" \
            "$ROOT/tools/linux"
        do
            [[ -d "$d" ]] || continue
            find "$d" -type f ! -perm -u+x -print0 |
                while IFS= read -r -d '' f; do
                    case "$(file -b "$f")" in
                        ELF*) chmod u+x "$f" ;;
                    esac
                done
        done
    fi

    for f in \
        "$ROOT/prebuilts/linux/bin/ninja" \
        "$ROOT/prebuilts/linux/cmake/bin/cmake" \
        "$ROOT/prebuilts/linux/gcc-arm-none-eabi/bin/arm-none-eabi-gcc"
    do
        [[ -x "$f" ]] || die "cannot execute $f
    check filesystem permission / noexec mount"
    done
}

# Windows/git 常把 .so 符号链接存成“内容为文件名”的普通文件，ld 会报 文件过短。
restore_flat_symlinks() {
    local dir="$ROOT/prebuilts/linux"
    [[ -d "$dir" ]] || return 0
    python3 - "$dir" <<'PY'
import os, sys
root = sys.argv[1]
n = 0
for dp, _, files in os.walk(root):
    for name in files:
        p = os.path.join(dp, name)
        if os.path.islink(p):
            continue
        try:
            size = os.path.getsize(p)
        except OSError:
            continue
        if size == 0 or size > 80:
            continue
        with open(p, "rb") as f:
            data = f.read()
        if b"\0" in data:
            continue
        try:
            text = data.decode("ascii").strip()
        except UnicodeDecodeError:
            continue
        if not text or any(c in text for c in "/\\ \n\t#"):
            continue
        dest = os.path.join(dp, text)
        if not os.path.isfile(dest) or os.path.islink(dest):
            continue
        os.remove(p)
        os.symlink(text, p)
        n += 1
        print("symlink", p, "->", text)
if n:
    print("restored", n, "flat symlinks")
PY
}

# nanopb 的 protoc 包装脚本若是 CRLF，LD_LIBRARY_PATH 会带 \r，插件找不到 32 位 libz。
# 不要把 nanopb 目录塞进全局 LD_LIBRARY_PATH：里面的 libz 是 i386，会搅乱 64 位工具。
fix_nanopb_protoc() {
    local proto="$ROOT/prebuilts/linux/nanopb/protoc"
    [[ -f "$proto" ]] || return 0
    if grep -q $'\r' "$proto" 2>/dev/null; then
        echo "strip CRLF: $proto"
        sed -i 's/\r$//' "$proto"
    fi
    chmod u+x "$proto" 2>/dev/null || true
}

# dtools（Qt5）要 ICU 55；Ubuntu 22/24 只有 70/74。放到 tools/linux/lib，RPATH 能找到。
ensure_icu55() {
    local dest="$ROOT/tools/linux/lib"
    if [[ -e "$dest/libicui18n.so.55" && -e "$dest/libicuuc.so.55" && -e "$dest/libicudata.so.55" ]]; then
        return 0
    fi
    echo "fetching libicu55 for dtools (Ubuntu xenial) ..."
    local tmp url
    tmp="$(mktemp -d)"
    mkdir -p "$dest"
    for url in \
        "http://archive.ubuntu.com/ubuntu/pool/main/i/icu/libicu55_55.1-7ubuntu0.5_amd64.deb" \
        "http://archive.ubuntu.com/ubuntu/pool/main/i/icu/libicu55_55.1-7_amd64.deb"
    do
        if wget -q --timeout=40 -O "$tmp/libicu55.deb" "$url"; then
            dpkg-deb -x "$tmp/libicu55.deb" "$tmp/ex"
            cp -a "$tmp/ex"/usr/lib/x86_64-linux-gnu/libicu{i18n,uc,data}.so.55* "$dest/"
            rm -rf "$tmp"
            echo "installed ICU 55 -> $dest"
            return 0
        fi
    done
    rm -rf "$tmp"
    die "cannot download libicu55 (dtools needs libicui18n.so.55)
    wget http://archive.ubuntu.com/ubuntu/pool/main/i/icu/libicu55_55.1-7ubuntu0.5_amd64.deb
    dpkg-deb -x libicu55_*.deb /tmp/icu55
    cp /tmp/icu55/usr/lib/x86_64-linux-gnu/libicu{i18n,uc,data}.so.55* $dest/"
}

setup_linux_path() {
    restore_flat_symlinks
    ensure_linux_tools_exec
    fix_nanopb_protoc
    ensure_icu55
    add_path "$ROOT/prebuilts/linux/bin"
    add_path "$ROOT/prebuilts/linux/cmake/bin"
    add_path "$ROOT/prebuilts/linux/gcc-arm-none-eabi/bin"
    add_path "$ROOT/prebuilts/linux/nanopb"
    add_path "$ROOT/tools"
    add_path "$ROOT/tools/linux"
    export PATH
    # 避免 PATH 里 100ask / 系统 ninja、cmake 抢先
    export CMAKE_MAKE_PROGRAM="$ROOT/prebuilts/linux/bin/ninja"
}

# projects/<名> 是产品叠加（feature / 表盘），不是移远模组目录。
apply_project_overlay() {
    local prj="$1" do_resgen="${2:-0}"
    local src="$ROOT/projects/$prj"
    copy_if() {
        local from="$1" to="$2"
        if [[ -e "$from" ]]; then
            mkdir -p "$(dirname "$to")"
            if [[ -d "$from" ]]; then
                rm -rf "$to"
            fi
            cp -a "$from" "$to"
            echo "copy $from -> $to"
        else
            echo "skip missing $from"
        fi
    }
    copy_if "$src/ql_target.cmake" "$ROOT/components/ql-application/ql_target.cmake"
    copy_if "$src/ql_app_feature_config.h.in" "$ROOT/components/ql-application/ql_app_feature_config.h.in"
    copy_if "$src/clockface_table.c" "$ROOT/components/ql-application/lv_widgets/clockface/clockface_table.c"
    copy_if "$src/digital1" "$ROOT/components/ql-application/lv_widgets/assets/images/clockface/digital1"
    copy_if "$src/digital2" "$ROOT/components/ql-application/lv_widgets/assets/images/clockface/digital2"
    copy_if "$src/digital3" "$ROOT/components/ql-application/lv_widgets/assets/images/clockface/digital3"
    if [[ "$do_resgen" == "1" ]]; then
        run_resgen || echo "resgen failed, continue compile"
    fi
}

run_resgen() {
    if ! command -v php >/dev/null 2>&1; then
        echo "php not found, skip image gen. install php-cli or run resgen.bat on Windows."
        return 0
    fi
    python3 "$ROOT/image_gen.py" \
        -o "$ROOT/components/ql-application/lv_widgets/assets/output/" \
        -F "$ROOT/components/ql-application/lv_widgets/assets/images/image_res.csv" \
        -c true_color_alpha \
        -f c_array
}

product_buildver() {
    if [[ "$(echo "${OUTPUT_TYPE:-${BUILD_RELEASE_TYPE:-}}" | tr '[:upper:]' '[:lower:]')" == "debug" ]]; then
        echo "C51V"
    else
        echo "C51"
    fi
}

# 无参数 = 默认手表产品增量
if [[ $# -eq 0 ]]; then
    set -- C56U_HM_SH8
fi

arg1="$(echo "${1:-}" | tr '[:upper:]' '[:lower:]')"
if [[ "$arg1" == "resgen" ]]; then
    run_resgen
    exit 0
fi
if [[ "$arg1" == "debug" ]]; then
    export BUILD_RELEASE_TYPE=debug
    _prj="${2:-C56U_HM_SH8}"
    [[ -d "$ROOT/projects/$_prj" ]] || die "project not found:
    $ROOT/projects/$_prj"
    apply_project_overlay "$_prj" 1
    set -- new EC600UCN_LB "$(product_buildver)" VOLTE SINGLESIM
fi

# ./build_all.sh C56U_HM_SH8
# ./build_all.sh r|new C56U_HM_SH8
if [[ -n "${1:-}" && -d "$ROOT/projects/$1" ]]; then
    apply_project_overlay "$1" 0
    set -- r EC600UCN_LB "$(product_buildver)" VOLTE SINGLESIM
elif [[ -n "${1:-}" && -n "${2:-}" && -z "${3:-}" && -d "$ROOT/projects/$2" ]]; then
    local_cmd="$(echo "$1" | tr '[:upper:]' '[:lower:]')"
    case "$local_cmd" in
        r|n|new)
            apply_project_overlay "$2" "$([[ "$local_cmd" == "r" ]] && echo 0 || echo 1)"
            set -- "$1" EC600UCN_LB "$(product_buildver)" VOLTE SINGLESIM
            ;;
    esac
fi

cmd="${1:-r}"
cmd="$(echo "$cmd" | tr '[:upper:]' '[:lower:]')"

case "$cmd" in
    h|-h|help|\?)
        usage
        exit 0
        ;;
    c|clean)
        echo "cleaning..."
        rm -rf "$ROOT/out"
        echo "cleaning done"
        echo "START TIME:  $START_AT"
        echo "END TIME:    $(date '+%H:%M:%S')"
        exit 0
        ;;
    r|n|new) ;;
    *)
        echo "!!!unknown build type: $1, should be r/new/debug/resgen/clean/h 或产品名!!!"
        usage
        exit 1
        ;;
esac

if [[ "$cmd" == "n" ]]; then
    cmd="new"
fi

ql_buildproj="${2:-$DEF_PROJ}"
buildver="${3:-}"
if [[ -z "$buildver" ]]; then
    echo "we need your version label..."
    usage
    exit 1
fi

volte_enable="${4:-NOVOLTE}"
quec_dsim="${5:-SINGLESIM}"
quec_ext_flash="${6:-}"

if [[ "$(echo "${OUTPUT_TYPE:-${BUILD_RELEASE_TYPE:-}}" | tr '[:upper:]' '[:lower:]')" == "debug" ]]; then
    BUILD_RELEASE_TYPE="debug"
else
    BUILD_RELEASE_TYPE="release"
fi

export _ccsdk_build=ON
export _OPEN_=OPEN_CPU
export PROJECT_ROOT="$ROOT"
export BUILD_TARGET
export BUILD_RELEASE_TYPE
export ql_buildproj
export ql_app_ver="${buildver}_APP"

cfg_dir="$ROOT/components/ql-config/build/${ql_buildproj}/${BUILD_TARGET}"
if [[ ! -d "$cfg_dir" ]]; then
    die "your target.config is not exist:
    $cfg_dir"
fi

export KCONFIG_CONFIG="$cfg_dir/target.config"
target_out_dir="out/${BUILD_TARGET}_${BUILD_RELEASE_TYPE}"
export PROJECT_OUT="$ROOT/$target_out_dir"

setup_linux_path

if [[ "$cmd" == "new" ]]; then
    echo "cleaning..."
    rm -rf "$ROOT/out"
    echo "cleaning done"
fi

ql_extflash="n"
ext_suffix=""
if [[ "$(echo "$quec_ext_flash" | tr '[:upper:]' '[:lower:]')" == "extflash" ]]; then
    ql_extflash="y"
    ext_suffix="_extflash"
    quec_ext_flash="EXTFLASH"
else
    quec_ext_flash=""
fi
export ql_extflash

export ql_dsim_cfg=n
modemdir="cat1_UIS8915DM_BB_RF_SS_cus"
partitionfile="components/hal/config/8910/partinfo_8910_8m_opencpu${ext_suffix}.json"

if [[ "$(echo "$quec_dsim" | tr '[:upper:]' '[:lower:]')" == "doublesim" ]]; then
    quec_dsim="DOUBLESIM"
    export ql_dsim_cfg=y
    modemdir="cat1_UIS8915DM_BB_RF_DS_cus"
    partitionfile="components/hal/config/8910/partinfo_8910_8m_opencpu_ds${ext_suffix}.json"
else
    quec_dsim="SINGLESIM"
fi

export prepack_json_path="components/ql-config/download/prepack/ql_prepack.json"
export ap_ram_offset=0xC00000
export ap_ram_size=0x400000
export ims_delta_nv=y
export quec_ims_feature=y

if [[ "$(echo "$volte_enable" | tr '[:upper:]' '[:lower:]')" != "volte" ]]; then
    volte_enable="NOVOLTE"
    export quec_ims_feature=n
    export ap_ram_offset=0x980000
    export ap_ram_size=0x680000
    export ims_delta_nv=n
    modemdir="cat1_UIS8915DM_BB_RF_SS_NoVolte_cus"
    partitionfile="components/hal/config/8910/partinfo_8910_8m_opencpu_novolte${ext_suffix}.json"
    if [[ "$quec_dsim" == "DOUBLESIM" ]]; then
        modemdir="cat1_UIS8915DM_BB_RF_DS_NoVolte_cus"
    fi
else
    volte_enable="VOLTE"
fi

export partitionfile
export modemdir

nv_src="$ROOT/components/ql-config/build/${ql_buildproj}/nvitem"
nv_dst="$ROOT/prebuilts/modem/8910/${modemdir}/nvitem"
mkdir -p "$nv_dst"
rm -f "$nv_dst"/*.prj
if [[ -d "$nv_src" ]]; then
    shopt -s nullglob
    cp -f "$nv_src"/*.nvm "$nv_dst/" 2>/dev/null || true
    shopt -u nullglob
    if [[ "$volte_enable" == "NOVOLTE" ]]; then
        if [[ "$quec_dsim" == "DOUBLESIM" ]]; then
            cp -f "$nv_src/nvitem_modem_novolte_ds.prj" "$nv_dst/nvitem_modem.prj"
        else
            cp -f "$nv_src/nvitem_modem_novolte.prj" "$nv_dst/nvitem_modem.prj"
        fi
    else
        if [[ "$quec_dsim" == "DOUBLESIM" ]]; then
            cp -f "$nv_src/nvitem_modem_ds.prj" "$nv_dst/nvitem_modem.prj"
        else
            cp -f "$nv_src/nvitem_modem.prj" "$nv_dst/nvitem_modem.prj"
        fi
    fi
fi

mkdir -p "$PROJECT_OUT/include"
cd "$PROJECT_OUT"

echo
echo "PATH=$PATH"
echo "which cmake=$(command -v cmake)"
echo "which ninja=$(command -v ninja)"
echo "which arm-none-eabi-gcc=$(command -v arm-none-eabi-gcc)"
echo

cmake ../.. -G Ninja
echo
ninja

echo
echo "********************        PASS         ***********************"
echo "**************   build ended successfully   ********************"
echo "********************        PASS         ***********************"
echo
echo "${ql_buildproj} ${buildver} ${BUILD_RELEASE_TYPE} ${volte_enable} ${quec_dsim}"

version_path="$ROOT/target/${buildver}"
rm -rf "$version_path"
mkdir -p "$version_path/prepack" "$version_path/app"

if [[ -d hex ]]; then
    cp -a hex/. "$version_path/prepack/"
fi
if [[ -f "$version_path/prepack/${BUILD_TARGET}.elf" ]]; then
    mv -f "$version_path/prepack/${BUILD_TARGET}.elf" "$version_path/"
fi
if [[ -f "$version_path/prepack/${BUILD_TARGET}.map" ]]; then
    mv -f "$version_path/prepack/${BUILD_TARGET}.map" "$version_path/"
fi
shopt -s nullglob
for pac in "$version_path/prepack/${BUILD_TARGET}"*"${BUILD_RELEASE_TYPE}".pac; do
    mv -f "$pac" "$version_path/${BUILD_TARGET}_${buildver}.pac"
    break
done
shopt -u nullglob
if [[ -f target.cmake ]]; then
    cp -f target.cmake "$version_path/prepack/"
fi
if [[ -d hex/examples ]]; then
    cp -a hex/examples/. "$version_path/app/" || true
fi

ql_prepack_opt="N"
if [[ -f ql_prepack.opt ]]; then
    ql_prepack_opt="Y"
fi

dtools_bin="$ROOT/tools/linux/dtools"
pac_out="$version_path/${BUILD_TARGET}_${buildver}.pac"
app_pac="$version_path/app/${ql_app_ver}.pac"
merge_pac="$version_path/${BUILD_TARGET}_${buildver}_merge.pac"

if [[ -x "$dtools_bin" && -f "$pac_out" && -f "$app_pac" ]]; then
    if [[ "$ql_prepack_opt" == "Y" ]]; then
        "$dtools_bin" pacmerge --id APPIMG,PS --id PREPACK,NV "$pac_out" "$app_pac" "$merge_pac" \
            || echo "pacmerge failed (often missing libicu55). firmware pac: $pac_out"
    else
        "$dtools_bin" pacmerge --id APPIMG,PS "$pac_out" "$app_pac" "$merge_pac" \
            || echo "pacmerge failed (often missing libicu55). firmware pac: $pac_out"
    fi
else
    echo "skip pacmerge (missing dtools or pac): dtools=$dtools_bin pac=$pac_out app=$app_pac"
fi

if [[ -f "$ROOT/tools/codesize.py" && -f "hex/${BUILD_TARGET}.map" ]]; then
    python3 "$ROOT/tools/codesize.py" --map "hex/${BUILD_TARGET}.map" || true
    for f in outlib.csv outobj.csv outsect.csv; do
        if [[ -f "$f" ]]; then
            mv -f "$f" "$version_path/prepack/"
        fi
    done
fi

cd "$ROOT"
echo
echo "START TIME:  $START_AT"
echo "END TIME:    $(date '+%H:%M:%S')"
echo "output:      $version_path"
