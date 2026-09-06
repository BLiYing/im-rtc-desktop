#pragma once

#include "imrtc/FieldSpec.h"

namespace imrtc {

/**
 * 帧字段声明表。**每一张表对应 RTC_PROTOCOL.md 里的一张字段表**，
 * 改协议 → 改一致性向量 → 改这里，顺序不许颠倒（§9）。
 *
 * 用函数返回 `const&` 而不是全局常量：静态初始化顺序在跨编译单元时没有保证，
 * 而这些表之间有引用关系（TRACK_PUBLISHED 引用 TRACK）。函数内静态量是惰性的，
 * 天然没有这个问题。
 */

// ---- sys 域：连接、鉴权、心跳、错误（§1、§7）----

/** emptyFields 用于 data 恒为 {} 的帧：sys.ping / sys.pong / 各种纯 ack。 */
const FrameFields& emptyFields();
/** helloFields：WS 打开后必须在 5 秒内发出的第一帧（§1.2）。 */
const FrameFields& helloFields();
/** limitsFields：服务端下发的限额，让客户端能本地预校验（§2.6）。 */
const FrameFields& limitsFields();
/** helloOkFields：鉴权成功的应答。 */
const FrameFields& helloOkFields();
/** errorFields：sys.error 的 data（§7）。 */
const FrameFields& errorFields();

// ---- room 域：房间与媒体（§3）----

/** participantFields：房间成员快照。 */
const FrameFields& participantFields();
/** trackFields：一条已发布 Track 的快照。 */
const FrameFields& trackFields();
/** joinFields：进房请求。**auto_subscribe / publish_audio 默认 true**。 */
const FrameFields& joinFields();
/** joinOkFields：带回整个房间的快照，客户端据此一次性把九宫格搭起来。 */
const FrameFields& joinOkFields();
/** leaveFields：离房请求。 */
const FrameFields& leaveFields();
/** publishFields：发布请求。cid 是客户端生成的本地 track 标识。 */
const FrameFields& publishFields();
/** publishOkFields：回带 track_id 与原样回显的 cid 供配对。 */
const FrameFields& publishOkFields();
/** trackIdFields：只带 track_id 的帧（unpublish / unsubscribe）。 */
const FrameFields& trackIdFields();
/** muteFields：开关麦克风/摄像头。**这不是 unpublish**，Track 与协商都保留。 */
const FrameFields& muteFields();
/** layerFields：订阅与换层。max_layer 是**上界不是命令**。 */
const FrameFields& layerFields();
/** sdpFields：room.offer 与 room.answer 共用。 */
const FrameFields& sdpFields();
/** iceFields：trickle ICE。candidate 为 "" 表示收集结束，接收方必须容忍。 */
const FrameFields& iceFields();
/** participantJoinedFields：有人进房。 */
const FrameFields& participantJoinedFields();
/** participantLeftFields：有人离房。 */
const FrameFields& participantLeftFields();
/** trackPublishedFields：有人发布了 Track。 */
const FrameFields& trackPublishedFields();
/** trackUnpublishedFields：有人销毁了 Track。 */
const FrameFields& trackUnpublishedFields();
/** trackMutedFields：对应 onUserAudioAvailable / onUserVideoAvailable。 */
const FrameFields& trackMutedFields();
/** speakerFields：一个正在说话的人。volume 0~100，**整数**。 */
const FrameFields& speakerFields();
/** activeSpeakersFields：服务端节流 300ms，客户端**不得**依赖更高频率。 */
const FrameFields& activeSpeakersFields();
/** qualityEntryFields：一个人的网络质量，level 0~6。 */
const FrameFields& qualityEntryFields();
/** qualityFields：服务端节流 2s。 */
const FrameFields& qualityFields();
/** roomClosedFields：房间结束。 */
const FrameFields& roomClosedFields();

// ---- call 域：振铃流程（§4）。Call 层**不碰媒体**，所以这里没有一个 SDP 字段。----

/** inviteFields：发起通话。user_data 是 opaque **字符串**不是对象（§2.4 规则 5）。 */
const FrameFields& inviteFields();
/** inviteOkFields：**主叫此时禁止 room.join**——接听前不进 SFU（§4.1）。 */
const FrameFields& inviteOkFields();
/** callIdFields：只带 call_id 的上行帧共用（accept / reject / cancel / hangup / join）。 */
const FrameFields& callIdFields();
/** inviteMoreFields：群通话中途加邀（P4）。仅主叫可发。 */
const FrameFields& inviteMoreFields();
/** incomingFields：被叫收到的邀请，对应 onCallReceived。 */
const FrameFields& incomingFields();
/** ringingFields：告诉主叫「对方设备开始响铃了」。它**不对应任何回调**。 */
const FrameFields& ringingFields();
/** memberOutcomeFields：某成员的裁决，四个帧共用。 */
const FrameFields& memberOutcomeFields();
/** cancelledFields：主叫取消，发给全部被叫设备。 */
const FrameFields& cancelledFields();
/** connectedFields：「可以进房了」，对应 onCallBegin。 */
const FrameFields& connectedFields();
/** handledElsewhereFields：本账号另一台设备处理了这通电话。 */
const FrameFields& handledElsewhereFields();
/** endedFields：**唯一终态帧**。所有结局都走它。 */
const FrameFields& endedFields();

}  // namespace imrtc
