# im_rtc_engine_capi —— **对外唯一的交付物**。
#
# 出两个目标：
#   - im_rtc_engine_capi        动态库（.dylib / .dll），宿主拿的就是它 + 一个 C 头
#   - im_rtc_engine_capi_static 静态库，只给本仓的测试与 Demo 用，省一次装载
#
# 符号可见性在顶层设成 hidden，头文件里的 IMRTC_API 把 imrtc_v1_* 这一族放出来——
# 别的什么都不导出（CONVENTIONS §2 红线 1）。
set(IMRTC_CAPI_SOURCES src/imrtc_c.cpp)

add_library(im_rtc_engine_capi SHARED ${IMRTC_CAPI_SOURCES})
add_library(im_rtc_engine_capi_static STATIC ${IMRTC_CAPI_SOURCES})

foreach(target im_rtc_engine_capi im_rtc_engine_capi_static)
  target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
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
