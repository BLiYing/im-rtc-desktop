# 测试目标。**一致性向量从 im-rtc-server 那一份读**，禁止拷贝到本仓。
set(IMRTC_CONFORMANCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../im-rtc-server/docs/conformance"
    CACHE PATH "im-rtc-server/docs/conformance 的路径（五仓共用的单一真相源）")

add_executable(im_rtc_engine_tests
  TestHarness.cpp
  Vectors.cpp
  ConformanceTest.cpp
  JsonTest.cpp
  CallFsmTest.cpp
  RoomFsmTest.cpp
  ConnectionTest.cpp
  ReconnectTest.cpp
  CallEngineTest.cpp
  MediaPlaneTest.cpp
)

target_include_directories(im_rtc_engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(im_rtc_engine_tests
  PRIVATE IMRTC_CONFORMANCE_DIR="${IMRTC_CONFORMANCE_DIR}")

# IxTransport 的契约测试只在真实 Transport 被构建时才编——离线机器关掉那个开关，
# 其余测试照跑。
# transport_ix 已经 PUBLIC 链了 engine，这里再列一次 engine 会让链接器报
# duplicate libraries，所以两条分支各链各的。
if(TARGET imrtc::transport_ix)
  # capi 的冒烟测试链的是**静态**那份：省一次装载，而且 ASan 能看穿整条链路。
  # 导出面本身由 scripts/check-abi.sh 对**动态库**验（那是宿主真正拿到的东西）。
  target_sources(im_rtc_engine_tests PRIVATE IxTransportTest.cpp CapiTest.cpp)
  target_link_libraries(im_rtc_engine_tests PRIVATE imrtc::capi_static imrtc::transport_ix
                        imrtc_warnings)
else()
  target_link_libraries(im_rtc_engine_tests PRIVATE imrtc::engine imrtc_warnings)
endif()

add_test(NAME engine COMMAND im_rtc_engine_tests)
