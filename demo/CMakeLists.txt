# demo —— Qt 6 参考实现。
#
# 三条不许破的规矩：
#   1. **经 C ABI 调引擎**（链 imrtc::capi 这个动态库，不是内部 C++ 接口）。
#      走内部接口会掩盖全部 ABI 问题，那样「Demo 跑通」不等于「宿主接得通」。
#      联调工具 tools/Smoke.cpp 已经是这么走的，本目标走同一条路。
#   2. 默认**不构建**（IMRTC_BUILD_DEMO=OFF）。engine 与全部测试必须在一台
#      只装了 CMake + 编译器的机器上就能跑——不能因为 Demo 就把 Qt 变成必需品。
#   3. **Qt 只出现在这个目录里**。engine/ 里任何 Q 开头的类型都是直接打回。
#
# 装 Qt（macOS，最小集，约 1.8 GB）：
#   pip install aqtinstall
#   aqt install-qt mac desktop 6.8.3 clang_64 \
#       --archives qtbase qtsvg qttranslations qttools -O ~/Qt
# 然后：
#   cmake --preset macos-clang -DIMRTC_BUILD_DEMO=ON -DCMAKE_PREFIX_PATH=$HOME/Qt/6.8.3/macos

# ---- macOS 26 SDK 移除了 AGL.framework ----
#
# Qt 6.8 的 FindWrapOpenGL.cmake 在 find_library 失败时会**无条件回退到硬编码的
# `-framework AGL`**，于是链接阶段直接 `ld: framework 'AGL' not found`。
#
# **不能用 find_library 探测 AGL**：它还留在**运行系统**的 /System/Library/Frameworks 下，
# 只是从 **SDK** 里删掉了，而 ld 只看 SDK——find_library 会兴高采烈地报「找到了」。
# 所以直接查 SDK 目录，缺了就预填 Qt 那个 cache 变量，让它拿到一个能链接的值。
# 必须在 find_package(Qt6) 之前。集成方用 Xcode 26 + Qt 6.8 会撞同一个坑。
if(APPLE AND NOT DEFINED CACHE{WrapOpenGL_AGL})
  set(_imrtc_sdk "${CMAKE_OSX_SYSROOT}")
  if(NOT _imrtc_sdk)
    execute_process(COMMAND xcrun --show-sdk-path
                    OUTPUT_VARIABLE _imrtc_sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
  endif()
  if(NOT EXISTS "${_imrtc_sdk}/System/Library/Frameworks/AGL.framework")
    set(WrapOpenGL_AGL "${_imrtc_sdk}/System/Library/Frameworks/OpenGL.framework"
        CACHE FILEPATH "AGL shim：macOS 26 SDK 已移除 AGL，指向 OpenGL（重复链接无害）")
    message(STATUS "AGL 不在 SDK 里，已为 Qt 6.8 打 shim：${WrapOpenGL_AGL}")
  endif()
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network Svg LinguistTools)

qt_standard_project_setup(I18N_TRANSLATED_LANGUAGES en)

# 界面代码做成一个静态库，给两个可执行文件共用：
#   imrtc_demo        真 Demo（要服务端）
#   imrtc_demo_shots  离线截图工具（不要服务端，见 Shots.cpp）
qt_add_library(imrtc_demo_ui STATIC
  Theme.cpp
  Icons.cpp
  Avatar.cpp
  ControlButton.cpp
  VideoTile.cpp
  CallOverlay.cpp
  Language.cpp
  EngineBridge.cpp
  CallHistory.cpp
  CallStrings.cpp
  LoginPage.cpp
  DialPage.cpp
  HistoryPage.cpp
  SettingsPage.cpp
  MainWindow.cpp
)
target_include_directories(imrtc_demo_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

qt_add_executable(imrtc_demo main.cpp)
qt_add_executable(imrtc_demo_shots Shots.cpp)

# imrtc::capi 是**动态库**。宿主拿到的就是这个 .dylib / .dll + 一个 C 头，
# Demo 走同一条路才算证明了交付面够用。
target_link_libraries(imrtc_demo_ui PUBLIC
  imrtc::capi
  Qt6::Widgets
  Qt6::Network
  Qt6::Svg
)
target_link_libraries(imrtc_demo_ui PRIVATE imrtc_warnings)
target_link_libraries(imrtc_demo PRIVATE imrtc_demo_ui imrtc_warnings)
target_link_libraries(imrtc_demo_shots PRIVATE imrtc_demo_ui imrtc_warnings)

# 翻译：源语言是中文（tr() 里写中文），en 那份由 i18n/imrtc_demo_en.ts 提供。
# `lupdate` 提词用 `cmake --build … --target update_translations`。
# **两个可执行文件都要挂**：只挂 imrtc_demo 的话，截图工具跑起来找不到 :/i18n
# 里的 .qm，会静默退回中文——而那正是最需要验证语言切换的地方。
qt_add_translations(
  TARGETS imrtc_demo imrtc_demo_shots
  TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/i18n/imrtc_demo_en.ts
  RESOURCE_PREFIX "/i18n"
)

# **部署目标：Demo 是 12.0，引擎仍是 11.0。**
# Qt 6.8 的官方 macOS 包是按 macOS 12 编的，拿 11.0 去链会一路 ld warning。
# 但这**不降低我们自己的交付面**——engine 与 capi 那两个目标照旧 11.0，
# 也就是说集成方的 App 仍可以支持 macOS 11，只是「用 Qt 6.8 的宿主」得 12+。
# 这是 Qt 的约束，不是我们的。
foreach(_t imrtc_demo_ui imrtc_demo imrtc_demo_shots)
  target_compile_options(${_t} PRIVATE -mmacosx-version-min=12.0)
  target_link_options(${_t} PRIVATE -mmacosx-version-min=12.0)
endforeach()

set_target_properties(imrtc_demo PROPERTIES
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_BUNDLE_NAME "im-rtc Demo"
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.imrtc.demo"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
)
