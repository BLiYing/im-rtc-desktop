# 联调小工具。**需要真服务端**，所以它不进 test.sh——那条路必须在没有网络的机器上也能跑。
add_executable(imrtc_smoke Smoke.cpp)
# 刻意链**动态库**：宿主拿到的就是这个 .dylib / .dll + 一个 C 头，
# smoke 走同一条路才算证明了「宿主接得通」。
target_link_libraries(imrtc_smoke PRIVATE imrtc::capi imrtc_warnings)
