# im-rtc-desktop —— C++17 Engine（零 Qt 依赖）
#
# 本期（P5 第一、二刀）只有纯逻辑：JSON、信封、帧编解码、通话与房间状态机。
# **没有任何第三方依赖**——媒体（libwebrtc）与 Qt Demo 在后面的刀里加。
# 于是这份工程在一台只装了 CMake + 编译器的机器上就能配置、编译、跑测试。
cmake_minimum_required(VERSION 3.21)

project(im_rtc_desktop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 对外只导出 imrtc_v1_*（CONVENTIONS §2）。现在还没有 capi/，但符号可见性
# 从第一天就默认收紧——等 capi 落地时不必回头改所有目标。
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

option(IMRTC_BUILD_TESTS "构建单元测试与一致性向量 runner" ON)
option(IMRTC_ASAN "打开 AddressSanitizer（本机排查用，不进发布）" OFF)

if(IMRTC_ASAN AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(engine)

if(IMRTC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
