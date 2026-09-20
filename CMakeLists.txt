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

# 版本号唯一来源：engine/include/imrtc/Version.h 的 kSdkVersion
# （C ABI 的 imrtc_v1_version() 与各 Options 的 sdk 默认串都从那取）。
# 这里用 file(STRINGS) 抠出同一个值给 install() 用的 imrtcConfigVersion.cmake，
# 免得第三处又写死版本号、迟早对不上。
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/engine/include/imrtc/Version.h" _imrtc_version_line
     REGEX "kSdkVersion\\[\\] = \"[0-9]+\\.[0-9]+\\.[0-9]+\"")
string(REGEX REPLACE ".*\"([0-9]+\\.[0-9]+\\.[0-9]+)\".*" "\\1" IMRTC_VERSION "${_imrtc_version_line}")
if(NOT IMRTC_VERSION)
  message(FATAL_ERROR "解析不到 SDK 版本号：engine/include/imrtc/Version.h 的 kSdkVersion")
endif()
message(STATUS "IMRTC_VERSION = ${IMRTC_VERSION}（唯一来源：engine/include/imrtc/Version.h）")

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
# Qt Demo **默认不构建**：engine 与全部测试必须在一台只装了 CMake + 编译器的
# 机器上就能跑通，不能因为一个 Demo 就把 Qt 变成必需品。
#   cmake --preset macos-clang -DIMRTC_BUILD_DEMO=ON -DCMAKE_PREFIX_PATH=$ENV{HOME}/Qt/6.8.3/macos
option(IMRTC_BUILD_DEMO "构建 Qt 6 Demo（需要 Qt 6）" OFF)

# Demo 的三档 SDK 开关（本地包 / 公网包共用这一个变量，`scripts/demo.sh` 按环境变量 IMRTC_SDK
# 挑目录传进来）。**设了它就整个切轨**：不再 add_subdirectory engine/transport/capi/tools，
# 也不构建引擎测试，改成 find_package(imrtc) 找一份已经打包好的 SDK——
# 这与第三方集成时的链接方式完全一样，Demo 借此把「发布包够不够用」也验一遍。
set(IMRTC_SDK_DIR "" CACHE PATH
    "设了就不编译引擎/capi 源码，Demo 改用 find_package(imrtc) 从这个目录找发布包（scripts/package.sh 的产物）")

if(IMRTC_ASAN AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

if(IMRTC_SDK_DIR)
  if(NOT IMRTC_BUILD_DEMO)
    message(FATAL_ERROR "IMRTC_SDK_DIR 只改 Demo 的链接方式，请同时加 -DIMRTC_BUILD_DEMO=ON")
  endif()
  message(STATUS "Demo 用的 SDK：发布包 ${IMRTC_SDK_DIR}")
  # 两档版本号相同，光看版本分不出源码档/发布包档——真正能分辨的只有这行 STATUS。
  find_package(imrtc ${IMRTC_VERSION} CONFIG REQUIRED PATHS "${IMRTC_SDK_DIR}" NO_DEFAULT_PATH)
  add_subdirectory(demo)
else()
  message(STATUS "Demo 用的 SDK：源码")
  add_subdirectory(engine)

  if(IMRTC_WITH_IX_TRANSPORT)
    add_subdirectory(transport)
    # capi 是对外交付物，它带的是**能真的连上服务端**的那个 Transport。
    add_subdirectory(capi)
    add_subdirectory(tools)
    if(IMRTC_BUILD_DEMO)
      add_subdirectory(demo)
    endif()
  elseif(IMRTC_BUILD_DEMO)
    message(FATAL_ERROR "Demo 要经 C ABI 调引擎，所以需要 IMRTC_WITH_IX_TRANSPORT=ON")
  endif()

  if(IMRTC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
  endif()
endif()
