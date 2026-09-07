#pragma once

#include <string>
#include <vector>

#include "imrtc/Envelope.h"
#include "imrtc/FieldSpec.h"

namespace imrtc {

/**
 * 帧类型注册表，对应 RTC_PROTOCOL.md 附录 A 的帧索引。
 *
 * 「加一个帧」的完整动作是：改协议文档 → 改 conformance 向量 → 在这里注册 →
 * 五仓跟进。顺序不许颠倒（§9）。
 */
namespace frame {
// sys 域
extern const char* const kHello;
extern const char* const kHelloOk;
extern const char* const kPing;
extern const char* const kPong;
extern const char* const kError;

// room 域：上行
extern const char* const kRoomJoin;
extern const char* const kRoomLeave;
extern const char* const kRoomPublish;
extern const char* const kRoomUnpublish;
extern const char* const kRoomMute;
extern const char* const kRoomSubscribe;
extern const char* const kRoomUnsubscribe;
extern const char* const kRoomUpdateLayer;
extern const char* const kRoomOffer;
extern const char* const kRoomAnswer;
extern const char* const kRoomIceCandidate;

// room 域：事件
extern const char* const kRoomParticipantJoined;
extern const char* const kRoomParticipantLeft;
extern const char* const kRoomTrackPublished;
extern const char* const kRoomTrackUnpublished;
extern const char* const kRoomTrackMuted;
extern const char* const kRoomActiveSpeakers;
extern const char* const kRoomQuality;
extern const char* const kRoomClosed;

// call 域：上行
extern const char* const kCallInvite;
extern const char* const kCallAccept;
extern const char* const kCallReject;
extern const char* const kCallCancel;
extern const char* const kCallHangup;
extern const char* const kCallInviteMore;
extern const char* const kCallJoin;

// call 域：下行裁决
extern const char* const kCallIncoming;
extern const char* const kCallRinging;
extern const char* const kCallAccepted;
extern const char* const kCallRejected;
extern const char* const kCallBusy;
extern const char* const kCallNoAnswer;
extern const char* const kCallCancelled;
extern const char* const kCallConnected;
extern const char* const kCallHandledElsewhere;
extern const char* const kCallEnded;
}  // namespace frame

/**
 * requestTypes 是全部上行请求帧。请求必须带非空 req_id，且**恰好**有一条应答。
 *
 * room.offer / answer / ice_candidate 不在这张表里——它们是双向的，
 * 「谁是请求方」由 pc 字段决定（§3.3），不由 type 决定。
 */
const std::vector<std::string>& requestTypes();

/**
 * reservedTypes 是 §3.6 的会议层留位帧：**已占名但 v1 不实现**。
 * 客户端不该发它们；收到服务端的 1003 时要能分清「将来会有」与「压根没有」。
 */
const std::vector<std::string>& reservedTypes();

/** isRequestType 报告这个 type 是不是上行请求帧。 */
bool isRequestType(const std::string& type);

/** isReservedType 报告这个 type 是不是已占名但未实现的会议层帧。 */
bool isReservedType(const std::string& type);

/**
 * replyTypeOf 给出某个上行请求帧的**应答类型**。
 *
 * 绝大多数是 `<type>.ok`，但 `room.offer` 不是——pub 侧的 offer 由 `room.answer`
 * 直接作为应答回来（§3.3 固定 offerer），它没有 `.ok`。这条例外只写在这一处。
 */
std::string replyTypeOf(const std::string& requestType);

/** isOkType 报告这个 type 是不是一条成功应答（`<请求>.ok`）。 */
bool isOkType(const std::string& type);

/**
 * lookupFrame 返回某帧类型的字段声明；未注册返回 nullptr。
 *
 * 请求帧的 .ok 如果没单独登记，一律给空对象——纯 ack 是常态，
 * 不必为每个 room.mute.ok 写一份声明。
 */
const FrameFields* lookupFrame(const std::string& type);

/** knownFrameTypes 列出全部已注册的帧类型（不含自动派生的 .ok），供测试核对。 */
std::vector<std::string> knownFrameTypes();

/**
 * decodeFrame 按 Envelope.type 解出帧数据（线路形状 + 默认值）。
 *
 * 未注册的 type 抛 unknown_type——但**客户端收到未知 type 时应当静默忽略**
 * （§2.3 前向兼容，服务端可能比客户端新），别把这个错误抛给宿主。
 */
Json decodeFrame(const Envelope& envelope);

}  // namespace imrtc
