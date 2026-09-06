# Current Task — im-rtc-desktop（C++17 Engine + C ABI + Qt Demo）

> **活快照**：只记当前状态，**就地覆盖、不追加**。历史见 `git log` 与 [current_task.archive.md](current_task.archive.md)。
> 工程规范见 [CONVENTIONS.md](CONVENTIONS.md)（**§2 是 C ABI 边界，本仓最重要的一条**）；
> 分期见 `im-rtc-server` 的 `docs/design/RTC_CALL_DESIGN.md` §8（桌面）与 §10（分期）。

## 当前焦点

**P5 已开工（2026-09-06）。第一~三刀落地：CMake 骨架 + 协议层 + 两台状态机 + 信令连接层。**
约 6000 行 C++17，**零第三方依赖**——一份 CMake + 一个编译器就能配置、编译、跑测试，
不需要 Qt、不需要 libwebrtc、不需要网络。

已落地：

| 层 | 文件 | 做了什么 |
|---|---|---|
| JSON | `engine/src/json/` | 手写解析器。**数字按值判定**（`1e3` 是整数、`15e-1` 不是），能表达非法值好让上层拒绝 |
| 信封 | `signaling/Envelope.cpp` · `Discipline.cpp` | §2.1 四字段；§2.4 七条硬规则里能脱离帧定义判的四条 |
| 帧 | `signaling/FieldSpec.cpp` · `Frames*.cpp` · `Registry.cpp` | 声明式字段表 + 默认值填充 + 枚举兜底 + 41 个帧类型 |
| 状态机 | `state/CallMachine` · `CallRecv` · `RoomMachine` · `RoomRecv` · `EngineMachine` | §5.1 通话机、§5.3 房间机，以及只有合起来才说得清的四件事 |
| 连接 | `signaling/Connection` · `Heartbeat` · `PendingRequests` · `Backoff` | 握手、心跳、请求应答配对、超时、退避重连、关闭码处置 |
| 测试 | `tests/` | 自制 harness + 五份向量的 runner + 假 Transport 的时序测试 |

**连接层的三个设计取舍**（决定了它今天就能测）：

1. **不持有定时器，时间从外面喂**（`tick(nowMs)`）。于是「45 秒静默判死」「10 秒请求超时」
   「退避到第 6 档」不用真的等就能测，也不必在 engine 里塞一个事件循环——宿主的事件循环
   长什么样我们不知道。
2. **按 `req_id` 配对，不按帧类型**。pub 侧的 `room.offer` 是由 `room.answer` 应答的（§3.3）。
3. **socket 藏在 `Transport` 接口后面**。engine 零 Qt，也不该把某个 WS 库焊死在信令逻辑里；
   换库只动一个实现文件。

**验证到什么程度**（别夸大）：`./scripts/test.sh` 在 macOS 全绿，**31 个用例**；
ASan + UBSan 干净；五次故意破坏（I7 便利回调、R2 意图缓存、hello 丢 session_id、4403 也重连、
心跳只认 pong）都被当场抓住。
**Windows 一次都没编译过**；**还没有真的 WS 实现**（只有接口与假实现），媒体、设备、capi、Demo 都还没有。

## 下一步

1. **接一个真的 WS 实现**（`Transport` 的唯一实现类）。**这是个待拍板的选型**：
   IXWebSocket（小、好接、Apache-2.0）vs Boost.Beast（重、但很多项目已经有 boost）
   vs 自己写。选定后要锁版本写进 README，并决定 vendor 还是 FetchContent。
   接完就能**无 GUI 连真服务端跑进房离房**——本地联调从这里开始。
2. **门面 `CallEngine`**：把 Connection 与 EngineMachine 缝起来（帧 → 回调、宿主调用 → 帧）。
   这一步不需要第三方库，可以先于上一条做。
3. **P5 第四刀 · 媒体**：`MediaAdapter` 接口 + `WebRTCAdapter`（libwebrtc C++ API）+ 1v1 语音/视频，
   与 Web/iOS 各互打一次。**这一刀开始需要 libwebrtc 预编译包，要先锁版本号。**
4. **P5 第五刀 · capi**：C 头定形 + 转换层 + header-only C++ 包装 + **ABI 冒烟测试（ASan）**。
5. **P5 第六刀 · Qt Demo**：四屏 + 《接入指南》，**经 capi 调引擎**。这一刀才需要装 Qt。

## 已知坑 / 限制

- **本机只有 macOS**：Windows 侧编译与验证需要集成方配合，时间未定。
  **不许把「macOS 过了」写成「桌面端完成」**——每次交付分平台说清楚。
- **Demo 必须经 capi 调引擎**。走内部 C++ 接口会掩盖全部 ABI 问题，那样「Demo 跑通」不等于「宿主接得通」。
- **`engine/` 里出现任何 `Q` 开头的类型 = 直接打回**（CONVENTIONS §1）。WS 换独立库，
  TLS 复用 libwebrtc 自带的 BoringSSL，不再引第二份 OpenSSL。
- **C ABI 的头号崩因是生命周期**：`destroy` 必须阻塞到所有回调线程静默；宿主是 C#/Java 这类
  GC 语言时它没法帮你保活。见 CONVENTIONS §5。
- **回调线程是宿主的责任**：engine 零 Qt，不认识宿主的事件循环，**切 UI 线程由宿主自己做**。
- **状态机与连接层都不读时钟**：状态机是不变量 I4 的要求，连接层是为了可测。
  `reduceEngine(ctx, input, nowMs)` 与 `Connection::tick(nowMs)` 的时间都由调用方喂。
  将来门面里谁来喂这个时钟（宿主线程还是 engine 自己起一条）要一并想清楚。
- **`Connection` 不是线程安全的**：所有方法（含 `tick`）要在同一个线程上调用，
  Transport 的回调也必须投递到那个线程。这条要写进《接入指南》。
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
