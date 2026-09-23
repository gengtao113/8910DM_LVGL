# 客户 SDK：只编 APPIMG。由 pack_opencpu_sdk.sh 拷成客户树的 CMakeLists.txt。
# 不 add_subdirectory kernel/hal/driver 源码。

cmake_minimum_required(VERSION 3.13)

set(SOURCE_TOP_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(BINARY_TOP_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_C_COMPILER_FORCED 1)
set(CMAKE_CXX_COMPILER_FORCED 1)
set(WITH_WERROR OFF)

set(SDK_PREBUILT ${SOURCE_TOP_DIR}/sdk_prebuilt)
if(NOT EXISTS "${SDK_PREBUILT}/core_stub.o")
    message(FATAL_ERROR "sdk_prebuilt/core_stub.o missing. Use the packaged SDK, not the full tree.")
endif()
if(NOT EXISTS "${SDK_PREBUILT}/target.cmake")
    message(FATAL_ERROR "sdk_prebuilt/target.cmake missing.")
endif()
if(NOT EXISTS "${SOURCE_TOP_DIR}/cmake/extension.cmake")
    message(FATAL_ERROR "cmake/extension.cmake missing (pack_opencpu_sdk.sh copied cmake/ into the wrong place).")
endif()

if(NOT BUILD_TARGET)
    set(BUILD_TARGET $ENV{BUILD_TARGET})
endif()
if(NOT BUILD_RELEASE_TYPE)
    set(BUILD_RELEASE_TYPE $ENV{BUILD_RELEASE_TYPE})
endif()
if(NOT BUILD_TARGET)
    set(BUILD_TARGET 8915DM_cat1_open)
endif()
if(NOT BUILD_RELEASE_TYPE)
    set(BUILD_RELEASE_TYPE release)
endif()

# chip.cmake 会展开 pac_fdl_files=${out_hex_dir}/fdl*.sign.img，必须先设目录
set(out_hex_dir ${BINARY_TOP_DIR}/hex)
set(out_hex_example_dir ${BINARY_TOP_DIR}/hex/examples)
set(out_hex_unittest_dir ${BINARY_TOP_DIR}/hex/unittests)
set(out_lib_dir ${BINARY_TOP_DIR}/lib)
set(out_app_lib_dir ${BINARY_TOP_DIR}/lib)
set(out_inc_dir ${BINARY_TOP_DIR}/include)
set(out_rpc_dir ${BINARY_TOP_DIR}/rpcgen)
set(tools_dir ${SOURCE_TOP_DIR}/tools)
set(ql_app_dir ${SOURCE_TOP_DIR}/components/ql-application)
set(core_stub_o ${SDK_PREBUILT}/core_stub.o)
set(pacgen_py ${tools_dir}/pacgen.py)
set(modemgen_py ${tools_dir}/modemgen.py)
set(cmd_mkappimg dtools mkappimg)
set(dummy_c_file ${SOURCE_TOP_DIR}/cmake/dummy.c)
set(dummy_cxx_file ${SOURCE_TOP_DIR}/cmake/dummy.cpp)

file(MAKE_DIRECTORY ${out_hex_dir} ${out_inc_dir} ${out_lib_dir})
file(COPY ${SDK_PREBUILT}/include/ DESTINATION ${out_inc_dir})
file(COPY ${SDK_PREBUILT}/fdl1.sign.img DESTINATION ${out_hex_dir})
file(COPY ${SDK_PREBUILT}/fdl2.sign.img DESTINATION ${out_hex_dir})

# 先读配置和交叉编译器，再 project()，否则会落到 /usr/bin/cc
include(${SOURCE_TOP_DIR}/components/ql-application/ql_target.cmake)
include(${SDK_PREBUILT}/target.cmake)
include(${SDK_PREBUILT}/partinfo.cmake)
include(${SOURCE_TOP_DIR}/components/chip/chip.cmake)
include(${SOURCE_TOP_DIR}/cmake/toolchain-gcc.cmake)
project(${BUILD_TARGET} C CXX ASM)

include(${SOURCE_TOP_DIR}/cmake/extension.cmake)

define_property(GLOBAL PROPERTY app_libraries
    BRIEF_DOCS "app libraries"
    FULL_DOCS "app libraries"
)

set(QL_CCSDK_BUILD ON)
set(QL_SDK_APP_ONLY ON)
set(QL_APP_BUILD_VER $ENV{ql_app_ver})
if(NOT QL_APP_BUILD_VER)
    set(QL_APP_BUILD_VER C51_APP)
endif()

include(${SOURCE_TOP_DIR}/components/ql-application/ql_app_feature_config.cmake)

# 顶掉应用 CMake 里对 kernel/hal/driver 的链接，只提供头文件
add_library(kernel INTERFACE)
target_include_directories(kernel INTERFACE
    ${SOURCE_TOP_DIR}/components/kernel/include
    ${out_inc_dir}
)
add_library(hal INTERFACE)
target_include_directories(hal INTERFACE
    ${SOURCE_TOP_DIR}/components/hal/include
    ${out_inc_dir}
)
add_library(driver INTERFACE)
target_include_directories(driver INTERFACE
    ${SOURCE_TOP_DIR}/components/driver/include
    ${out_inc_dir}
)

include_directories(${out_inc_dir})
include_directories(${SOURCE_TOP_DIR}/components/newlib/include)
include_directories(${SOURCE_TOP_DIR}/components/hal/include)
include_directories(${SOURCE_TOP_DIR}/components/fs/include)
include_directories(${SOURCE_TOP_DIR}/components/fs/fsmount/include)
include_directories(${SOURCE_TOP_DIR}/components/cfw/include)
include_directories(${SOURCE_TOP_DIR}/components/net/lwip/include)
include_directories(${SOURCE_TOP_DIR}/components/net/lwip/src/include)
include_directories(${SOURCE_TOP_DIR}/components/net/mbedtls/include)
include_directories(${SOURCE_TOP_DIR}/components/net/include)
include_directories(${SOURCE_TOP_DIR}/components/kernel/include)
include_directories(${SOURCE_TOP_DIR}/components/ql-kernel/inc)

add_custom_target(ql_examples ALL)
add_custom_target(beautify)
add_custom_target(unittests)
add_definitions(-DPLATFORM_EC600)

add_subdirectory(${ql_app_dir})
