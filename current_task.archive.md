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
