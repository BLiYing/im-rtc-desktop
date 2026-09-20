/*
 * imrtc_c_call_events.h —— 通话事件回调的载荷结构体（invite / begin / end / summary / missed）。
 *
 * 从 imrtc_c.h 拆出来只为守体量红线；**宿主只 include imrtc_c.h 就够了**，它末尾会带上本头
 * （imrtc_c.h 里已 typedef 好这几个名字，这里只给出结构体定义）。
 * 兼容性规则同 imrtc_c.h：字段只许追加，永不重排、永不删除。
 */
#ifndef IMRTC_V1_C_CALL_EVENTS_H
#define IMRTC_V1_C_CALL_EVENTS_H

#include "imrtc_c.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 收到的通话邀请，对应 on_call_received。 */
struct imrtc_v1_call_invite {
  uint32_t struct_size;
  const char* call_id;
  const char* caller;
  /** 被叫列表。群通话里被叫要靠它摆占位格。 */
  const char* const* callee_ids;
  uint32_t callee_count;
  const char* media_type;
  imrtc_v1_bool is_group;
  /*
    以下两个字段是 2026-09-15 追加的：**只追加、不改旧字段**（CONVENTIONS §2 红线 2）。
    宿主用旧头编译时结构体更小，读不到这两个字段也不会崩——它压根不知道它们存在，
    引擎这边只是多填了两个旧宿主不看的尾部字节。
  */
  /** 宿主自己的群号，可空（HOST_INTEGRATION_DESIGN §3.2）。 */
  const char* chat_group_id;
  /** 宿主私有字节，原样透传，引擎不解析。 */
  const char* user_data;
  /*
    下面这个字段是 2026-09-16 追加的，理由与上面那两个相同：**只追加、不改旧字段**
    （CONVENTIONS §2 红线 2）。旧宿主的结构体更小，读不到它也不会崩。
  */
  /**
   * 这次邀请是谁发的。**与 caller 不是一回事**：caller 恒为这通电话的发起人，
   * 群通话里被别人 invite_more 拉进来时，inviter 才是按下「添加成员」的那个人。
   * 界面上「谁邀请你」要显示这个。服务端没带（旧版本）时引擎已回落成 caller。
   */
  const char* inviter;
  /*
    以下两个字段是 2026-09-20 追加的（只追加、不改旧字段，CONVENTIONS §2 红线 2）。
    旧宿主的结构体更小，读不到它们也不会崩。
  */
  /** 此刻已经在通话里的人（不含自己）。展开页据此摆正常格子，callee_ids 里不在其中的才是「呼叫中…」。 */
  const char* const* joined_ids;
  uint32_t joined_count;
};

/** 通话接通，对应 on_call_begin。 */
struct imrtc_v1_call_begin {
  uint32_t struct_size;
  const char* call_id;
  const char* room_id;
  const char* media_type;
  imrtc_v1_bool is_group;
  /** "caller" / "callee"。 */
  const char* role;
  /*
    以下三个字段是 2026-09-15 追加的，理由同 imrtc_v1_call_invite。
    取 call.connected 里的值，为空时回落到本通 call.incoming / call_ex 选项记下的值
    （HOST_INTEGRATION_DESIGN §3.3）；经 call.join 加入的人没收过 call.incoming，
    回落不到，只能靠 call.connected 自带。
  */
  /** 发起人。 */
  const char* caller;
  /** 宿主自己的群号，可空。 */
  const char* chat_group_id;
  /** 宿主私有字节。 */
  const char* user_data;
};

/** 通话结束，对应 on_call_end。**所有结束分支的唯一出口**。 */
struct imrtc_v1_call_end {
  uint32_t struct_size;
  const char* call_id;
  /** §6 的封闭枚举，陌生值已折成 "error"。 */
  const char* reason;
  /** 未接通恒为 0。**别自己算时长**，用这个值。 */
  int64_t duration_sec;
  const char* ended_by;
  /*
    以下字段是 2026-09-15 追加的：**只追加、不改旧字段**（CONVENTIONS §2 红线 2）。
    宿主用旧头编译时结构体更小，`struct_size` 天然报不到这里——引擎只是多填了
    旧宿主不看的尾部字节，不影响它读前面那几个字段。
  */
  /** `reason` 的类型化版本，见 imrtc_v1_end_reason。 */
  imrtc_v1_end_reason reason_code;
};

/**
 * 通话事实汇总，对应 on_call_summary（通话记录设计 §4）。**紧跟 on_call_end、每通拿到 call_id 的
 * 电话恰好一次**；宿主要发通话记录消息的话，只在 role == "caller" 时发。
 */
struct imrtc_v1_call_summary {
  uint32_t struct_size;
  const char* call_id;
  /** §6 的封闭枚举，陌生值已折成 "error"。 */
  const char* reason;
  imrtc_v1_end_reason reason_code;
  /** 服务端给的秒数，未接通恒 0。 */
  int64_t duration_sec;
  const char* ended_by;
  /** "audio" / "video"。 */
  const char* media_type;
  imrtc_v1_bool is_group;
  const char* chat_group_id;
  const char* caller;
  /** "caller" / "callee"。 */
  const char* role;
  /** 1v1 的对端 uid；群通话为空串。 */
  const char* peer;
  /** 主叫拨号时透传的宿主私有字符串，原样返回。 */
  const char* user_data;
};

/** 通话中被第三个人呼叫、已被自动回忙线，对应 on_call_missed。 */
struct imrtc_v1_call_missed {
  uint32_t struct_size;
  const char* call_id;
  const char* caller;
  const char* reason;
};

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* IMRTC_V1_C_CALL_EVENTS_H */
