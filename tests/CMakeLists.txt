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
)

target_link_libraries(im_rtc_engine_tests PRIVATE imrtc::engine)
target_include_directories(im_rtc_engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(im_rtc_engine_tests
  PRIVATE IMRTC_CONFORMANCE_DIR="${IMRTC_CONFORMANCE_DIR}")

add_test(NAME engine COMMAND im_rtc_engine_tests)
