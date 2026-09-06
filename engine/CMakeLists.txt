# im_rtc_engine —— 无 UI、零 Qt 依赖的 C++17 Engine。
#
# 本期只有纯逻辑（JSON / 信封 / 帧 / 状态机），所以是个静态库、没有任何链接依赖。
# 媒体（libwebrtc）与 WS 客户端在后面的刀里加进来，届时它们只会出现在
# src/media/ 与 src/signaling/ 内部，**公开面不变**。
add_library(im_rtc_engine STATIC
  src/Errors.cpp
  src/Enums.cpp
  src/Reasons.cpp
  src/json/JsonValue.cpp
  src/json/JsonParser.cpp
  src/signaling/Envelope.cpp
  src/signaling/Backoff.cpp
  src/signaling/Heartbeat.cpp
  src/signaling/PendingRequests.cpp
  src/signaling/Connection.cpp
  src/signaling/Discipline.cpp
  src/signaling/FieldSpec.cpp
  src/signaling/FramesSys.cpp
  src/signaling/FramesRoom.cpp
  src/signaling/FramesCall.cpp
  src/signaling/Registry.cpp
  src/state/MachineTypes.cpp
  src/state/CallMachine.cpp
  src/state/CallRecv.cpp
  src/state/RoomMachine.cpp
  src/state/RoomRecv.cpp
  src/state/EngineMachine.cpp
)

target_include_directories(im_rtc_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(im_rtc_engine PRIVATE imrtc_warnings)

# engine **不许**链接 Qt，也不许链接任何 GUI 库（CONVENTIONS §1）。
# 这里刻意什么都不 link——将来加依赖时，加什么会在 review 里一眼看见。
add_library(imrtc::engine ALIAS im_rtc_engine)
