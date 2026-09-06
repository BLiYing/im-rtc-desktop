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

# 警告选项挂在 interface 目标上，**不用 add_compile_options**：后者是目录级的，
# 会连同 FetchContent 拉进来的第三方源码一起管，别人的代码用别人的标准。
add_library(imrtc_warnings INTERFACE)
if(MSVC)
  target_compile_options(imrtc_warnings INTERFACE /W4 /permissive- /utf-8)
else()
  target_compile_options(imrtc_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

option(IMRTC_BUILD_TESTS "构建单元测试与一致性向量 runner" ON)
option(IMRTC_ASAN "打开 AddressSanitizer（本机排查用，不进发布）" OFF)
# 真实的 WS Transport 是**唯一**需要第三方依赖的部分。离线机器上关掉它，
# engine 与全部测试照样能编能跑（它们用的是假 Transport）。
option(IMRTC_WITH_IX_TRANSPORT "构建基于 IXWebSocket 的真实 Transport" ON)

if(IMRTC_ASAN AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(engine)

if(IMRTC_WITH_IX_TRANSPORT)
  add_subdirectory(transport)
  # capi 是对外交付物，它带的是**能真的连上服务端**的那个 Transport。
  add_subdirectory(capi)
  add_subdirectory(tools)
endif()

if(IMRTC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
