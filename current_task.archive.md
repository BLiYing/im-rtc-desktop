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
