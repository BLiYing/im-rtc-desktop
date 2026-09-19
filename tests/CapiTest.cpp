#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#include "TestHarness.h"
#include "imrtc/CallEngine.h"
#include "imrtc/CallEngine.hpp"
#include "imrtc/imrtc_c.h"

/**
 * C ABI 的冒烟测试（CONVENTIONS §2 要求的那条）。
 *
 * 它**不测业务**——业务由前面几百个用例守着。它测的是边界本身：
 * 参数校验、struct_size 版本闸、异常不跨界、句柄销毁后没有回调、
 * 以及 header-only 的 C++ 包装确实是**经 C ABI** 走的。
 *
 * 跑在 ASan 下才有全部价值：`destroy` 之后还有回调打进来，就是一个 use-after-free。
 */
namespace {

/** makeOptions 造一份合法参数。URL 指向一个必然连不上的端口——这些用例不需要服务端。 */
imrtc_v1_options makeOptions() {
  imrtc_v1_options options{};
  options.struct_size = sizeof(options);
  options.url = "ws://127.0.0.1:1/v1/ws";
  options.device_id = "mac-abi-1";
  options.sdk = "desktop-test/1.0.0";
  options.request_timeout_ms = 10000;
  return options;
}

/** Counters 记回调次数。它是 C 回调的 user_data。 */
struct Counters {
  int errors = 0;
  int disconnects = 0;
  std::int32_t lastCode = 0;
  std::string lastName;
};

void onErrorCb(void* userData, std::int32_t code, const char* name, const char*) {
  Counters* counters = static_cast<Counters*>(userData);
  ++counters->errors;
  counters->lastCode = code;
  counters->lastName = name == nullptr ? "" : name;
}

void onDisconnectedCb(void* userData) { ++static_cast<Counters*>(userData)->disconnects; }

/** Results 记下结果回调（2.0.0 的 imrtc_v1_result_cb）。 */
struct Results {
  int calls = 0;
  std::int32_t lastCode = -1;
  std::string lastName;
  std::string lastValue;
};

void onResultCb(void* userData, std::int32_t code, const char* name, const char* value) {
  Results* results = static_cast<Results*>(userData);
  ++results->calls;
  results->lastCode = code;
  results->lastName = name == nullptr ? "" : name;
  results->lastValue = value == nullptr ? "" : value;
}

}  // namespace

IMRTC_TEST(capiCreateRejectsBadParams, "C ABI —— 参数校验：空指针与对不上的 struct_size 一律拒") {
  imrtc_v1_engine* engine = nullptr;

  CHECK_EQ(imrtc_v1_engine_create(nullptr, &engine), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS},
           "options 为空");
  imrtc_v1_options options = makeOptions();
  CHECK_EQ(imrtc_v1_engine_create(&options, nullptr), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS},
           "出参为空");

  /*
    struct_size 是**版本闸**：宿主的头比我们新（多了字段）无所谓，我们只读认识的那些；
    比我们旧说明它连必填字段都不全，拒掉。不拒的话我们会去读一段它根本没分配的内存。
  */
  imrtc_v1_options stale = makeOptions();
  stale.struct_size = 8;
  CHECK_EQ(imrtc_v1_engine_create(&stale, &engine), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS},
           "struct_size 太小");

  imrtc_v1_options noUrl = makeOptions();
  noUrl.url = nullptr;
  CHECK_EQ(imrtc_v1_engine_create(&noUrl, &engine), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS},
           "url 为空");
  CHECK_TRUE(engine == nullptr, "失败时不许写出一个野句柄");
}

IMRTC_TEST(capiNullHandleIsSafe, "C ABI —— 对空句柄调任何方法都返回错误码，不崩") {
  // 返回非 0 = 根本没受理，结果回调**不许**再被调（imrtc_v1_result_cb 的约定）。
  Results results;
  CHECK_EQ(imrtc_v1_login(nullptr, "tk", &onResultCb, &results), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS},
           "login");
  CHECK_EQ(imrtc_v1_hangup(nullptr, &onResultCb, &results), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS},
           "hangup");
  CHECK_EQ(results.calls, 0, "没受理就不回调");
  CHECK_EQ(imrtc_v1_force_end(nullptr), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS}, "force_end");
  CHECK_EQ(imrtc_v1_engine_tick(nullptr), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS}, "tick");
  CHECK_EQ(imrtc_v1_set_app_foreground(nullptr, 1), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS},
           "set_app_foreground");
  CHECK_EQ(imrtc_v1_notify_network_changed(nullptr), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS},
           "notify_network_changed");
  CHECK_EQ(imrtc_v1_attach_view(nullptr, "bob", nullptr), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS},
           "attach_view");
  CHECK_EQ(imrtc_v1_attach_local_view(nullptr, nullptr), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS},
           "attach_local_view");
  CHECK_EQ(imrtc_v1_set_remote_layer(nullptr, "bob", "h"), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS},
           "set_remote_layer");
  std::int32_t state = -1;
  CHECK_EQ(imrtc_v1_get_call_state(nullptr, &state), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS},
           "get_call_state");
  // destroy 对空指针必须是空操作——宿主的清理路径上传空是常态。
  imrtc_v1_engine_destroy(nullptr);
  CHECK_TRUE(true, "destroy(nullptr) 没崩");
}

IMRTC_TEST(capiRejectsBadDeviceId,
           "C ABI —— device_id 不合规在 create 就被挡回（协议 §2.5）") {
  imrtc_v1_engine* engine = nullptr;

  /*
    挡在**最早**的地方：create 本来就返回错误码，比 login 更早，而且是同步的。
    真机上那次是 Android 的 `Build.MODEL == "Pixel 2 XL"`——带空格。

    **只校验不改写**：`MI 8` 与 `MI8` 是两款不同的机器，「删掉非法字符」会让它们
    撞成同一个 device_id，后果是两台设备互相顶号、轮流把对方踢下线。
  */
  for (const char* bad : {"Pixel 2 XL", "", "mac.abi", "mac/abi", "\xe8\xae\xbe-1"}) {
    imrtc_v1_options options = makeOptions();
    options.device_id = bad;
    CHECK_EQ(imrtc_v1_engine_create(&options, &engine), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS},
             bad);
    CHECK_EQ(engine == nullptr, true, "拒了就不该交出句柄");
  }

  // 别把校验写成谁都拦。
  for (const char* good : {"mac-abi-1", "Pixel_2_XL", "a"}) {
    imrtc_v1_options options = makeOptions();
    options.device_id = good;
    CHECK_EQ(imrtc_v1_engine_create(&options, &engine), std::int32_t{IMRTC_V1_OK}, good);
    imrtc_v1_engine_destroy(engine);
    engine = nullptr;
  }
}

IMRTC_TEST(capiSetRemoteLayerRejectsBadLayer,
           "C ABI —— 非法层名在边界上就被挡回，不许上线路") {
  imrtc_v1_options options = makeOptions();
  imrtc_v1_engine* engine = nullptr;
  CHECK_EQ(imrtc_v1_engine_create(&options, &engine), std::int32_t{IMRTC_V1_OK}, "create");

  /*
    协议 §2.4 规则 6 只认 none/l/m/h，并规定**集合外的值必须兜底**（兜到 l）
    而不是报错——这是 §10 前向兼容的前提，所以服务端按设计不会拒绝我们
    （2026-09-07 实测：发 "zzz" 照样回 .ok）。于是这道同步校验是**宿主唯一
    会收到的反馈**，放过去的后果只是那条流被降到 l、画面糊。
  */
  for (const char* good : {"none", "l", "m", "h"}) {
    CHECK_EQ(imrtc_v1_set_remote_layer(engine, "bob", good), std::int32_t{IMRTC_V1_OK}, good);
  }
  for (const char* bad : {"high", "L", "", "1"}) {
    CHECK_EQ(imrtc_v1_set_remote_layer(engine, "bob", bad),
             std::int32_t{IMRTC_V1_ERR_BAD_PARAMS}, bad);
  }
  CHECK_EQ(imrtc_v1_set_remote_layer(engine, nullptr, "h"),
           std::int32_t{IMRTC_V1_ERR_BAD_PARAMS}, "uid 为空");
  CHECK_EQ(imrtc_v1_set_remote_layer(engine, "bob", nullptr),
           std::int32_t{IMRTC_V1_ERR_BAD_PARAMS}, "layer 为空");

  imrtc_v1_engine_destroy(engine);
}

IMRTC_TEST(capiLifecycle, "C ABI —— create → set_observer → 调用 → destroy 全程干净") {
  imrtc_v1_options options = makeOptions();
  imrtc_v1_engine* engine = nullptr;
  CHECK_EQ(imrtc_v1_engine_create(&options, &engine), std::int32_t{IMRTC_V1_OK}, "create");
  CHECK_TRUE(engine != nullptr, "句柄非空");

  Counters counters;
  imrtc_v1_observer observer{};
  observer.struct_size = sizeof(observer);
  observer.user_data = &counters;
  observer.on_error = &onErrorCb;
  observer.on_disconnected = &onDisconnectedCb;
  // 其余回调**故意留 NULL**：宿主只关心几个事件是常态，留空不该崩。
  CHECK_EQ(imrtc_v1_engine_set_observer(engine, &observer), std::int32_t{IMRTC_V1_OK},
           "set_observer");

  std::int32_t state = -1;
  CHECK_EQ(imrtc_v1_get_call_state(engine, &state), std::int32_t{IMRTC_V1_OK}, "get_call_state");
  CHECK_EQ(state, std::int32_t{IMRTC_V1_CALL_IDLE}, "初始是 idle");

  /*
    **没登录就拨号**：状态机会推进到 inviting，但那一帧根本没地方发。
    必须报错并退回 idle——早先这里是静默吞掉的，通话会永远停在 inviting，
    界面「正在呼叫…」转个不停，之后每次挂断都发向一个不存在的 call。
  */
  const char* callees[] = {"bob"};
  // 结果回调传 NULL：失败退回 on_error（ACTION_RESULT_DESIGN R7）。
  CHECK_EQ(imrtc_v1_call(engine, callees, 1, "audio", 0, nullptr, nullptr), std::int32_t{IMRTC_V1_OK},
           "调用本身是成功的（错误从回调出）");
  CHECK_EQ(counters.lastCode, std::int32_t{2007}, "该报 not_logged_in");
  CHECK_EQ(imrtc_v1_get_call_state(engine, &state), std::int32_t{IMRTC_V1_OK}, "再查状态");
  CHECK_EQ(state, std::int32_t{IMRTC_V1_CALL_IDLE}, "必须退回 idle");

  // 提示类：没登录时是空操作，照样回 OK、不抛错误回调。
  const std::int32_t errorsBefore = counters.lastCode;
  CHECK_EQ(imrtc_v1_set_app_foreground(engine, 1), std::int32_t{IMRTC_V1_OK}, "set_app_foreground");
  CHECK_EQ(imrtc_v1_notify_network_changed(engine), std::int32_t{IMRTC_V1_OK}, "notify_network_changed");
  CHECK_EQ(counters.lastCode, errorsBefore, "提示类不该报错");

  CHECK_EQ(imrtc_v1_engine_tick(engine), std::int32_t{IMRTC_V1_OK}, "tick");
  CHECK_EQ(imrtc_v1_engine_set_observer(engine, nullptr), std::int32_t{IMRTC_V1_OK}, "注销回调");

  // destroy 之后绝不该再有回调——ASan 会在这条路径上抓 use-after-free。
  imrtc_v1_engine_destroy(engine);
  CHECK_TRUE(true, "destroy 返回了，没崩");
}

IMRTC_TEST(capiObserverStructSize, "C ABI —— 回调表的 struct_size 也是版本闸") {
  imrtc_v1_options options = makeOptions();
  imrtc_v1_engine* engine = nullptr;
  CHECK_EQ(imrtc_v1_engine_create(&options, &engine), std::int32_t{IMRTC_V1_OK}, "create");

  imrtc_v1_observer stale{};
  stale.struct_size = 8;
  CHECK_EQ(imrtc_v1_engine_set_observer(engine, &stale), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS},
           "回调表太旧要拒");

  imrtc_v1_engine_destroy(engine);
}

IMRTC_TEST(capiObserverBackwardCompatWithoutDisconnectedEx,
           "C ABI —— on_disconnected_ex 是追加字段，旧宿主更小的 struct_size 不该被拒") {
  imrtc_v1_options options = makeOptions();
  imrtc_v1_engine* engine = nullptr;
  CHECK_EQ(imrtc_v1_engine_create(&options, &engine), std::int32_t{IMRTC_V1_OK}, "create");

  /*
    模拟一个 2026-09-15 之前编译的旧宿主：它的头里根本没有 on_disconnected_ex
    这个字段，struct_size 天然只到 on_room_closed 那里为止。这里没法真的声明
    一份更小的旧结构体类型（那需要另一份旧头），就用现在这份头造一个 observer，
    再把 struct_size 改小去模拟「宿主那份内存只有这么大」——
    引擎必须只信 struct_size 以内的字节，不能因为新增了字段就把旧宿主拒之门外，
    否则「只追加不删除」的 ABI 承诺就是一句空话。
  */
  Counters counters;
  imrtc_v1_observer legacy{};
  legacy.struct_size = static_cast<std::uint32_t>(offsetof(imrtc_v1_observer, on_disconnected_ex));
  legacy.user_data = &counters;
  legacy.on_disconnected = &onDisconnectedCb;
  CHECK_EQ(imrtc_v1_engine_set_observer(engine, &legacy), std::int32_t{IMRTC_V1_OK},
           "旧宿主的 struct_size（不含新字段）应当被接受");

  // 2026-09-15 ~ 09-17 之间编译的宿主：有 on_disconnected_ex、没有 on_user_ringing。
  imrtc_v1_observer beforeRinging{};
  beforeRinging.struct_size = static_cast<std::uint32_t>(offsetof(imrtc_v1_observer, on_user_ringing));
  beforeRinging.user_data = &counters;
  CHECK_EQ(imrtc_v1_engine_set_observer(engine, &beforeRinging), std::int32_t{IMRTC_V1_OK},
           "不含 on_user_ringing 的 struct_size 也应当被接受");

  imrtc_v1_engine_destroy(engine);
}

IMRTC_TEST(capiMicrophoneAliases,
           "C ABI —— imrtc_v1_open/close_microphone 与旧的 open/close_mic 语义一致") {
  imrtc_v1_options options = makeOptions();
  imrtc_v1_engine* engine = nullptr;
  CHECK_EQ(imrtc_v1_engine_create(&options, &engine), std::int32_t{IMRTC_V1_OK}, "create");

  CHECK_EQ(imrtc_v1_open_microphone(engine), std::int32_t{IMRTC_V1_OK}, "open_microphone");
  CHECK_EQ(imrtc_v1_close_microphone(engine), std::int32_t{IMRTC_V1_OK}, "close_microphone");
  // 旧名字保留做已弃用别名，行为必须与新名字完全一样。
  CHECK_EQ(imrtc_v1_open_mic(engine), std::int32_t{IMRTC_V1_OK}, "open_mic 别名照旧能用");
  CHECK_EQ(imrtc_v1_close_mic(engine), std::int32_t{IMRTC_V1_OK}, "close_mic 别名照旧能用");

  imrtc_v1_engine_destroy(engine);
}

IMRTC_TEST(capiErrorNamesAreStatic, "C ABI —— error_name / version 返回静态串，不需要释放") {
  CHECK_EQ(std::string(imrtc_v1_error_name(2005)), std::string("invalid_state"), "2005");
  CHECK_EQ(std::string(imrtc_v1_error_name(1201)), std::string("room_not_found"), "1201");
  CHECK_EQ(std::string(imrtc_v1_error_name(999999)), std::string("unknown"), "未知码兜底");
  // 同一个码两次拿到的必须是**同一个指针**——不是每次现造一个 std::string 的 c_str()，
  // 那种指针出了函数就悬空了。
  CHECK_TRUE(imrtc_v1_error_name(2005) == imrtc_v1_error_name(2005), "静态串，指针稳定");
  CHECK_TRUE(std::strlen(imrtc_v1_version()) > 0, "version 非空");
  // 五端统一在 1.0.0（2026-09-11）。升版本时这条跟着改——它就是用来逼人记得同步另外四端的。
  CHECK_EQ(std::string(imrtc_v1_version()), std::string("1.0.0"), "SDK 版本");
  CHECK_TRUE(imrtc_v1_version() == imrtc_v1_version(), "静态串，指针稳定");
  CHECK_EQ(imrtc::CallEngineOptions{}.sdk, std::string("desktop/1.0.0"), "引擎 sdk 默认串同源");
  CHECK_EQ(imrtc::ConnectionOptions{}.sdk, std::string("desktop/1.0.0"), "连接 sdk 默认串同源");
}

namespace {

/** WrapperObserver 用来验 header-only 包装那一层。 */
class WrapperObserver : public imrtc::capi::Observer {
public:
  void onError(std::int32_t code, const std::string& name, const std::string&) override {
    ++errors;
    lastName = name;
    lastCode = code;
  }
  int errors = 0;
  std::int32_t lastCode = 0;
  std::string lastName;
};

}  // namespace

IMRTC_TEST(capiCppWrapper, "C ABI —— header-only 的 C++ 包装（它自己也走 C ABI）") {
  WrapperObserver observer;
  {
    imrtc::capi::Engine engine("ws://127.0.0.1:1/v1/ws", "mac-abi-2");
    CHECK_TRUE(engine.valid(), "构造成功");
    CHECK_TRUE(engine.setObserver(&observer).ok(), "注册回调");

    CHECK_EQ(static_cast<int>(engine.callState()), int{IMRTC_V1_CALL_IDLE}, "初始 idle");

    // 与上面那条 C 用例同一件事，只是从包装那边走一遍——两条路必须同样表现。
    engine.call({"bob"}, "audio", false);
    CHECK_EQ(observer.lastCode, std::int32_t{2007}, "该报 not_logged_in");
    CHECK_EQ(static_cast<int>(engine.callState()), int{IMRTC_V1_CALL_IDLE}, "退回 idle");

    CHECK_TRUE(engine.tick().ok(), "tick");
    // 析构前把回调注销掉：observer 是栈上的，它比 engine 活得久，但显式注销是好习惯。
    engine.setObserver(nullptr);
  }
  // Engine 析构 = imrtc_v1_engine_destroy，它阻塞到回调静默为止。
  CHECK_TRUE(true, "RAII 析构没崩");
}

IMRTC_TEST(capiCallExStructSizeIsVersionGate,
           "C ABI —— imrtc_v1_call_ex 的 options.struct_size 也是版本闸") {
  imrtc_v1_options options = makeOptions();
  imrtc_v1_engine* engine = nullptr;
  CHECK_EQ(imrtc_v1_engine_create(&options, &engine), std::int32_t{IMRTC_V1_OK}, "create");

  const char* callees[] = {"bob"};
  imrtc_v1_call_options stale{};
  stale.struct_size = 4;
  Results results;
  CHECK_EQ(imrtc_v1_call_ex(engine, callees, 1, "audio", &stale, &onResultCb, &results),
           std::int32_t{IMRTC_V1_ERR_BAD_PARAMS}, "options 太旧要拒");
  CHECK_EQ(imrtc_v1_call_ex(engine, nullptr, 0, "audio", nullptr, &onResultCb, &results),
           std::int32_t{IMRTC_V1_ERR_BAD_PARAMS}, "callee_ids 为空");
  CHECK_EQ(results.calls, 0, "没受理就不回调");

  imrtc_v1_engine_destroy(engine);
}

IMRTC_TEST(capiCallExOptionsNullEquivalentToPlainCall,
           "C ABI —— imrtc_v1_call_ex 的 options 传 NULL 等价于 imrtc_v1_call") {
  imrtc_v1_options options = makeOptions();
  imrtc_v1_engine* engine = nullptr;
  CHECK_EQ(imrtc_v1_engine_create(&options, &engine), std::int32_t{IMRTC_V1_OK}, "create");

  Counters counters;
  imrtc_v1_observer observer{};
  observer.struct_size = sizeof(observer);
  observer.user_data = &counters;
  observer.on_error = &onErrorCb;
  CHECK_EQ(imrtc_v1_engine_set_observer(engine, &observer), std::int32_t{IMRTC_V1_OK},
           "set_observer");

  // 没登录就拨号：走的是与 imrtc_v1_call 同一条状态机路径，该报 not_logged_in（2007）。
  const char* callees[] = {"bob"};
  CHECK_EQ(imrtc_v1_call_ex(engine, callees, 1, "audio", nullptr, nullptr, nullptr),
           std::int32_t{IMRTC_V1_OK}, "调用本身是成功的（错误从回调出）");
  CHECK_EQ(counters.lastCode, std::int32_t{2007}, "该报 not_logged_in");

  // 传了结果回调：2007 只从结果回来，on_error 不再多报一次。
  const int errorsBefore = counters.errors;
  Results results;
  CHECK_EQ(imrtc_v1_call_ex(engine, callees, 1, "audio", nullptr, &onResultCb, &results),
           std::int32_t{IMRTC_V1_OK}, "带回调也受理");
  CHECK_EQ(results.calls, 1, "结果恰好回一次");
  CHECK_EQ(results.lastCode, std::int32_t{2007}, "结果是 not_logged_in");
  CHECK_EQ(results.lastName, std::string("not_logged_in"), "机读名");
  CHECK_EQ(counters.errors, errorsBefore, "失败时没有多发 on_error");

  imrtc_v1_engine_destroy(engine);
}

IMRTC_TEST(capiCallExRejectsOversizedChatGroupIdLocally,
           "C ABI —— imrtc_v1_call_ex 的 chat_group_id 超限本地先拦，不上线路"
           "（HOST_INTEGRATION_DESIGN §3.3）") {
  imrtc_v1_options options = makeOptions();
  imrtc_v1_engine* engine = nullptr;
  CHECK_EQ(imrtc_v1_engine_create(&options, &engine), std::int32_t{IMRTC_V1_OK}, "create");

  Counters counters;
  imrtc_v1_observer observer{};
  observer.struct_size = sizeof(observer);
  observer.user_data = &counters;
  observer.on_error = &onErrorCb;
  CHECK_EQ(imrtc_v1_engine_set_observer(engine, &observer), std::int32_t{IMRTC_V1_OK},
           "set_observer");

  const std::string tooLong(65, 'g');
  imrtc_v1_call_options callOptions{};
  callOptions.struct_size = sizeof(callOptions);
  callOptions.chat_group_id = tooLong.c_str();
  const char* callees[] = {"bob"};
  // 返回值仍是「调用本身合法」——错误从回调出，跟「未登录就拨号」同一条约定。
  Results results;
  CHECK_EQ(imrtc_v1_call_ex(engine, callees, 1, "audio", &callOptions, &onResultCb, &results),
           std::int32_t{IMRTC_V1_OK}, "调用本身是成功的（1004 从结果回调出）");
  CHECK_EQ(results.calls, 1, "结果恰好回一次");
  CHECK_EQ(results.lastCode, std::int32_t{1004}, "该回 bad_params，不是 not_logged_in");
  CHECK_EQ(counters.errors, 0, "1004 不再经 on_error 抛");

  std::int32_t state = -1;
  CHECK_EQ(imrtc_v1_get_call_state(engine, &state), std::int32_t{IMRTC_V1_OK}, "查状态");
  CHECK_EQ(state, std::int32_t{IMRTC_V1_CALL_IDLE}, "本地拒绝不改变状态");

  imrtc_v1_engine_destroy(engine);
}

IMRTC_TEST(capiInviteDeniedErrorNameIsRegistered,
           "C ABI —— 1409 invite_denied 的机读名可查（HOST_INTEGRATION_DESIGN §3.2）") {
  CHECK_EQ(std::string(imrtc_v1_error_name(1409)), std::string("invite_denied"), "1409");
  CHECK_EQ(std::int32_t{IMRTC_V1_ERR_INVITE_DENIED}, std::int32_t{1409}, "常量值");
}

IMRTC_TEST(capiCppWrapperCallEx,
           "C ABI —— header-only 包装的 callEx 也走 C ABI，本地校验与 C 层一致") {
  WrapperObserver observer;
  imrtc::capi::Engine engine("ws://127.0.0.1:1/v1/ws", "mac-abi-3");
  CHECK_TRUE(engine.valid(), "构造成功");
  CHECK_TRUE(engine.setObserver(&observer).ok(), "注册回调");

  imrtc::capi::CallOptions options;
  options.chatGroupId = std::string(65, 'g');
  engine.callEx({"bob"}, "audio", true, options);
  CHECK_EQ(observer.lastCode, std::int32_t{1004}, "不传 done：超限 chat_group_id 退回 onError");
  CHECK_EQ(static_cast<int>(engine.callState()), int{IMRTC_V1_CALL_IDLE}, "本地拒绝不改状态");

  int doneCalls = 0;
  std::int32_t doneCode = 0;
  const int errorsBefore = observer.errors;
  engine.callEx({"bob"}, "audio", true, options, [&](imrtc::capi::Result<std::string> result) {
    ++doneCalls;
    doneCode = result.code;
  });
  CHECK_EQ(doneCalls, 1, "done 恰好回一次");
  CHECK_EQ(doneCode, std::int32_t{1004}, "done 拿到 1004");
  CHECK_EQ(observer.errors, errorsBefore, "传了 done 就不再多发 onError");

  // 没受理（名单为空）：C 层不会回调，包装层就地把同一个码交给 done，不许悬着。
  doneCalls = 0;
  const imrtc::capi::Error rejected =
      engine.inviteMore({}, [&](imrtc::capi::Result<> result) {
        ++doneCalls;
        doneCode = result.code;
      });
  CHECK_EQ(rejected.code(), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS}, "返回值报没受理");
  CHECK_EQ(doneCalls, 1, "done 仍然恰好回一次");
  CHECK_EQ(doneCode, std::int32_t{IMRTC_V1_ERR_BAD_PARAMS}, "同一个码");

  engine.setObserver(nullptr);
}

IMRTC_TEST(capiErrorIsUsable, "C ABI —— 包装层的 Error 能问出机读名") {
  const imrtc::capi::Error ok;
  CHECK_TRUE(ok.ok(), "默认是成功");
  const imrtc::capi::Error failed(2005);
  CHECK_TRUE(!failed.ok(), "非零就是失败");
  CHECK_EQ(std::string(failed.name()), std::string("invalid_state"), "机读名");
}

namespace {

struct HistoryResult {
  int calls = 0;
  std::int32_t code = 0;
  std::string name;
  std::uint32_t count = 99;
};

void onHistoryCb(void* user, std::int32_t code, const char* name, const imrtc_v1_call_record* records,
                 std::uint32_t count, std::int64_t) {
  HistoryResult* result = static_cast<HistoryResult*>(user);
  ++result->calls;
  result->code = code;
  result->name = name == nullptr ? "" : name;
  result->count = count;
  if (count == 0 && records != nullptr) result->count = 98;  // 失败 / 空页时指针必须是 NULL
}

}  // namespace

IMRTC_TEST(capiFetchCallHistory, "C ABI —— 通话记录：空句柄 / NULL 回调不受理；没登录就地回 2007；包装层同一条路") {
  HistoryResult result;
  CHECK_EQ(imrtc_v1_fetch_call_history(nullptr, 20, 0, &onHistoryCb, &result),
           std::int32_t{IMRTC_V1_ERR_BAD_PARAMS}, "空句柄");
  CHECK_EQ(result.calls, 0, "没受理就不回调");

  imrtc_v1_options options = makeOptions();
  imrtc_v1_engine* engine = nullptr;
  CHECK_EQ(imrtc_v1_engine_create(&options, &engine), std::int32_t{IMRTC_V1_OK}, "create");
  CHECK_EQ(imrtc_v1_fetch_call_history(engine, 20, 0, nullptr, &result), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS},
           "回调不能为 NULL");
  CHECK_EQ(result.calls, 0, "NULL 回调没受理");

  // 没登录：受理了，结果在返回之前就地回 2007（本地拒绝，不需要 tick）。
  CHECK_EQ(imrtc_v1_fetch_call_history(engine, 20, 0, &onHistoryCb, &result), std::int32_t{IMRTC_V1_OK}, "受理");
  CHECK_EQ(result.calls, 1, "恰好回一次");
  CHECK_EQ(result.code, 2007, "没登录");
  CHECK_EQ(result.name, std::string("not_logged_in"), "机读名");
  CHECK_EQ(static_cast<int>(result.count), 0, "失败没有记录");
  imrtc_v1_engine_destroy(engine);

  // 包装层走同一条路；没受理（done 为空）时返回 BAD_PARAMS 且不悬着。
  imrtc::capi::Engine wrapped("ws://127.0.0.1:1/v1/ws", "mac-abi-4");
  int doneCalls = 0;
  std::int32_t doneCode = 0;
  const imrtc::capi::Error accepted = wrapped.fetchCallHistory(
      20, 0, [&](imrtc::capi::Result<imrtc::capi::CallHistoryPage> r) {
        ++doneCalls;
        doneCode = r.code;
      });
  CHECK_TRUE(accepted.ok(), "包装层受理");
  CHECK_EQ(doneCalls, 1, "包装层恰好回一次");
  CHECK_EQ(doneCode, std::int32_t{2007}, "包装层 2007");
  CHECK_EQ(wrapped.fetchCallHistory(20, 0, {}).code(), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS}, "没传 done 不受理");
}

IMRTC_TEST(capiDebugSignToken, "C ABI —— 调试签票：成功写缓冲区；非法入参 / 缓冲区太小 / struct_size 偏小一律 BAD_PARAMS") {
  imrtc_v1_debug_token_options options{};
  options.struct_size = sizeof(options);
  options.app_id = "10000001";
  options.key_id = "dbg-1";
  options.secret = "0123456789abcdef0123456789abcdef";
  options.uid = "alice";
  options.now_unix = 1790000000;

  char out[512];
  std::uint32_t length = 0;
  CHECK_EQ(imrtc_v1_debug_sign_token(&options, out, sizeof(out), &length), std::int32_t{IMRTC_V1_OK}, "成功");
  CHECK_EQ(static_cast<std::size_t>(length), std::strlen(out), "out_len 不含 NUL");
  CHECK_TRUE(std::string(out).rfind("eyJ", 0) == 0, "是 JWT");

  char tiny[8];
  std::uint32_t needed = 0;
  CHECK_EQ(imrtc_v1_debug_sign_token(&options, tiny, sizeof(tiny), &needed), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS},
           "缓冲区太小");
  CHECK_EQ(static_cast<std::size_t>(needed), std::strlen(out), "告知需要的长度");

  CHECK_EQ(imrtc_v1_debug_sign_token(nullptr, out, sizeof(out), &length), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS}, "options 为空");
  options.key_id = "v1";
  CHECK_EQ(imrtc_v1_debug_sign_token(&options, out, sizeof(out), &length), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS}, "非 dbg- 密钥");
  options.key_id = "dbg-1";
  options.uid = nullptr;
  CHECK_EQ(imrtc_v1_debug_sign_token(&options, out, sizeof(out), &length), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS}, "uid 为空");
  options.uid = "alice";
  options.struct_size = 4;
  CHECK_EQ(imrtc_v1_debug_sign_token(&options, out, sizeof(out), &length), std::int32_t{IMRTC_V1_ERR_BAD_PARAMS}, "struct_size 偏小");
}
