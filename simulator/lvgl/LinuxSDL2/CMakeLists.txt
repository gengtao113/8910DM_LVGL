cmake_minimum_required(VERSION 3.16)

project(watch_sim LANGUAGES C)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
set(SIM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../LVGL.Simulator")
set(WIDGETS_DIR "${REPO_ROOT}/components/ql-application/lv_widgets")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

# 窗口、刷屏步长、LVGL 分辨率必须同一套数字，否则会花屏。
# 与 Windows LVGL.Simulator / lv_conf.h 一致：240。不要用板上的 128，否则菜单/指针盘会被裁。
set(SIM_HOR_RES 240)
set(SIM_VER_RES 240)
set(SIM_ZOOM 2)

add_compile_definitions(
    LV_HOR_RES_MAX=${SIM_HOR_RES}
    LV_VER_RES_MAX=${SIM_VER_RES}
    USE_MONITOR=1
    USE_MOUSE=1
    USE_KEYBOARD=1
    USE_MOUSEWHEEL=1
    USE_WIN32DRV=0
    USE_GTK=0
    USE_EVDEV=0
    USE_LIBINPUT=0
    MONITOR_ZOOM=${SIM_ZOOM}
    MONITOR_HOR_RES=${SIM_HOR_RES}
    MONITOR_VER_RES=${SIM_VER_RES}
)

file(GLOB_RECURSE LVGL_SRCS CONFIGURE_DEPENDS "${SIM_DIR}/lvgl/src/*.c")

set(LV_DRIVERS_SRCS
    "${SIM_DIR}/lv_drivers/display/monitor.c"
    "${SIM_DIR}/lv_drivers/indev/mouse.c"
    "${SIM_DIR}/lv_drivers/indev/keyboard.c"
    "${SIM_DIR}/lv_drivers/indev/mousewheel.c"
)

file(GLOB_RECURSE WIDGET_SRCS CONFIGURE_DEPENDS "${WIDGETS_DIR}/*.c")
list(FILTER WIDGET_SRCS EXCLUDE REGEX "ic_hal_rtc\\.c$")
list(FILTER WIDGET_SRCS EXCLUDE REGEX "mainmenu_iwatch\\.c$")
list(FILTER WIDGET_SRCS EXCLUDE REGEX "factory_sensor\\.c$")

add_executable(watch_sim
    main.c
    sim_stubs.c
    ${LVGL_SRCS}
    ${LV_DRIVERS_SRCS}
    ${WIDGET_SRCS}
)

target_include_directories(watch_sim PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
    "${SIM_DIR}"
    "${WIDGETS_DIR}"
    "${WIDGETS_DIR}/i18n"
    "${WIDGETS_DIR}/hal/inc"
    "${WIDGETS_DIR}/assets/output"
    "${WIDGETS_DIR}/config"
    "${WIDGETS_DIR}/fonts"
    "${WIDGETS_DIR}/image_resource"
    "${WIDGETS_DIR}/mainmenu/inc"
    "${WIDGETS_DIR}/alarm_clock/inc"
    "${WIDGETS_DIR}/clockface"
    "${REPO_ROOT}/components/ql-application/function/call"
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

target_include_directories(watch_sim PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(watch_sim PRIVATE ${SDL2_LIBRARIES} m Threads::Threads)

target_compile_options(watch_sim PRIVATE -Wno-unused-variable -Wno-unused-function)
