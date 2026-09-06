# 联调小工具。**需要真服务端**，所以它不进 test.sh——那条路必须在没有网络的机器上也能跑。
add_executable(imrtc_smoke Smoke.cpp)
target_link_libraries(imrtc_smoke PRIVATE imrtc::transport_ix imrtc_warnings)
