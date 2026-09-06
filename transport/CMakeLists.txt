# im_rtc_transport_ix —— Transport 的真实实现，**本仓唯一需要第三方依赖的目标**。
#
# 依赖：IXWebSocket，BSD-3-Clause，锁死在 v12.0.1（两平台同一版本号，CONVENTIONS §9）。
# 用 FetchContent 而不是把源码 vendor 进来：版本号在这里一眼可见，升级只改一行，
# 也不会把几万行别人的代码塞进我们的 git 历史。**代价是配置期需要网络**——
# 离线时用 -DIMRTC_WITH_IX_TRANSPORT=OFF 关掉，engine 与全部测试照样能编能跑。
include(FetchContent)

set(USE_TLS ON CACHE BOOL "" FORCE)
# TLS 后端用平台自带的：macOS 走 SecureTransport、Windows 走 mbedTLS（IXWebSocket 的默认选择）。
# 注意这与「TLS 只有 BoringSSL 一份」的设想不同——媒体那一刀引入 libwebrtc 之后，
# 包里会有两份 TLS 实现。信令是低频小帧，这个代价可以接受；要收敛成一份就得换 Boost.Beast。
set(USE_ZLIB OFF CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  ixwebsocket
  GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
  GIT_TAG        v12.0.1
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(ixwebsocket)

# IXWebSocket 自己的 CMakeLists 给自己开了 -Wall -Wextra -pedantic，编出来十几条
# unused-parameter。别人的代码用别人的标准，但那堆噪声会把**我们自己的**警告淹掉，
# 所以在这里单独闭嘴。这不改它的行为，只改它的诊断。
if(NOT MSVC)
  target_compile_options(ixwebsocket PRIVATE -w)
endif()

add_library(im_rtc_transport_ix STATIC src/IxTransport.cpp)

target_include_directories(im_rtc_transport_ix PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# imrtc_warnings 只挂在我们自己的目标上——第三方源码用它自己的标准。
target_link_libraries(im_rtc_transport_ix PUBLIC imrtc::engine PRIVATE ixwebsocket imrtc_warnings)

add_library(imrtc::transport_ix ALIAS im_rtc_transport_ix)
