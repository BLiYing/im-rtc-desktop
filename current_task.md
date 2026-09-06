# Current Task — im-rtc-desktop（C++17 Engine + C ABI + Qt Demo）

> **活快照**：只记当前状态，**就地覆盖、不追加**。历史见 `git log` 与 [current_task.archive.md](current_task.archive.md)。
> 工程规范见 [CONVENTIONS.md](CONVENTIONS.md)（**§2 是 C ABI 边界，本仓最重要的一条**）；
> 分期见 `im-rtc-server` 的 `docs/design/RTC_CALL_DESIGN.md` §8（桌面）与 §10（分期）。

## 当前焦点

**P5 已开工（2026-09-06）。第一~三刀 + 门面全部落地，并且——**
**已经对着真服务端跑通了一整轮：握手 → 拨号 → `onCallEnd(offline)`。**
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
| 联调工具 | `tools/Smoke.cpp` · `scripts/smoke.sh` | 对着真服务端跑一轮，**不进 test.sh** |
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

**验证到什么程度**（别夸大）：`./scripts/test.sh` 在 macOS 全绿，**45 个用例**；
ASan、UBSan、**TSan** 都干净。变异测试八次，七次当场被抓；没抓住的那次
（不关 IX 自带的自动重连）已补测试补上。
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

## 下一步

1. **P5 第四刀 · 媒体**：`MediaAdapter` 接口 + `WebRTCAdapter`（libwebrtc C++ API）+ 1v1 语音/视频，
   与 Web/iOS 各互打一次。**这一刀开始需要 libwebrtc 预编译包，要先锁版本号。**
2. **P5 第五刀 · capi**：C 头定形 + 转换层 + header-only C++ 包装 + **ABI 冒烟测试（ASan）**。
3. **P5 第六刀 · Qt Demo**：四屏 + 《接入指南》，**经 capi 调引擎**。这一刀才需要装 Qt。

## 已知坑 / 限制

- **本机只有 macOS**：Windows 侧编译与验证需要集成方配合，时间未定。
  **不许把「macOS 过了」写成「桌面端完成」**——每次交付分平台说清楚。
- **Demo 必须经 capi 调引擎**。走内部 C++ 接口会掩盖全部 ABI 问题，那样「Demo 跑通」不等于「宿主接得通」。
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
