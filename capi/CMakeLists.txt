# im_rtc_engine_capi —— **对外唯一的交付物**。
#
# 出两个目标：
#   - im_rtc_engine_capi        动态库（.dylib / .dll），宿主拿的就是它 + 一个 C 头
#   - im_rtc_engine_capi_static 静态库，只给本仓的测试与 Demo 用，省一次装载
#
# 符号可见性在顶层设成 hidden，头文件里的 IMRTC_API 把 imrtc_v1_* 这一族放出来——
# 别的什么都不导出（CONVENTIONS §2 红线 1）。
set(IMRTC_CAPI_SOURCES
  src/imrtc_c.cpp
  src/CApiConvert.cpp
  src/CObserver.cpp
  src/CDebugToken.cpp
)

add_library(im_rtc_engine_capi SHARED ${IMRTC_CAPI_SOURCES})
add_library(im_rtc_engine_capi_static STATIC ${IMRTC_CAPI_SOURCES})

foreach(target im_rtc_engine_capi im_rtc_engine_capi_static)
  # BUILD_INTERFACE/INSTALL_INTERFACE：静态库那份不 install()，写绝对路径就够；
  # 动态库那份要进 install(EXPORT)，装出来的 targets 文件不能再指向本机源码目录，
  # 得指向相对 install 前缀的 include/（configure_package_config_file 会把
  # ${_IMPORT_PREFIX} 接到这里）。两个目标共用同一个 foreach，用 generator
  # expression 让静态库那份自然吃到 BUILD_INTERFACE 分支，互不影响。
  target_include_directories(${target} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
  # transport_ix 是 PRIVATE：宿主不需要知道我们用了哪个 WS 库，也不该被它的头污染。
  target_link_libraries(${target} PRIVATE imrtc::transport_ix imrtc_warnings)
  set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endforeach()

# 只有动态库那份需要 dllexport；静态库那份用 dllimport 会链不上。
target_compile_definitions(im_rtc_engine_capi PRIVATE IMRTC_BUILDING_SHARED)

# -fvisibility=hidden 挡不住 libc++ 的 RTTI（weak-def），所以再上一份白名单。
# 见 exported_symbols.txt 的注释。守门的是 scripts/check-abi.sh。
if(APPLE)
  target_link_options(im_rtc_engine_capi PRIVATE
    "-Wl,-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/exported_symbols.txt")
  set_target_properties(im_rtc_engine_capi PROPERTIES
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/exported_symbols.txt)
endif()
target_compile_definitions(im_rtc_engine_capi_static PUBLIC IMRTC_API=)

add_library(imrtc::capi ALIAS im_rtc_engine_capi)
add_library(imrtc::capi_static ALIAS im_rtc_engine_capi_static)

# ── 交付物：只装动态库 ──────────────────────────────────────────────
#
# 静态库那份（im_rtc_engine_capi_static）只给本仓测试省一次装载用，不对外交付
# （CONVENTIONS §2：C ABI 边界只有「动态库 + 一个 C 头」）。EXPORT_NAME 让装出来的
# targets 文件里导出目标名是 `imrtc::capi`——与源码档里 Demo 链的别名同名，
# 于是 demo/CMakeLists.txt 的 target_link_libraries(... imrtc::capi ...) 不用分两档写。
#
# install_name 用 @rpath/<soname>：即便不显式设置，CMake 在 APPLE 上默认就是这个值
# （CMAKE_MACOSX_RPATH 默认 ON），这里写出来是为了不依赖这条隐式默认——装完的库离开了
# 构建目录，任何人读这份 CMakeLists 都能看出「宿主要自己布 rpath」这件事，不用去翻 CMake 文档。
set_target_properties(im_rtc_engine_capi PROPERTIES
  EXPORT_NAME capi
  MACOSX_RPATH ON
  INSTALL_NAME_DIR "@rpath")

install(TARGETS im_rtc_engine_capi
  EXPORT imrtcTargets
  RUNTIME DESTINATION bin      # Windows 的 .dll
  LIBRARY DESTINATION lib      # macOS 的 .dylib / Linux 的 .so
  ARCHIVE DESTINATION lib)     # Windows 配套的 .lib 导入库（不是静态库那份）

# 对外头文件：纯 C 头 + header-only C++ 包装（CallEngineTypes.hpp 是它拆出来的值类型，被它 include）。
# C++ 内部实现头（capi/src/*.h）不装——它们是「C++ → C 的转换层」的私有实现，不是交付面。
install(FILES
  include/imrtc/imrtc_c.h
  include/imrtc/imrtc_c_history.h
  include/imrtc/imrtc_c_debug.h
  include/imrtc/imrtc_c_call_events.h
  include/imrtc/CallEngine.hpp
  include/imrtc/CallEngineTypes.hpp
  include/imrtc/CallEngineObserver.hpp
  DESTINATION include/imrtc)

# LICENSE（MIT，仓根 LICENSE 文件，2026-09-16 用户拍板）装到发布包根目录——
# zip 解压后第一层就能看见，不塞进 include/ 或 lib/ 底下。
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE" DESTINATION .)

install(EXPORT imrtcTargets
  FILE imrtcTargets.cmake
  NAMESPACE imrtc::
  DESTINATION lib/cmake/imrtc)

include(CMakePackageConfigHelpers)

# 兼容性策略：SameMajorVersion。C ABI 红线 2 是「只许追加字段、永不重排删除」，
# 所以同大版本内新版 dylib 对旧版头文件永远向后兼容；大版本号只在协议不兼容时才升
# （CLAUDE.md）。于是 find_package(imrtc 2.0.0) 应当接受任何 2.x.y（x/y ≥ 请求值），
# 拒绝 1.x 与 3.0.0——那正是 SameMajorVersion 的语义，不用手写 CheckVersion 段。
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/imrtcConfigVersion.cmake"
  VERSION ${IMRTC_VERSION}
  COMPATIBILITY SameMajorVersion)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/imrtcConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/imrtcConfig.cmake"
  INSTALL_DESTINATION lib/cmake/imrtc)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/imrtcConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/imrtcConfigVersion.cmake"
  DESTINATION lib/cmake/imrtc)
