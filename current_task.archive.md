# current_task 归档 —— im-rtc-desktop

> 只读。**活快照在 [current_task.md](current_task.md)**，退休的细节落到这里，不许再搬回去。
> 更细的历史在 `git log`。

---

## 2026-09-03 ~ 2026-09-06：建仓期（P5 未开工，只有文档与体量门禁）

以下是 2026-09-06 之前的活快照原文。这一版的核心判断已被 2026-09-06 的「引擎零 Qt 依赖 + 对外纯 C ABI」取代——
特别是「Qt 版本：是否兼容 Qt 5.15 …… P5 开工前要问清楚」这条**已作废**（引擎不碰 Qt，宿主用 Qt 几都无关）。

### Current Task — im-rtc-desktop（C++17 Engine + Qt Demo）

> **活快照**：只记当前状态，**就地覆盖、不追加**。历史见 `git log`。
> 工程规范见 [CONVENTIONS.md](CONVENTIONS.md)；方案与分期见 `im-rtc-server` 的
> `docs/design/RTC_CALL_DESIGN.md` §8（桌面）与 §10（分期）。

## 当前焦点

**仓库刚建（2026-09-03），只有文档与体量门禁，尚无一行代码。**

本仓在分期里是 **P5**，排在 P0 协议 → P1 SFU → P2 Web → P3 iOS → P4 群通话之后。

**在此之前本仓的价值是「接收契约」**：协议与一致性向量从第一天起就必须考虑 C++ 端能实现——
帧结构不得依赖 JS/Swift 特有的数据表达（如可选字段的隐式 undefined、关联值枚举）。
P0 定协议时如果发现某个设计 C++ 侧别扭，**现在就提，别等 P5**。

## 下一步

1. **等 P0~P4**。期间只做一件事：**评审协议对 C++ 的友好度**，发现问题回 server 仓提。
2. **P5 第一刀**：CMake 骨架 + `CMakePresets.json`（windows-msvc / macos-clang）+
   libwebrtc 预编译包接入（锁定版本）+ `scripts/` 三件套。
3. **P5 第二刀**：`engine/`——signaling + state，**先跑通一致性向量**（不需要媒体、不需要 GUI）。
4. **P5 第三刀**：`media/WebRTCAdapter`（libwebrtc C++ API）+ 1v1 语音/视频，与 Web/iOS 互通。
5. **P5 第四刀**：Qt Demo 四屏 + 《Qt 接入指南》，交付给公司项目。

## 已知坑 / 限制

- **本机只有 macOS**：Windows 侧编译与验证需要集成方配合，时间未定。
  **不许把「macOS 过了」写成「桌面端完成」**——每次交付都要分平台说清楚。
- **腾讯等厂商没有桌面版含 UI 通话组件**：这是整个方案选择自建信令 + 自建 SFU 的直接原因之一。
  桌面端只能「媒体用 libwebrtc + 信令接自己的协议」，没有捷径。
- **libwebrtc 桌面预编译包**（`shiguredo-webrtc-build`）随 Chromium 里程碑更新，
  两平台必须同一版本号；API 变化由 `MediaAdapter` 接口隔离。
- **Qt 版本**：Qt 6 为主；是否兼容 Qt 5.15 取决于集成方现状，**P5 开工前要问清楚**。
- **裸指针回调是 C++ 端最常见的崩因**（对象先死、回调后到）。观察者一律 weak_ptr 或显式注销，
  见 CONVENTIONS §4。
- **跨平台策略已定，别再翻案**：五端不共享代码，共享「协议 + 状态机 + 测试向量」。

## 关联工程 / 常用命令

- **各端能力对照表：`../im-rtc-server/docs/CLIENT_PARITY.md`**（逐端逐特性状态的**单一真相源**，✅ 只写在那里，本文件不重复）。

- 五仓（本地同级 `/Users/liying/IOSProject/im-rtc/`）：
  [im-rtc-server](https://github.com/BLiYing/im-rtc-server)（**协议契约在这里，只读引用**）·
  [im-rtc-ios](https://github.com/BLiYing/im-rtc-ios) · [im-rtc-web](https://github.com/BLiYing/im-rtc-web) ·
  **im-rtc-desktop**（本仓）·
  [im-rtc-android](https://github.com/BLiYing/im-rtc-android)。
- 集成方：公司现有 Windows/Mac Qt 项目（不在本机，需对方配合）。
- 常用命令（脚本随 P5 落地）：
  ```bash
  ./scripts/install-hooks.sh                     # 新 clone 跑一次
  cmake --preset macos-clang && cmake --build --preset macos-clang
  ./scripts/test.sh                              # 唯一测试入口
  ```

---

## 2026-09-06：P5 第一~四刀（上半）的落地明细

> 从活快照里搬过来的。活快照只留「现在在做什么、坑在哪」，明细在这里。

### 当时的落地清单

**P5 已开工（2026-09-06）。第一~三刀 + 门面 + 媒体面接线全部落地，**
**并且已经对着真服务端跑通了一整轮：握手 → 拨号 → `onCallEnd(offline)`。**

**第四刀卡在一个平台问题上**：libwebrtc 的桌面预编译包**没有 macOS x86_64**
（shiguredo 近 100 个 release 一个都没有），而本机是 Intel Mac。
所以这一刀只做了上半——接口与接线，用假适配器测全；
真正的 `WebRTCAdapter` 要换机器，见「已知坑」。
约 6800 行 C++17。**engine 与全部状态机测试仍是零第三方依赖**——唯一需要下载依赖的是
`transport/` 那一个目标（IXWebSocket v12.0.1，BSD-3-Clause），离线时
`-DIMRTC_WITH_IX_TRANSPORT=OFF` 关掉，其余照编照跑。

已落地：

| 层 | 文件 | 做了什么 |
|---|---|---|
| JSON | `engine/src/json/` | 手写解析器。**数字按值判定**（`1e3` 是整数、`15e-1` 不是），能表达非法值好让上层拒绝 |
| 信封 | `signaling/Envelope.cpp` · `Discipline.cpp` | §2.1 四字段；§2.4 七条硬规则里能脱离帧定义判的四条 |
| 帧 | `signaling/FieldSpec.cpp` · `Frames*.cpp` · `Registry.cpp` | 声明式字段表 + 默认值填充 + 枚举兜底 + 41 个帧类型 |
| 状态机 | `state/CallMachine` · `CallRecv` · `RoomMachine` · `RoomRecv` · `EngineMachine` | §5.1 通话机、§5.3 房间机，以及只有合起来才说得清的四件事 |
| 连接 | `signaling/Connection` · `Heartbeat` · `PendingRequests` · `Backoff` | 握手、心跳、请求应答配对、超时、退避重连、关闭码处置 |
| 真实 WS | `transport/src/IxTransport.cpp` | IXWebSocket v12.0.1。三件必须做对的事见下 |
| **门面** | `src/CallEngine.cpp` · `CallEngineEvents.cpp` · `include/imrtc/CallEngineObserver.h` | 宿主方法 ↔ 状态机 ↔ 连接；§7.5 回调总表；**时钟在这一层收口** |
| **媒体面** | `include/imrtc/MediaAdapter.h` · `src/media/MediaPlane.cpp` | 接口 + 信令↔媒体的接线：进房推流、SDP 填充、候选双向、媒体就绪、终局归零 |
| 联调工具 | `tools/Smoke.cpp` · `scripts/smoke.sh` | 对着真服务端跑一轮，**不进 test.sh** |

**媒体面这一层做了什么**（`WebRTCAdapter` 还没有，全部用假适配器测）：

- **状态机不认识 SDP**：它产出的 `room.offer` / `room.answer` 里 sdp 是空串，
  只表示「现在该协商了」。门面把这两种帧拦下来交给媒体面填真 SDP 再发。
  这样一致性向量的契约一行不用改。
- **进房即推流**：`room.join.ok` → 采麦克风（视频通话再采摄像头）→ 走状态机的
  publish 动作（不是直接发帧，本地记账与 R2 缓存才跟得上）。
- **麦克风被拒整通失败，摄像头被拒只降级为语音**（交互稿 §02）。
- **候选双向**：本端的发上去，远端的收下来，空候选忽略（§3.3）。
- **「媒体就绪」只看 sub PC**：pub 通了只说明我们发得出去，能听见对方才算接通。
| 测试 | `tests/` | 自制 harness + 五份向量的 runner + 假 Transport 的时序测试 + IxTransport 契约测试 |

**IxTransport 的三件必须做对的事**（都有测试钉着）：

1. **关掉 IXWebSocket 自带的自动重连**——重连策略只能有一份。两层同时跑，退避档以
   两倍速度往上走，关闭码规则（4403 不重连）完全失效。测试从行为上验：
   连一个必然被拒的端口，1.5 秒里只该报一次 closed。
2. **不用它的 WS 层 ping**——协议 §1.3 的心跳是业务帧 `sys.ping`，判活条件是
   「收到对端任何一帧」，WS 层 ping/pong 满足不了。
3. **回调跨线程投递**——IX 在自己的后台线程收帧，`Connection` 不是线程安全的。
   事件排队，`poll()`（`tick()` 调用）在宿主线程上放出来。

**连接层的三个设计取舍**（决定了它今天就能测）：

1. **不持有定时器，时间从外面喂**（`tick(nowMs)`）。于是「45 秒静默判死」「10 秒请求超时」
   「退避到第 6 档」不用真的等就能测，也不必在 engine 里塞一个事件循环——宿主的事件循环
   长什么样我们不知道。
2. **按 `req_id` 配对，不按帧类型**。pub 侧的 `room.offer` 是由 `room.answer` 应答的（§3.3）。
3. **socket 藏在 `Transport` 接口后面**。engine 零 Qt，也不该把某个 WS 库焊死在信令逻辑里；
   换库只动一个实现文件。

**真服务端跑通了**（2026-09-06，本机 `rtc-server :8787`）：

```
→ login ws://127.0.0.1:8787/v1/ws（device=mac-smoke-1）
  ✓ onConnected      session=s-3f784ddcb1678838 resumed=false
→ call [nobody-offline] audio 1v1
  ✓ onCallEnd        reason=offline duration=0 endedBy=
✓ 走通：握手 → 拨号 → 终局。
```

这一趟覆盖了 `sys.hello` 握手、`call.invite` 请求应答、`call.ended` 事件分发、
以及连不上时的退避重连（`./scripts/smoke.sh` 指一个死端口会看到 1s/2s/4s 三次重试）。
**媒体一行都没有**——没有 SDP、没有 ICE、没有声音画面。

**验证到什么程度**（别夸大）：`./scripts/test.sh` 在 macOS 全绿，**58 个用例**；
ASan、UBSan、**TSan** 都干净。变异测试十一次，十次当场被抓；没抓住的那次
（不关 IX 自带的自动重连）已补测试补上。
**媒体只测了「什么时候该问媒体层要什么」**，没测「SDP 生成得对不对」——
后者要等真适配器，而真适配器要等机器。
**Windows 一次都没编译过**；媒体、设备、capi、Demo 都还没有。

**门面这一刀里测试抓到的三个真 bug**（都补了用例钉住）：

1. **断线时在途请求被当成了「这件事失败了」**——`call.invite` 在途时掉线，
   会在 onDisconnected 之前先冒一条 onCallEnd(error)，界面当场收场；
   而重连成功后那通电话其实还在。协议 §1.4 明说断开期间通话要保持。
   现在断线导致的失败一律放过，成不成由 `sys.hello.ok` 的 resumed 裁决。
2. **成功的应答没喂回状态机**——`room.join.ok` 被 Connection 当作请求的应答吃掉了，
   房间机永远停在 joining。现在 `.ok` 也回喂。
3. **`logout()` 抛了 onKickedOut**（真机 smoke 第一次跑就撞上）——用户自己点的退出，
   却收到「您的账号在别处登录」。顺带还多抛了 error:2005 + roomLeft：
   `close()` 结算在途请求时被当成了真失败。现在拆除期间连接层的东西一概不外传。


### 这几刀里测试抓到的 bug（都补了用例钉住）

| # | Bug | 后果 |
|---|---|---|
| 1 | 断线时在途请求被当成「这件事失败了」 | `call.invite` 在途掉线 → onDisconnected 之前先冒 `onCallEnd(error)`，界面当场收场；重连成功后那通电话其实还在。违反 §1.4 |
| 2 | 成功的应答没喂回状态机 | `room.join.ok` 被 Connection 当请求应答吃掉，房间机永远停在 joining |
| 3 | `logout()` 抛 onKickedOut | 用户自己点的退出，却收到「您的账号在别处登录」。真机 smoke 撞出来的 |
| 4 | 下行帧只从「事件」一条路接媒体面 | pub offer 的应答是 `room.answer`（没有 `.ok`），走的是应答那条路 → 上行协商永远完不成，且不报错 |
| 5 | 媒体面先动、事件后抛 | 采集失败的 onError 跑到 onRoomJoined 前面，宿主还不知道自己进了房 |

变异测试十一次，十次当场被抓。没抓住的那次是「不关 IXWebSocket 自带的自动重连」——
它只写在注释里没有测试守着，已补一条从行为上验的用例（连必然被拒的端口，1.5 秒里只该报一次 closed）。


---

# 2026-09-08 搬入：P5 各刀的落地明细（均已完成，test.sh 七步全绿）

**刚跑完一轮 `/code-review max` 并把结论落地了**（15 条，13 修 / 1 记为待办 / 1 判为文档错）。
其中五条是**测试当时全绿也照样存在**的：

- `room.mute` 发的是本端 cid 而不是服务端 track_id —— 本端停发了，房里其他人的
  麦克风图标永不变化，两端都不报错。（旧用例把错的值当成期望钉住了。）
- `logout()` 的 `tearingDown_` 只在**假**传输上成立：真 Transport 异步投递关闭事件，
  标记早清了，那条「已断开」照样弹给宿主。假件当时是同步回调的，所以测试看不见。
- 心跳判死晚一个周期（60s vs 协议 §1.3 的 45s）。
- join 还在飞时掉线，恢复后被直接宣布 `joined` —— 那个房间**再也出不去**。
- 应答只按 `req_id` 认、不校验类型；超时后才回来的 `.ok` 会被当成服务端主动事件，
  把状态机推回去。

另外三条是**闸门本身**的问题：600 行体量门禁**从来没扫过 `capi/`**（只扫 engine 与 demo），
`imrtc_v1_speaker` / `imrtc_v1_quality` 缺 `struct_size`（而它们恰恰是按**数组**交出去的），
IxTransport 的投递队列无上限、且在锁内做 socket IO。都已修。

**对外交付物已经成立**：`libim_rtc_engine_capi.dylib` + 一个 C 头，
**导出面只有 27 个 `imrtc_v1_*` 符号**（`scripts/check-abi.sh` 守着，已进 test.sh）。
联调工具 `scripts/smoke.sh` **改成经 C ABI 走**——与 Qt / C# 宿主同一条路，
对着真服务端跑通：握手 → 拨号 → `onCallEnd(offline)`。

**媒体已决定推迟（2026-09-06 定，见「已知坑」第一条）**：libwebrtc 桌面预编译包**没有 macOS x86_64**，
本机是 Intel Mac，所以第四刀只做了上半——`MediaAdapter` 接口 + `MediaPlane` 接线，用假适配器测全。
真正的 `WebRTCAdapter` **等 Apple Silicon 或 Windows 机器**再做。在那之前
**桌面端按纯信令模式交付**：能拨号、能进房、能收到全部状态回调，就是没有声音和画面。

**Qt 6.8.3 已装好**（2026-09-06，`~/Qt/6.8.3/macos`，占 1.8 GB）：
`aqt install-qt mac desktop 6.8.3 clang_64 --archives qtbase qtsvg qttranslations qttools`，
官方包是 universal（`x86_64 arm64`），Intel Mac 上正常。

**第六刀已落地**：`demo/` 四屏（登录 / 拨号 / 记录 / 设置）+ 通话浮窗四态 + 九宫格，
**经 C ABI 调引擎**。已对着真服务端多实例互打验过（2026-09-06）：

| 场景 | 验到了什么 |
|---|---|
| 1v1 `offline` | 拨不在线的人 → 记录「呼出 · 对方不在线」 |
| 1v1 `no_answer` | 振铃 30s 超时，两侧各落一条（被叫是「未接来电」） |
| 1v1 `hangup` | 真接通 → 计时 → 挂断，两侧 `connected=true`、时长一致 |
| 1v1 `busy` | 第三方拨通话中的人 → 他收 `onCallBusy`，被拨方落一条 `on_call_missed` |
| **群通话 4 人** | `onUserAccept`/`onUserEnter` 逐个到；**`invite_more` 中途加人**，被加者收到 `group=1` 的来电并进同一个房 |
| **群通话离场规则（协议 §4 规则 6）** | 主叫先走→只他自己 `ended{hangup}`，其余人继续；第二个人走；**最后一人也收 `ended`**。三人时长各 6 / 10 / 10，各算各的 |
| **会议房** | `POST /v1/rooms` 建房 → `POST /v1/rooms/{id}/tokens` 换票（273 字符 JWT）→ `room.join` → 双方互见 → `onRoomLeft`。**没有 `onCallEnd`**，会议房不产生通话记录 |

**时长是服务端给的**：1v1 那轮设了 6 秒挂断，记录里是 5 秒——正是不变量 I8 要防的那个差。
《接入指南》在 [docs/INTEGRATION_GUIDE.md](docs/INTEGRATION_GUIDE.md)。
`IMRTC_BUILD_DEMO` 默认 OFF——engine 与测试不依赖 Qt 这条不能破。

**渲染路径 A 的宿主侧已经走通**（九宫格 2026-09-06，1v1 2026-09-07）：
格子与 1v1 那一屏里都塞了真的原生子窗口（macOS `NSView*`），句柄递给引擎，
几何 / DPI / 层级 / 生命周期都有回归测试。1v1 是**两层**：远端铺满 + 本端小窗
160×90 压在右下角。**但引擎侧没通电**——没有媒体适配器时那两个方法直接返回。
等 `WebRTCAdapter` 落地就自动生效，Demo 这边不用改。`--fake-video` 能看到它。

**C ABI 加了两个符号**（2026-09-07，都是追加式，已有的一个没动，25 → 27）：

1. `imrtc_v1_attach_local_view(engine, handle)` —— **本端预览**。做 1v1 那一屏时
   才发现缺：`attachView` 是按 uid 找**远端**轨道的，而引擎不知道自己的 uid，
   本端画面也来自采集侧、根本没有 trackId。
2. `imrtc_v1_set_remote_layer(engine, uid, layer)` —— **层上界**（协议 §3.5）。
   底下全是现成的（`RoomMachine::updateLayer` + `remoteTracks` 记账早就有了），
   缺的只是门面与 C ABI 这两层。Web 端一直有，桌面端漏了。

**层上界这一条是唯一一个不用等媒体的**：`room.update_layer` 是纯信令帧。
已对真服务端验过（2026-09-07）——用 `rtc-cli` 的 `publishStub` 那条路造一个
只登记 Track、不做媒体协商的发布者占住会议房，桌面端进同一个房、收到
`onUserVideoAvailable`、报 `l`，服务端回 `.ok`（持房 18 秒 > 10 秒请求超时，
没有 2004，所以应答确实回来了）。Demo 里九宫格报 `l`、1v1 报 `h`。

**顺带在服务端查出一件事（已在 server 仓修完）**：非法 `max_layer` 服务端
**按设计不报错**——协议 §2.4 规则 6 规定枚举越界必须兜底（`max_layer` 兜到 `l`），
而这条兜底正是 §10「新增枚举值不算破坏兼容」的前提。所以那里**不能加校验**，
加了反而毁掉前向兼容。真正的问题只是兜底不留痕，server 仓已补一条 Warn 日志
（`fix/max-layer-validation` 分支），并把 1306 `layer_unavailable` 标成
**保留码、服务端不发**（它的原描述与规则 6 自相矛盾且零调用点）。

结论不变：桌面端仍要在 **C ABI 边界上自己挡**（`isValidLayer`，名单从
`imrtc::layers()` 读，不另抄一份）——但理由是「服务端按设计不会拒绝你，
所以这个同步返回值是宿主唯一的反馈」，**不是**「服务端漏了」。

**边界上还有一个洞（已知，未修）**：`probeMicrophone` / `startLocalPreview`
在 `CallEngine.h` 的公开面上，**但不在 27 个导出符号里**——走纯 C ABI 的宿主
（Qt / C#）根本调不到。原因是它们带 completion 回调，跨 C ABI 要另定形状，
**与渲染路径 B 是同一类问题**，该一起定。现在没人踩到是因为没有媒体适配器时
它们本来就是空操作。

**还没有的**：真实媒体（没有 SDP、没有 ICE、没有声音画面）、设备枚举、
渲染路径 B（原始帧回调）、共享屏幕、C# 绑定。**Windows 一次都没编译过。**

## 2026-09-08 ~ 09-09 的「当前焦点」（2026-09-11 从 current_task.md 移出）

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

---

## 2026-09-11 晚：来电页批次第 3 步（桌面）窗内来电横幅的「当前焦点」（同日从 current_task.md 移出）

**2026-09-11 晚：来电页批次第 3 步（桌面）——窗内来电横幅。未提交；`test.sh` 八步全绿（113 个引擎用例，Demo 界面测试 3 组，新增 `IncomingTest` 7 例）。**

UX_FLOWS §07 v3.7：来电先出**窗内横幅**，点横幅本体才换成来电浮层。
- **`demo/IncomingBanner`（新）**：贴主窗顶部居中，离顶与两侧 8、最宽 420、高 62、圆角 16、底 `#1E2330` + 阴影，规格同 Web `IncomingCall`。
  头像 38 + 名字 + 邀请语是**画**出来的（点它们 = 点本体 → `expandRequested`）；右边三颗 38 圆：摄像头（仅视频来电，禁用态）/ 拒绝 / 接听（恒 phone）。
  **横幅与按钮全是 `Qt::NoFocus`**，拨号页输入框的光标不会被抢。
- `ControlButton::setCompact`：38 圆、不画说明字，说明字转成 tooltip / 无障碍名。
- `callstrings::incomingInviteText`：横幅与浮层共用，与 Web 同一张表（群通话「邀请你加入群通话」）。
- `CallOverlay` 来电态：视频来电也显示摄像头（禁用，点了出提示）；标题栏留空。
- `MainWindow`：来电 → `beginIncoming` 备好浮层但不显示、只出横幅；点横幅 → 浮层；在横幅上接 → 接通时换浮层；
  振铃中结束 → 横幅收起、不弹结束态（与 Web 一致）；横幅开着时 `roomJoined` 不再弹浮层；提示条排到横幅下面。
- `Shots` 加 `06-call-incoming-banner`。

**本批顺序**（以 server 仓 `current_task.md` 为准）：① 设计稿 ✅ ② Web ✅ ③ 桌面 ✅（本条）④ Android ⑤ iOS ⑥ 三端进房前关摄像头停采集。桌面只有第 3 步。
2026-09-08 ~ 09-09 的焦点（leave_failed / 2006 放弃阈值 / CallEngine 拆分 / 上行协商闸门）已移到 archive。

---

## 2026-09-11 精简前全文（✅ 已完成项与冗长细节从 current_task.md 移出，原文照录）

# Current Task — im-rtc-desktop（C++17 Engine + C ABI + Qt Demo）

> **活快照**：只记当前状态，**就地覆盖、不追加**。历史见 `git log` 与 [current_task.archive.md](current_task.archive.md)。
> 工程规范见 [CONVENTIONS.md](CONVENTIONS.md)（**§2 是 C ABI 边界，本仓最重要的一条**）；
> 分期见 `im-rtc-server` 的 `docs/design/RTC_CALL_DESIGN.md` §8（桌面）与 §10（分期）。

## 当前焦点

**2026-09-11 晚：延后项 ②（桌面）——窗口在后台时的来电提醒。已提交；`test.sh` 八步全绿（113 个引擎用例，Demo 界面测试 4 组，新增 `IncomingAlertTest` 6 例）。**
**用户 2026-09-11 在 macOS 上两台实例手点验过（跳 Dock + 系统通知 + 点了展开）；Windows 未验证；本机 Intel 无媒体。**

UX_FLOWS §07 第 3 条：窗口在前台只出横幅；不在前台（最小化 / 非活动 / 被挡）再**跳 Dock / 闪任务栏 + 系统通知**，点了才前置。
- **`demo/IncomingAlert`（新）**：判据 `needsSystemAlert`（看不见 || 最小化 || 非活动窗口）+ 状态（`ring` / `clear` / `notificationClicked`）。
  系统那一侧经 `Sink` 注入——test.sh 跑真窗口，不注入就会真弹通知。**通话结束后才点到旧通知不再展开**。
- **`demo/SystemAlertSink`（新）**：通知走 `QSystemTrayIcon::showMessage`（点通知 / 托盘图标 → 前置），不引第三方库。
- **`demo/SystemAlertAttention_mac.mm` / `_win.cpp` / `_stub.cpp`**：macOS 调 `requestUserAttention:NSCriticalRequest` 并留请求号以便撤；
  Windows 调 `FlashWindowEx(FLASHW_ALL | FLASHW_TIMERNOFG)`、撤时 `FLASHW_STOP`（同日补，**只在 Mac 上对着 mingw-w64 头做过语法检查**）；
  其余平台暂用 `QApplication::alert`（撤不回来）。
- `MainWindow`：来电 `alert_->ring(presenceOf(this), …)`；接通 / 结束 / `hideOverlay`（登出、被踢、他端已处理）都 `clear()`；
  `openRequested` → 恢复最小化、`raise` + `activateWindow`、横幅还在就展开来电浮层。
- `.ts` 加 `SystemAlertSink` 一条（托盘 tooltip）。

**来电页批次**（以 server 仓 `current_task.md` 为准）桌面只有第 3 步（窗内横幅，已移到 archive）；本条是那批之后的延后项 ②。

---

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

- **本批之后的两刀（2026-09-11 用户定，等本批六步做完再动）**：
  ① 通话中关摄像头也停采集——桌面没接媒体，不涉及；
  ② ~~桌面窗口在后台时的来电提醒~~ —— 已做（见「当前焦点」）。`FlashWindowEx` 已写进 `_win.cpp`；剩：到 Windows 机器上编译，按「已知坑」里那四条手点验。
  §07 第 1 条（关窗语义）/ 第 2 条（托盘常驻 + 绿点 / 菜单）没排期。
- **设置页「详细日志」+ SDK 1.0.0**（2026-09-11 合入 `acede6a`，macOS 八步全绿）：Windows 上编译并点一次；
  `Shots` 的设置页截图（高度改到 680）没重新生成、没人看过。
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
5. **体量预警只剩两处**：`capi/src/imrtc_c.cpp` 538、`demo/MainWindow.cpp` 548
   （阈值 600，预警线 480）。`imrtc_c.cpp` 是 27 个回调各一段几乎一样的跳板，
   涨得最快，下一刀碰它之前先看行数。
6. **日志的下一步**：环形缓冲 + `exportDiagnostics()`（LOGGING.md §7 的 P3），
   给宿主做「报告问题」按钮用。四端都还没做，做的时候该一起定形状。

## 已知坑 / 限制

- **后台来电提醒的平台限制**（2026-09-11）：
  - **macOS 的系统通知撤不掉**：Qt 没有撤通知的接口，`withdraw()` 只能藏托盘图标；已进通知中心的那条会留着。
    通话结束后再点它只会激活应用，`IncomingAlert` 不再展开（有用例钉着）。Qt cocoa 走的是已废弃的 `NSUserNotificationCenter`。
  - **托盘 / 菜单栏图标只在响铃期间出现**：发通知必须先 `show()` 托盘图标。系统没托盘（部分 Linux）就只剩注意请求。
  - **Windows 未编译、未运行**：闪任务栏已换成 `FlashWindowEx`（`demo/SystemAlertAttention_win.cpp`），
    但只在 Mac 上用 mingw 目标对着 mingw-w64 头 + Qt 6.8.3 头做过 `-fsyntax-only`（`temp_verify.py`）。MSVC 没编过。
    到 Windows 上手点四条：① 窗口最小化 / 不是活动窗口时来电，任务栏按钮持续闪；② 对方取消后立刻停闪，按钮不留橙色高亮；
    ③ 闪着的时候切回窗口，系统自己停，之后接通 / 挂断不出错；④ 气泡靠藏托盘图标收起——也要一起看。
  - **「被挡住」只能按「不是活动窗口」判**：窗口是活动窗口但被别的 always-on-top 窗口盖住时，不会提醒。
  - **不在窗口激活时 `clear()`**：AppKit 在应用被激活时自己停跳 Dock；在激活时清会和「点通知」的回调抢先后，导致点了不展开。
  - **Demo 没有铃声**：看不见窗口时，提醒只有 Dock / 任务栏 + 通知。
- **横幅 / 后台提醒的接线没有集成测试**：`IncomingTest` 只测横幅与浮层两个控件，`IncomingAlertTest` 只测判据与状态（假 sink）；`MainWindow` 那几处
  （来电出横幅 / 点开换浮层 / 振铃中结束收横幅 / 横幅上接通换浮层 / 后台来电发通知 / 点通知前置）要真引擎，只能两台实例手点验证。
- **Demo 没有 Web 的 `bannerFirst` 开关**：桌面固定横幅先行。
- **SDK 版本号只改 `engine/include/imrtc/Version.h` 一处**（2026-09-11 五端统一 1.0.0）：`imrtc_v1_version()`、引擎 sdk 默认串、
  Demo 应用版本都读它；编译期常量，不增导出符号。**详细日志存 QSettings `log/verbose`，但环境变量 `IMRTC_LOG_LEVEL` 设了以它为准**——
  脚本联调时界面勾选「不生效」是这个原因，不是坏了。
- **横幅用了 `QGraphicsDropShadowEffect`**：它会把整块横幅离屏再画一遍——**别往横幅里放原生子窗口**
  （本端预览之类），原生层绕过这个效果，会画到阴影外面去。
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
