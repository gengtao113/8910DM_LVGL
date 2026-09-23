#!/usr/bin/env bash
# 仓库根目录：Ubuntu 上用 SDL2 跑手表 LVGL 模拟器（对照 X-TRACK LinuxSDL2）。
#   ./build_lvgl_sim.sh        编译
#   ./build_lvgl_sim.sh run    编译后运行
#   ./build_lvgl_sim.sh clean
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="${ROOT}/simulator/lvgl/LinuxSDL2"
OUT_DIR="${SRC_DIR}/build"
CMD="${1:-build}"

usage() {
    echo "usage: $0 [build|run|clean]"
}

if [[ ! -f "${SRC_DIR}/CMakeLists.txt" ]]; then
    echo "error: missing ${SRC_DIR}/CMakeLists.txt" >&2
    exit 1
fi

cmake_build() {
    cmake -S "${SRC_DIR}" -B "${OUT_DIR}"
    cmake --build "${OUT_DIR}" --parallel "$(nproc)"
}

case "${CMD}" in
    build|"")
        cmake_build
        echo "ok: ${SRC_DIR}/watch_sim"
        echo "run:  $0 run"
        ;;
    run)
        cmake_build
        cd "${SRC_DIR}"
        exec ./watch_sim
        ;;
    clean)
        rm -rf "${OUT_DIR}"
        rm -f "${SRC_DIR}/watch_sim"
        echo "cleaned"
        ;;
    -h|--help)
        usage
        ;;
    *)
        usage
        exit 1
        ;;
esac
