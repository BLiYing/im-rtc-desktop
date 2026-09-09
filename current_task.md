# Current Task — im-rtc-desktop（C++17 Engine + C ABI + Qt Demo）

> **活快照**：只记当前状态，**就地覆盖、不追加**。历史见 `git log` 与 [current_task.archive.md](current_task.archive.md)。
> 工程规范见 [CONVENTIONS.md](CONVENTIONS.md)（**§2 是 C ABI 边界，本仓最重要的一条**）；
> 分期见 `im-rtc-server` 的 `docs/design/RTC_CALL_DESIGN.md` §8（桌面）与 §10（分期）。

## 当前焦点

**2026-09-09：补齐两条 parity 裂缝（分支 `fix/parity-leave-failed-and-2006`）。113 个用例全绿。**

**① 本仓此前是四端里唯一没有 `leave_failed` 的**（Web / iOS / Android 早就有）。
离房被拒是真事：服务端在「会话已不在房间里」时回 1203，而那语义恰恰是**我们已经不在房里了**，
本地却还停在 `leaving`——媒体停不掉（摄像头与前台资源一直开着），再 leave 被 R1 拒成 2005，
再 join 因为「不在 idle」也被拒，**除非 logout 这台 Engine 再也进不了任何房间**。
补了三处：`CallEngine::failLocally` 认 `room.leave`、`RoomMachine` 新增 `leave_failed`
分支（归零 + `onRoomLeft`，与 `leave.ok` 同一个收场）、`EngineMachine` 路由给房间机。

**② pub 侧 ICE 自愈补上放弃阈值**（协议 §7.2）：连续 `kPubIceGiveUp`（3）次重启仍 failed
抛一次 2006，之后继续重试但不再重复抛；回 connected 清零。
本仓原先 sub 报、pub 不报，pub 那条于是永久静默重试。
计数在 `MediaPlane::resetPubNegotiation()` 里归零（换连接即新一轮）。

新增用例：`RoomFsmTest.cpp` 的 `roomLeaveRejectedStillSettles`、
`MediaPlaneTest.cpp` 的三条 `iceRestartPub*`。**仍只证明了 Darwin，Windows 未验证。**

**`CallEngine.cpp` 拆完了（2026-09-08）：589 → 459，新增 `engine/src/CallEngineSession.cpp`（153 行）。**
搬走的是 `login / logout / updateToken` 三个方法整段，纯位移——**没有改动一行逻辑**，
八步门禁全绿、109 个引擎用例 + 23 个 Demo 用例照旧。

挑这一刀是因为这一段与状态机几乎没有耦合：它只做「把 `options_` 摊成 `ConnectionOptions`、
把连接层六个回调接到 `apply()` 上」，之后所有判断都在状态机里。拆完两边各自内聚——
门面剩「宿主动作 → 状态机输入」的转译与出帧，新文件是连接的生老病死。
**别把新的业务判断加进 `CallEngineSession.cpp`**，那属于状态机；那边只负责接线。

顺带把 `Handshake.h` 显式包含上了（`deviceIdValid` 原先是从别的头里蹭进来的）。

---

**上行协商闸门补上了（2026-09-08）**，`./scripts/test.sh` 八步全绿、**109 个引擎用例**。
预置的一刀——桌面端还没有媒体，撞不到，但接线形状已经是会撞的那个。

**要防的是什么**：发布 audio + video 两条轨道 → 两次 `room.publish.ok` → 房间机连吐
两帧 `room.offer{pub}`。两个一起在飞时 offer#2 的 `setLocalDescription` 覆盖掉 offer#1，
answer#1 回来就落在一个对不上的本地描述上。iOS 真机上是
`Called in wrong state: stable (INVALID_STATE)`（那次自愈了），
**Android 上同一个缺陷的后果是上行再也协商不出去**。

**闸门在 `MediaPlane`，与 iOS 刻意不同**：iOS 放在 `IMFrameLoop`，因为在它那儿只有帧泵
能表达「这一帧先别发」；本仓的 `fillSdp` 本身就是帧的接管点，返回 true 即
「这一帧我收下了，什么时候真发由我说了算」。另两处与 iOS 相同、与 Android 不同：

| | Android | iOS | 本仓 |
|---|---|---|---|
| 锁 | 要（三个线程碰） | 不要（actor） | **不要**（单线程 tick） |
| `pendingIceRestart` | 要 | 不要 | **不要**（那一位在适配器上，P2 那一刀放的） |

**放闸的四个终局**：answer 落地 / answer 应用失败 / 帧根本没发出去 / 换连接时清零。
**攒一条就够**——offer 描述的是当前全部轨道的状态，三条待办合成一条不丢东西。

**同轮改了 `MediaPlane::Deps::send` 的签名**（`void` → `bool`）：闸门要知道这一帧到底有没有
走出去。发不出去却当成发出去，闸门就永远停在「有一个在飞」，上行从此协商不出去而日志里
一切正常。**内部接口，C ABI 不受影响。**

**载重验过，并且量清楚了每条断言钉住的到底是什么**：拿掉整道闸、answer 落地后不放闸、
只在成功时放闸——三处都当场变红。而**「请求失败放闸」与「恢复时清零」互为兜底**：
单独拿掉任一条都不红，两条一起拿掉才红。这是刻意留的冗余（一条走门面、一条走媒体面），
已写进用例注释，免得下一个人当重复代码删掉。

**没有真机、也不可能有**：还没有 `WebRTCAdapter`，「两个 offer 一起在飞会互相覆盖」这一半
验不了，验的是「第二条确实被拦住、放闸后确实补上」。

**顺带查实**：**Web 至今没有这道闸**（`roomRecv.ts` 的 `handlePublishOk` 每次直接吐一帧
offer，全仓搜不到 in-flight 记账）。已在 `CLIENT_PARITY` 新增的那一行里记成 ⬜。

---

**ICE 自愈与恢复后重协商补上了（2026-09-08）**，`./scripts/test.sh` 八步全绿、
**104 个引擎用例**。四端到齐，桌面端是最后一个。

**两行是同一刀**：同一个 `restart_pub_ice` 动作，两个触发点。

| 落点 | 是什么 |
|---|---|
| `MediaAdapter::restartPubICE()` | **只置位**，下一个 offer 才生效。位记在适配器上 |
| `RoomMachine` 的 `restart_pub_ice` | 产出 `room.offer{pc:pub, sdp:""}`。**刻意不进 isBufferable** |
| `MediaPlane::onPcState(pub, failed)` | 触发点一：信令还活着、只有媒体路径断了 |
| `MediaPlane::renegotiateAfterResume()` | 触发点二：`hello.ok{resumed:true}`，门面在那儿调 |

**两个触发点缺一不可。** 网一断信令先断，房间立刻进 reconnecting，而 PC 要约 30 秒
才判 failed——那时动作会被本地拒掉且不进缓冲，于是**在它唯一该生效的场景里等于不存在**
（iOS 真机 2026-09-07 的实证，四端同一条路）。

**「要重启」和「要补一次协商」必须分开记**：位在适配器上、帧走状态机。位若跟着帧走，
忙的时候排队一次就丢，补出来的是个普通 offer——那条连接**永远重连不上而日志里一切正常**。
有专门的用例断言「出去的那一帧真的带着重启位」，注入把顺序反过来立刻红。

**载重验过**：五处注入都当场变红——顺序反过来、去掉恢复那个触发点、把动作改成可缓冲的、
`disconnected` 也重启、`sub` 失败也去重启 pub。
（第三条第一次没抓住：原用例只断言「reconnecting 期间发不出去」，而可缓冲的写法在那一刻
同样发不出去，差别在**恢复之后会多补一条**。用例补到恢复之后才载重。）

**没有真机、也不可能有**：桌面端还没有 `WebRTCAdapter`，整条链路走的是假适配器。
「重启之后 ICE 真的重新打洞了」这一半要等媒体落地，所以对照表里是 🟡 不是 ✅。

**行为有一处变化**：`pub` 判 failed 不再抛 `onError(2006)` 给宿主了——它现在会自愈，
报一个正在恢复的错误只会让界面闪一下。改成 warn 日志。`sub` 那条照旧报（服务端救，我们救不了）。

**顺手补的**：两张表都不认的动作原先被**静默丢掉**（没有帧、没有回调、没有错误、没有日志），
现在留一条 warn。`restart_pub_ice` 落地时正好踩了这一次——忘了往 `isRoomAct` 里加一行，
症状就是「什么都没发生」，最难查的那一类。

---

**日志设施落地了（2026-09-08）**，`./scripts/test.sh` **八步**全绿、98 个引擎用例
+ 23 个 Demo 用例。此前本仓**一行日志都没有**，出了问题只能靠单步。

| 层 | 是什么 |
|---|---|
| engine | `imrtc::log`（`Log.h`）——可注入 sink、分级、结构化字段；`redact` / `redactSdp` / `redactCandidate` |
| 帧日志 | `Connection` 上下行各一条 **debug**，带 `request_id` 与 `call_id`/`room_id`；ping/pong 不记 |
| 状态跃迁 | `CallEngineEvents` 里一条 **info**，只认那 8 个回调（info 的量级约束是设计目标，不是估计） |
| C ABI | 追加三个符号 `set_log_sink` / `set_log_level` / `log`，导出面 27 → 30，**追加式** |
| 回传 | **在 Qt Demo 里**（`RemoteLogSink`）——engine 不认识 HTTP 也不该认识 |
| 闸门 | `scripts/check-logging.sh`，进 `test.sh` 第 2 步，带 `--selftest` |

**sink 是 fan-out 不是替换**：iOS 踩过反例——「装了 sink 就不写控制台」，
而宿主装的正是回传服务端的 sink，于是网断那一刻唯一的出口跟着一起没了。

**回传的三个坑**照 iOS/Android 的教训避开：超时显式设短（5 秒；iOS 踩过默认 60 秒 +
发送闩，一个卡住的请求让后面所有日志静默丢掉）、队列满了丢最旧的、**发失败不重试不回队**。

**怎么验的**：两个 Demo 实例对着真服务端互打一通（接通 4 秒挂断），两份
`client-desktop-*.log` 落盘，`timeline.py --dir dev-logs` 把**两端 + 服务端**交错排出来，
`call_id` / `request_id` / `session_id` 三者都对得上。`im-rtc-server/scripts/timeline.py`
同轮补上 `desktop` 的识别与配色（此前只认 web/ios/android，桌面日志会被归成服务端的）。
闸门三条违规各验过一次拦得住，并做过一次性能返工：**逐文件起进程那版要跑 2 分 45 秒**，
改成一次 grep 扫全部文件之后 6 秒。

**联调把级别开到 debug**：`IMRTC_LOG_LEVEL=debug ./scripts/demo.sh alice`。
帧日志在 debug 上，而「帧到底发出去没有」正是「按了没反应」唯一问得出答案的地方。

**已知限制**：进程被 SIGTERM/SIGKILL 掉时，攒着还没发的最后一批会丢
（`uninstall()` 会 flush，但信号不跑析构）。正常退出不受影响。

**没做**：环形缓冲 + `exportDiagnostics()`（LOGGING.md §7 把它列为 P3，四端都还没做）；
Demo 里没有切级别的界面入口，只有环境变量。

---

**握手这一关补齐了（2026-09-08）**，`./scripts/test.sh` 七步全绿、**91 个引擎用例**
+ 23 个 Demo 用例；另**对真服务端验过两条**（见下）。四端里桌面是最后一个补的。

补的是同一个 bug 的两半：**`retryable` 分流让失败说清楚，入参校验让它根本发不出去。**

| | 做了什么 |
|---|---|
| 入参校验 | `imrtc_v1_engine_create` 同步拒掉不合规的 `device_id`（协议 §2.5），`CallEngine::login` 再挡一道（走 C++ 门面那条路）。**只校验不改写** —— `MI 8` 与 `MI8` 删掉空格会撞成同一个 id，两台设备互相顶号 |
| 被拒分流 | `Connection::abortIfHandshakeRejected`：不可重试的**一次就放弃**，并按「谁救得了」抛三类原因之一 |
| 新枚举 | `KickedReason{TakenOver, AuthExpired, ConfigRejected}`，穿到 C ABI 是 `imrtc_v1_kicked_reason` |

**判据是错误码表里的 `retryable`，不另立名单**——那张表是五仓共用向量的一部分。
两条边界都钉了用例：**local 组的码直接放行**（`close()` 拿 2005 结掉在飞的握手，
那是宿主自己按的 logout；不挡的话**静默续期会把人踹回登录页**，因为续期正是
先 logout 再换票）；**本端不认识的码信帧上自带的那一位**（不认识的码会被折算成
1501，而它 `retryable == true`，照着判就是「服务端每加一个终局码，客户端就多一种
无限重连」——本仓漏过 1106 一次）。

**原因绕开状态机**：`room_fsm.json` 把 `ws_closed_4403` 的 `onKickedOut` args 钉成 `{}`，
而同一个内部事件又被 4403 与「鉴权失败到顶」复用，状态机没条件知道原因。
所以由门面在派发前把连接层给的原因补进 args，**五仓共用的向量一个字没动**。

**载重验过**：五处注入都当场变红——删掉放弃那一刀、local 组不再放行、未知码改信
折算后的 1501、校验形同虚设、以及**把假服务端的「拒了就关连接」去掉**（去掉后两条
用例一起红，证明断言不是空的；Web 与 iOS 补这条时都踩过空断言）。
**真服务端**：伪造的票 → `1101` → 一次就放弃并抛 `auth_expired`；
`device_id` 传 `"Pixel 2 XL"` → `create` 回 `bad_params`，**服务端日志里一个字都没有**。

**顺带纠正一条一直在传的说法**：桌面端「收到 1004 无限重连」是**不准确**的。
服务端对 `device_id` 不合规关的是 **4400**，而 4400 本来就不重连。真正吃亏的是
**1106 app_disabled**（关 4401 → 白退避三轮 → 还报成「票的问题」）。
缺的从来不是「会不会停」，而是**停下来之后宿主知不知道为什么**。

**没做**：`onKickedOut` 之外的回调没动；日志设施仍然没有（本仓至今一行日志都没有，
排查只能靠单步）；**Windows 一次都没编译过**。

---

**回调顺序的重入问题已修（2026-09-08）**，`./scripts/test.sh` 七步全绿、81 个引擎用例（rebase 到「本地收场」那一刀之上后重跑）。

`dispatchOutput` 是**先发帧、再抛回调**（有意为之：宿主在 onCallBegin 里回调引擎时，
状态不能比线路旧）。代价是发帧可能就地失败（`sendOne` → `failLocally` → `apply`），
于是**内层跑完整个 dispatchOutput 把事件全抛了，而外层一条还没抛**。
`call.connected` 那一步同时产出 onCallBegin 与一帧 room.join——room.join 发不出去时，
宿主收到的顺序是 `onError → onRoomLeft → onCallBegin`：拿着一条「结束」去关一个
它还不知道存在的通话，界面收不了场。

**改法**：不是把两个循环调个头（那会毁掉「先发帧」那条），而是让重入的事件排队。
发帧循环期间 `sendDepth_ > 0`，此时产生的事件一律进 `deferredEmits_`；回到最外层后
先抛自己的，再按产生顺序放队列里的。顺序恢复成 `onCallBegin → onError → onRoomLeft`。

| 决定 | 为什么 |
|---|---|
| 深度只裹住**发帧循环**，不裹抛事件 | 宿主在回调里回调进来是一次新的最外层派发，事件本来就该同步可见。要治的只是引擎自己的重入 |
| `failLocally` 与媒体面的 `reportError` 都改走 `emitOrDefer` | 它们是仅有的两处「绕过状态机直接抛给宿主」。`reportError` 多数时候是异步回来的（深度 0，就地抛），但**同步的适配器会让它落在发帧循环里**（`sendOne` → `fillSdp` → 当场失败）。让所有抛给宿主的事件走同一条路，比在每个入口各自判断可靠 |
| 排空用 `while + swap`，不直接迭代成员容器 | 抛的过程中宿主可能回调进来，那条路上的失败会继续往队列里追加——边遍历边扩容会踩迭代器 |
| `onError` 的 `for_type` 补进 `EmittedEvent` | 原先本地补的那条错误带 `type`、状态机产出的那条固定传空串。走同一条路之后统一从 args 取，取不到仍是空串 |

**载重验过**：把内层改回就地抛，新用例立刻红，实得正是
`error → roomLeft → callBegin` 那个倒序。

**没做**：一致性向量仍然没有覆盖「帧发不出去」这一段（向量描述的是正常时序）；
本轮只在 macOS 上编译测试过，**Windows 一次都没编译过**（本仓一贯如此）。

---

---

**网络一直不回来时通话再也退不出去，已修（2026-09-08）**，`./scripts/test.sh` 全绿（80 用例）。
**未真机复验，且只跑过 Darwin。**

四端契约的最后一端（iOS / Android / Web 已先修）。本地放弃的**唯一**入口是
「重连上了但 `resumed=false`」时的 `synthesizeNetworkEnd`，它要求先连回来；
网络不回来那一刻永远不会到，界面就永远停在「正在重连」，而且**连挂断都点不动**
（挂断只产出一帧发不出去的 `call.hangup`，本地状态按 §4.2 铁律 1 一动不动）。
真机是在 iOS 上撞到的，四端同形。

`Connection` 里加一个到期时刻（本仓是 `tick(nowMs)` 模型，不需要定时器，
四端里数这一版最干净），到点抛 `onSessionUnrecoverable`，
状态机走与 `resumed=false` 完全相同的那段。协议 §1.4 有对应条款。

**上界 = `3×ping + 30s + 5s` 余量（默认 80 秒），不是恢复窗口那 30 秒**：
服务端的 30 秒是从**它自己察觉**算起，而它要连续 3 个心跳周期收不到东西才察觉（§1.3）。
**取短了会杀掉一通还能恢复的电话** —— 真机实测断开 14 秒后重连成功、通话照常继续。

三条用例，都验过回退即红（不起倒计时 / 每次断开都重排 / 上界只取 30 秒）。

**P5 进行中。第一~三刀 + 门面 + 媒体面接线 + 第五刀 capi + 第六刀 Qt Demo 已落地。**
`./scripts/test.sh` **七步全绿**（macOS）：77 个引擎用例 + 23 个 Demo 界面用例，
约 16700 行 C++17。第七步只在 `IMRTC_BUILD_DEMO=ON` 时存在（需要 Qt）。
落地明细见 [current_task.archive.md](current_task.archive.md)。
**P5：C ABI + Qt Demo 已交付,`./scripts/test.sh` 七步全绿**(macOS,77 个引擎用例 + 23 个 Demo 界面用例,约 16700 行 C++17)。
落地明细见 [current_task.archive.md](current_task.archive.md)。

对外交付物已成立:`libim_rtc_engine_capi.dylib` + 一个 C 头,**导出面只有 27 个 `imrtc_v1_*` 符号**
(`scripts/check-abi.sh` 守着)。`scripts/smoke.sh` 经 C ABI 走,与 Qt / C# 宿主同一条路,对着真服务端跑通。

**媒体推迟,按纯信令模式交付**(2026-09-06 定):libwebrtc 桌面预编译包没有 macOS x86_64,
本机是 Intel Mac。第四刀只做了上半(`MediaAdapter` 接口 + `MediaPlane` 接线,假适配器测全),
真正的 `WebRTCAdapter` 等 Apple Silicon 或 Windows 机器。**能拨号、能进房、能收到全部状态回调,就是没有声音和画面。**

**还没有的**:真实媒体(SDP / ICE / 声画)、设备枚举、渲染路径 B(原始帧回调)、共享屏幕、C# 绑定。
**Windows 一次都没编译过。**

## 下一步

- **本仓的静默失败点清单**（P0×2 / P1×4 / P2×6，2026-09-09 扫描）见
  `../im-rtc-server/docs/ops/silent-failure/desktop.md`，跨端结论与修复顺序见同目录的
  `SILENT_FAILURE_AUDIT.md`。**逐条状态只在那里维护，别抄回本文件。**
  未修的头两条：`onDisconnected()` 无参把关闭码/原因/willReconnect 全抹平（Demo 永远显示「正在重连…」）、`deps.send` 对非请求帧无条件返回 true。

1. **异步口子的形状**（一件事，两处用）：渲染路径 B 的原始帧回调，与
   `probeMicrophone` / `startLocalPreview` 这两个还没出 C ABI 的方法，
   卡的是同一个问题——**带 completion / 高频回调的东西怎么过 C ABI**。
   要定：帧格式（I420/NV12）、那块内存谁 free、回调频率（30fps 跨 ABI 是真问题）、
   completion 的 `user_data` 生命周期。**实现等媒体，形状现在就能定**，
   而且该一次定完，别分两次定出两套风格。
2. **`WebRTCAdapter`**：等 Apple Silicon 或 Windows 机器。宿主侧的坑已经清完了，
   到时候只剩一个文件。
2. ~~**P5 第四刀下半 · `WebRTCAdapter`**~~ —— **已推迟，不在当前排期内**（2026-09-06 定）。
   等 Apple Silicon 或 Windows 机器到手再做，做完与 Web/iOS 各互打一次。
   - **ICE 重启的信令这一半已经做完了**（2026-09-08，见「当前焦点」）。
     `WebRTCAdapter` 要做的只剩把 `restartPubICE()` 那一位交给
     `createOffer({iceRestart:true})`，并在 offer 生成后清掉。下面这段留作背景：
     规则是「**各自重启自己 offer 的那条**」——`pub` 由客户端救、`sub` 由服务端救，
     沿用固定 offerer 的设计，所以**不需要新协议帧**，也不会两边同时 offer 打架。
     触发用 `failed` 不用 `disconnected`（后者是几秒的抖动）。
     不做的后果不是"画质差一点"：切网 / 休眠唤醒之后人**永久掉出这通通话**，
     对端格子从此是一块黑，而界面上一切正常、计时还在走、谁也不挂断。
     另有一个坑四端都钉了用例：**「要重启」和「要补一次协商」必须分开记**——
     忙的时候重启请求只能先记成待办，待办里不带「重启」这一位，补出来的就是个普通 offer，
     那条连接**永远重连不上，而日志里一切正常**。
4. **按需**：C# / P&#8203;Invoke 绑定（C ABI 已经定型，这一层是薄的）。
5. **体量预警只剩两处**：`capi/src/imrtc_c.cpp` 537、`demo/MainWindow.cpp` 512
   （阈值 600，预警线 480）。`imrtc_c.cpp` 是 27 个回调各一段几乎一样的跳板，
   涨得最快，下一刀碰它之前先看行数。
6. **日志的下一步**：环形缓冲 + `exportDiagnostics()`（LOGGING.md §7 的 P3），
   给宿主做「报告问题」按钮用。四端都还没做，做的时候该一起定形状。

## 已知坑 / 限制

- **2006 的阈值「3」没经过真机校准，而且它现在抛出来也没人接。** 两件事一起记（2026-09-09）：
  - **阈值待校准**：libwebrtc 判 `failed` 约 30 秒一轮，连续 3 次就是**一分半以后**宿主才知道，
    用户多半早挂了。真机弱网跑过之后很可能要调成 2 次、或者改成按时间而不是按次数。
    四端 libwebrtc 版本还不一样（iOS M152 / Android M150 / 桌面 M150 / Web 是浏览器自带），
    `failed` 的触发时机不见得对得齐——这条只有真机验得出来。
  - **目前它在界面上等于不存在**：四端 Kit 的错误出口都只认几个码
    （Web uikit 2 个、iOS `default: break`、Android `when` 没有 `else`），2006 落地即消失。
    所以现在**回归风险≈0，价值也≈0**，要等 Kit 那几个兜底补上才通。
  - 弱网环境暂缓搭建（2026-09-09 决定），有条件再做。

- **C ABI 有三处不向后兼容的改动（趁 0.1.0 还没有宿主接入时改掉）**：
  - `on_kicked_out` **多了一个 `reason` 参数**（2026-09-08）：
    `void(*)(void*)` → `void(*)(void*, imrtc_v1_kicked_reason)`。
    这是**唯一**能把「该换票 / 该回登录页 / 该改配置」告诉宿主的地方，而三者的处置
    完全相反。**导出面没变**（27 个符号，改的是回调签名不是符号表），所以
    `check-abi.sh` 拦不住这一类——抄过回调表的宿主要跟着改。
    **这个窗口关上（第一个宿主接入）之后，补原因就得走版本协商。**
  - `imrtc_v1_speaker` / `imrtc_v1_quality` **各加了首字段 `uint32_t struct_size`**。
    它们是唯二**按数组**交出去的结构体（`on_active_speakers` / `on_network_quality`
    收指针 + 个数），宿主是按 `sizeof` 的步长在里面走 —— 没有这个字段，将来追加任何
    字段都会让已发出去的宿主读 `items[1]` 落在结构体中间，而且**没有任何版本信号**
    能让它察觉。已经照着 C 头重新编译过的宿主不受影响；抄过结构体定义的要同步。
  - `CallEngineOptions` **多了一个 `wallClock`**（仅 C++ 内部接口，C ABI 不受影响）。
    `clock` 的含义随之变成**单调时钟**，默认 `steadyClock()`；`wallClock` 默认
    `systemClock()`，**只**用来填信封的 `ts`。原先两者是同一个墙上时钟，
    NTP 往回校正一次就会让心跳、超时、退避集体停摆，而且一声不吭。

- **`room.mute` 要的是服务端的 `track_id`，不是本端 cid**（§3.2、room_fsm.json 第 10 步）。
  映射记在房间机的 `publishTrackIds` 里，媒体面经 `Deps::trackIdOfCid` 借出来查。
  **`publish.ok` 还没回来的那个窗口里静音**：本端照常停发，但线路上发不出去 ——
  当前是报一条 `invalid_state`。要做得更好就得把意图攒到 `publish.ok` 之后重放，
  暂时没做。

- **测试用的 `FakeTransport` 默认同步回调，真件是异步的。** 凡是依赖「close() 返回时
  回调已经抛完」的逻辑，同步假件上全绿、真件上全错（`logout()` 就这么漏过一次）。
  要守那类规则的用例，把 `FakeNet::deferClose` 打开 —— 它会像 IxTransport 那样
  把关闭事件排到 `poll()` 里放。

- **✅ 已决定（2026-09-06）：媒体推迟，Intel Mac 上先不支持声音与视频。**
  这是一个**决定**，不是一条待办——不要再拿它当「卡住了」重新讨论。
  桌面端在此期间按**纯信令模式**交付；影响面被 `MediaAdapter` 关死在
  **一个还没写的文件 `WebRTCAdapter.cpp`** 里，其余全部代码与测试不受影响。
  起因是 libwebrtc 桌面预编译包的平台矩阵：

  | 平台 | shiguredo 预编译包 | 备注 |
  |---|---|---|
  | macOS **arm64** | ✅ `webrtc.macos_arm64.tar.gz`（319 MB） | 要 Apple Silicon |
  | macOS **x86_64** | ❌ **近 100 个 release 一个都没有** | **本机是 Intel，卡在这** |
  | Windows x86_64 | ✅ `webrtc.windows_x86_64.zip` | 集成方那边有 |
  | Ubuntu x86_64 | ✅ | 与本产品无关 |

  **决定：等机器**——Apple Silicon Mac 或 Windows，哪台先到手就在哪台做。
  已经排除掉的三条路，别再回头试：
  - ❌ **`bengreenier/webrtc` 的 `darwin-x64` 包**（273 MB）—— 它**确实存在**，能在 Intel Mac 上用，
    但冻在 2023 年的 branch 5735（约 **M115**），与我们要对齐的 **M150** 差 35 个里程碑。
    为一台过渡机器去适配一套三年前的 API，代价高于收益。
  - ❌ **自己从源码编 macOS x86_64 的 libwebrtc** —— depot_tools + 数十 GB + 数小时，还要长期维护。
    本机数据卷已 97% 满，连磁盘都不够。
  - ❌ **换成 stasel/WebRTC 的 XCFramework** —— 那是 ObjC API，Windows 上用不了，
    会变成两套媒体适配器，正好违背「一套代码两平台」。

  生态背景（说明这不是我们选型失误）：Homebrew 已于 2026-09 停发 Intel macOS bottle，
  macOS 27 起 Apple 不再支持 Intel，GitHub Actions 2027 下线 Intel runner——
  **x86_64 macOS 正在被整个生态放弃**，不只是 libwebrtc 一家。

  版本打算锁 **m150.7871.3.2**（与 Android 那条线的 M150 对齐，见 CLIENT_PARITY §3），
  落地时再确认。
- **本机只有 macOS**：Windows 侧编译与验证需要集成方配合，时间未定。
  **不许把「macOS 过了」写成「桌面端完成」**——每次交付分平台说清楚。
- **Qt 6.8 + Xcode 26 会撞 `ld: framework 'AGL' not found`**。macOS 26 SDK 删掉了 `AGL.framework`，
  而 Qt 6.8 的 `FindWrapOpenGL.cmake` 在找不到时会**无条件回退到硬编码的 `-framework AGL`**。
  **注意别用 `find_library` 探测**：AGL 还留在**运行系统**的 `/System/Library/Frameworks/` 下，
  只是从 **SDK** 里删了，而 `ld` 只看 SDK——`find_library` 会误报「存在」。
  必须直接查 SDK 目录。修法已进 Demo 的 `CMakeLists.txt`，也要写进《接入指南》——
  集成方只要是 Xcode 26 + Qt 6.8 就会撞同一个坑。
- **Demo 必须经 capi 调引擎**。走内部 C++ 接口会掩盖全部 ABI 问题，那样「Demo 跑通」不等于「宿主接得通」。
  已经是这么接的（`demo/EngineBridge.cpp` 用 `imrtc::capi::Engine`，链的是动态库）。
- **本机 `lupdate` 跑不起来 —— 这是个连带依赖，不是我们用了 QML**（2026-09-06 查证）。
  `lupdate` 要能从 `.qml` 里抠 `qsTr()`，所以它链了 **QtQml**；而 QtQml 属于
  `qtdeclarative`（QML / Qt Quick 那一整套：QtQml、QtQuick、QtQuickControls2…），
  最小装法没装它。**Demo 一行 QML 都没有**（用的是 Widgets，在 `qtbase` 里）。

  | 工具 | 依赖 | 状态 |
  |---|---|---|
  | `lrelease`（.ts → .qm） | 只要 QtCore | ✅ 正常，构建与运行都不受影响 |
  | `lupdate`（源码 → .ts） | QtCore + QtNetwork + **QtQml** | ❌ 起不来 |

  **决定：不装。** 装它只买到「一个构建期工具能启动」，代价 430 MB 下载 /
  约 2.8 GB 磁盘，而那 2.8 GB 里 99% 的东西永远不会被加载；集成方装的是全量 Qt，
  他们那边 `lupdate` 本来就能跑，所以这条限制**只影响本机，不影响交付物**。
  真要装：`aqt install-qt mac desktop 6.8.3 clang_64 --archives qtdeclarative -O ~/Qt`
  （只写 qtdeclarative，会并进已有的 `~/Qt/6.8.3/macos`，不重装 qtbase）。

  代价是 `demo/i18n/imrtc_demo_en.ts`（132 条）**手工维护**：
  **加了新的 `tr()` 之后记得同步 .ts**，否则那条在英文下会静默退回中文，不报错。
- **层上界的合法性，宿主只会从我们这里得到反馈**：协议 §2.4 规则 6 规定
  枚举越界**必须兜底**（`max_layer` 兜到 `l`）而不是报错——这条兜底是 §10
  前向兼容的前提，所以服务端**按设计**照收并回 `.ok`（1306 `layer_unavailable`
  已标为保留码、服务端不发）。于是层名写错的症状是**画面糊而客户端一切正常**。
  `imrtc_v1_set_remote_layer` 里的同步 `BAD_PARAMS` 是宿主唯一的反馈，
  **别把它当冗余删掉**——它拦的不是服务端会拦的东西，是服务端按协议不该拦的东西。
- **报层要在 `onUserVideoAvailable` 里报，不能只在建格子时报**。格子通常在
  `onUserEnter` 就建好，那时对方视频轨还没发布，引擎手里没有 track_id，
  这次调用会被**静默丢掉**（返回 0，不是错误——先建格子后到轨道是正常时序）。
  只报一次的结果是永远按默认的 `m` 下发，**没有任何症状**。
- **本端预览必须走 `attachLocalView`，不是 `attachView(自己的 uid, …)`**。
  引擎不知道自己的 uid，本端画面也没有远端 trackId。别为此约定魔法 uid。
- **隐藏控件不腾地方**：想让画面铺满时把头像 `hide()` 掉是不够的，
  同一个布局里的 `addStretch()` 会把空间全吃掉——实测画面只分到 520×**16**。
  用 `QStackedWidget` 分页。
- **原生子窗口会盖住同一个窗口里所有 Qt 绘制**，与 Qt 的 z 序无关（`raise()` 没用）。
  所以格子的名字标签 / 静音角标 / 发言描边不能由格子自己 `paintEvent` 画——
  必须放进**画面之后创建的另一个原生子窗口**（`demo/VideoTile.cpp` 的 `TileChrome`）。
- **原生视图的尺寸滞后于 Qt 的 `resizeEvent`**：那一刻读 `view.layer.bounds`
  拿到的是上一次的值，而且**不会再有第二次 resizeEvent 来纠正**。
  实测格子 157×92 / 控件 153×88 / layer 停在 116×86。几何一律**以 Qt 控件尺寸为准**。
  另：`autoresizingMask` 与显式 `frame` **不能同时开**，会相乘（153×88 → 190×90）。
- **带原生子窗口的界面截图要用 `QScreen::grabWindow`**。`QWidget::grab()` 能看到
  原生层但**内容滞后一帧**（实测与系统合成差 5.11/255，先跑一次系统合成后只差 0.54）。
  `[CATransaction flush]` 不管用。`screencapture` 那条路要「屏幕录制」授权。
- **test.sh 里不许写死测试可执行文件名**：按用例分文件之后，写死会在改名后
  **静默跑一个过时的二进制**。已经踩过一次，现在是遍历 `imrtc_demo_*_test`。
- **红按钮：动作按「有没有 call」分叉，文案按人数分叉——依据不一样，别混。**
  会议房 → `leaveRoom()`；**1v1 与群通话都是 `hangup()`**（接通前 cancel/reject）。
  文案则是 1v1「挂断」、群通话与会议房「离开」。
  **我们写错过**：设计稿 §05 原话是「群/会议 → leaveRoom()」，照抄之后群通话的离开者
  **永远收不到 `onCallEnd`**（服务端对 `room.leave` 只广播 `participant_left`），
  记录落不下来且违反 I1。Web / iOS 的代码本来就只判 `isMeeting`，是对的。
  设计稿已于 2026-09-06 更正，别再照旧版写。规则本身抽在
  `CallOverlay::dangerAction()` 里（单一真相源），`demo/tests/` 有回归测试守着，
  做过变异测试：改回旧规则，两个用例当场红。
- **联调时每轮换一次用户名**。上一轮被 kill 的进程在服务端还挂着「通话中」
  （30s 恢复窗口），复用同一个 uid 会让下一轮直接回 `busy`——
  `scripts/smoke.sh` 早就是这么做的（`smoke-$$`），Demo 联调也照办。
- **提示不许用 `QMessageBox` 静态函数**。它开一个嵌套事件循环并**等人点确定**，
  在没人看着的场合会把后面的动作全挡住——实测中它挡掉过一次自动挂断，
  害我以为是引擎少发了 `onCallEnd`。`MainWindow::toast()` 现在是非模态自动消失的提示条。
  只有「清空通话记录」这类破坏性操作才该用模态。
- **同机开两个实例要加 `--profile`**。macOS 的 `QStandardPaths` **不理会 `$HOME`**
  （它走密码库里的真实家目录），所以靠环境变量隔离不了，两个实例会写同一份
  `call-history.json` 互相覆盖。`scripts/demo.sh` 已自动按用户名传 `--profile`。
- **静音 / 摄像头按钮在 Demo 里是禁用态**：没接媒体适配器时引擎的 `openMic()`
  是**静默空操作**，留个假开关比画成禁用更糟。`WebRTCAdapter` 落地后
  把 `CallOverlay` 里的 `setBlocked({})` 打开即可。
- **`engine/` 里出现任何 `Q` 开头的类型 = 直接打回**（CONVENTIONS §1）。WS 换独立库，
  TLS 复用 libwebrtc 自带的 BoringSSL，不再引第二份 OpenSSL。
- **C ABI 的头号崩因是生命周期**：`destroy` 必须阻塞到所有回调线程静默；宿主是 C#/Java 这类
  GC 语言时它没法帮你保活。见 CONVENTIONS §5。
- **回调线程是宿主的责任**：engine 零 Qt，不认识宿主的事件循环，**切 UI 线程由宿主自己做**。
- **时钟在门面收口（已定）**：状态机与连接层都不读时钟（前者是不变量 I4，后者是为了可测），
  `CallEngine` 持有 `Clock` 并在 `tick()` 里往下喂。**engine 不自己起线程**——
  宿主的事件循环长什么样我们不知道，多起一条就等于把「回调在哪个线程」甩给宿主。
  宿主按 ~200ms~1s 的粒度调 `tick()` 即可。
- **`logout()` 会本地合成 `onCallEnd(reason=network)`**：服务端随后也会结束那通电话，
  但那条 `call.ended` 到不了我们手里。不合成的话中途登出的通话会从宿主的记录里凭空消失
  （记录只由 onCallEnd 拼出来）。**待与协议确认**：§5.1 的 I8 目前只写了「重连恢复失败」
  一个例外，logout 是第二个——要么写进 I8，要么给 reason 加一个值。
- **C ABI 的三条要写进《接入指南》第一页**：① 回调在 Engine 的线程上抛，切 UI 线程是宿主的事；
  ② 回调里给出的指针只在该次回调期间有效；③ `imrtc_v1_engine_destroy` 阻塞到回调静默，
  返回之后绝不会再有回调——宿主是 C# / Java 这类 GC 语言时尤其要紧。
- **导出面靠脚本守，不靠自觉**：`-fvisibility=hidden` **挡不住 libc++ 的 RTTI**
  （weak-def，实测漏 42 个 `std::function` 的 typeinfo）。白名单在
  `capi/exported_symbols.txt`，守门的是 `scripts/check-abi.sh`（test.sh 第 5 步）。
  Windows 侧由 `__declspec(dllexport)` 天然收口，但仍应在集成方那边用 `dumpbin /exports` 核一次。
- **`Connection` 不是线程安全的**：所有方法（含 `tick`）要在同一个线程上调用。
  `IxTransport` 已经替它把 IX 后台线程的回调排队投递到 `poll()`（`tick()` 里调），
  但**宿主自己也必须在同一个线程上调 Engine 的方法**。这条要写进《接入指南》。
- **包里会有两份 TLS 实现**：信令走平台 TLS（macOS SecureTransport / Windows mbedTLS），
  媒体那一刀引入的 libwebrtc 自带 BoringSSL。原先设想的「TLS 只有一份」要换 Boost.Beast
  才走得通，代价是引入 boost。信令是低频小帧，这个代价当前可以接受——
  **但打包体积与 Windows 侧的 mbedTLS 依赖要在第四刀复核一次。**
- **4401 必须有重试上限**：重连带的是同一枚 token，没有上限就是拿同一把坏钥匙永远敲同一扇门
  （Web 端实测重试到第 19 次还在敲）。连续 3 次后抛 `onKickedOut` 回登录页换票。
- **libwebrtc 桌面预编译包**（`shiguredo-webrtc-build`）随 Chromium 里程碑更新，两平台同一版本号；
  API 变化由 `MediaAdapter` 隔离；**静态链进动态库内部**。
- **macOS 权限 SDK 代劳不了**：`NSMicrophoneUsageDescription` / `NSCameraUsageDescription` 与
  Hardened Runtime entitlement 必须由**宿主 App 的 bundle** 声明。Demo 自己就是第一个宿主。
- **Electron / CEF / Tauri 宿主不接本仓**，直接用 `im-rtc-web` 的 `@im-rtc/call-engine`。

## 关联工程 / 常用命令

- **各端能力对照表：`../im-rtc-server/docs/CLIENT_PARITY.md`**（单一真相源，✅ 只写在那里）。
  桌面端按 **§1.1 交付分层表**逐行填，**不许用「桌面 ✅」一个格子结账**。
- 五仓（本地同级 `/Users/liying/IOSProject/im-rtc/`）：
  [im-rtc-server](https://github.com/BLiYing/im-rtc-server)（**协议契约在这里，只读引用**）·
  [im-rtc-ios](https://github.com/BLiYing/im-rtc-ios) · [im-rtc-web](https://github.com/BLiYing/im-rtc-web) ·
  **im-rtc-desktop**（本仓）· [im-rtc-android](https://github.com/BLiYing/im-rtc-android)。
- 集成方：公司现有 Windows/Mac Qt 项目（不在本机，需对方配合）。
- 常用命令：
  ```bash
  ./scripts/install-hooks.sh                     # 新 clone 跑一次
  ./scripts/test.sh                              # 唯一测试入口：体量 + 配置 + 编译 + 单测
  cmake --preset macos-clang && cmake --build --preset macos-clang
  cmake -S . -B build/asan -G Ninja -DIMRTC_ASAN=ON && cmake --build build/asan   # ASan/UBSan
  ```
