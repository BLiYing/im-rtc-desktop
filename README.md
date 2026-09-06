# im-rtc-desktop

`im-rtc` 音视频产品的**桌面端**：Windows + macOS **共用一套 C++17 代码**。

> **不只服务 Qt 宿主**（2026-09-06 定）：engine **零 Qt 依赖**，对外只导出**纯 C ABI 动态库**——
> Qt / MFC / WPF+C# / Delphi / Java / Python / Flutter / Swift 都能接。Qt 只出现在本仓的 Demo 里。

| 产物 | 是什么 |
|---|---|
| **`im_rtc_engine`** | **无 UI、零 Qt 依赖**的 C++17 Engine：信令 / 状态机 / 媒体（libwebrtc）/ 设备 |
| **C ABI 动态库** | **对外唯一边界**：`imrtc_v1_*`，libwebrtc 静态链在内。`.dll` / `.dylib` + 一个 C 头 |
| **C++ 包装头** | header-only RAII 包装，**自己也走 C ABI**，给 Qt / MFC 宿主用着顺手 |
| **Qt Demo** | 登录 / 拨号 / 通话记录 / 通话界面，**经 C ABI 调引擎**，验证 Engine 够用 |

## 为什么单独有这个仓

主流厂商（腾讯等）**没有桌面版含 UI 通话组件**。桌面端只能「媒体用 libwebrtc + 信令接自己的协议」——
这正是整个方案选择**自建信令 + 自建 SFU** 的直接原因之一。

## 跨平台策略（已定）

五端**不共享代码，共享「协议 + 状态机 + 一致性测试向量」**。
iOS 用 Swift、Web 用 TS、桌面用 C++17、Android 用 Kotlin，靠 `im-rtc-server/docs/conformance/*.json` 钉死行为一致。

**桌面为什么必须是 C++17**：唯一可用的媒体栈 libwebrtc 是 C++ 且不提供 C ABI，这一层跑不掉。
Rust 要另写一层 C++ shim 才能桥 libwebrtc，是净负担。

**「支持更多宿主」靠的是边界形态，不是语言**：C++ 没有跨编译器 ABI
（MSVC↔MinGW、`/MD`↔`/MT`、Debug↔Release CRT 都不兼容），所以对外只能是 C ABI。
细节见设计文档 §8 与本仓 [CONVENTIONS.md](CONVENTIONS.md) §2。

## 边界

**不做集成方的业务界面**：公司 Qt 项目有自己的视觉体系，UI 由他们自画。
本仓的 Qt Demo 是**参考实现**。

**Electron / CEF / Tauri 宿主不接本仓**：它们的渲染进程自带完整 WebRTC，
直接用 [im-rtc-web](https://github.com/BLiYing/im-rtc-web) 的 `@im-rtc/call-engine`（那边已经跑通）。

## 文档

| 文档 | 内容 |
|---|---|
| [CLAUDE.md](CLAUDE.md) | 项目说明、结构、工作流程与「完成的定义」 |
| [CONVENTIONS.md](CONVENTIONS.md) | 工程规范（分层 / **§2 C ABI 边界** / 体量 / RAII / 线程 / 跨平台 / 测试） |
| [current_task.md](current_task.md) | 当前进度活快照 |
| 协议契约 | 在 [im-rtc-server](https://github.com/BLiYing/im-rtc-server) 的 `docs/RTC_PROTOCOL.md`，本仓只读引用 |

## 依赖（锁死版本，两平台同一版本号）

| 依赖 | 版本 | 许可 | 谁用 |
|---|---|---|---|
| [IXWebSocket](https://github.com/machinezone/IXWebSocket) | **v12.0.1** | BSD-3-Clause | 只有 `transport/` 用。TLS 走平台自带：macOS SecureTransport、Windows mbedTLS |

**engine 与全部测试零第三方依赖**——它们用的是假 Transport。
FetchContent 在**配置期**下载 IXWebSocket；离线时：

```bash
cmake --preset macos-clang -DIMRTC_WITH_IX_TRANSPORT=OFF
```

engine 与 31 个用例照样能编能跑（少掉的 5 个是 IxTransport 的契约测试）。

## 开发

```bash
./scripts/install-hooks.sh                                  # 新 clone 跑一次
./scripts/test.sh                                           # 唯一入口：体量 + 配置 + 编译 + 单测
```

一致性向量**只读 `im-rtc-server/docs/conformance/` 那一份**（默认按同级目录找，
也可以设 `RTC_CONFORMANCE_DIR`）。**禁止拷贝一份进本仓**——一拷贝就会漏同步。

**本机只有 macOS**：Windows 侧编译与验证需要集成方配合。
不许把「macOS 过了」写成「桌面端完成」。
最低系统：**Windows 10+ / macOS 11+**；macOS 产物是 x86_64 + arm64 universal。

## 状态

**P5 进行中（2026-09-06 开工）。** 已落地：

- **协议层**：JSON（数字按值判定）、信封、§2.4 编码硬规则、声明式帧表、41 个帧类型
- **状态机**：通话（§5.1）+ 房间（§5.3）+ 只有合起来才说得清的那一层
- **信令连接**：握手、心跳、按 `req_id` 配对、超时、退避重连、关闭码处置。
  **不持有定时器**（时间由 `tick(nowMs)` 喂），socket 藏在 `Transport` 接口后
- **真实 WS Transport**：IXWebSocket v12.0.1，回调跨线程投递到宿主线程
- **门面 `CallEngine`**：§7.5 的回调总表，宿主实现 `CallEngineObserver` 就能自画 UI
- **媒体面接线**：`MediaAdapter` 契约 + `MediaPlane`（进房推流、SDP 填充、候选双向、
  媒体就绪、终局归零）。**真适配器还没有**——见上面的 libwebrtc 平台问题
- **C ABI 交付物**：`libim_rtc_engine_capi.dylib` + `imrtc_c.h` + header-only C++ 包装。
  **导出面只有 25 个 `imrtc_v1_*` 符号**，`scripts/check-abi.sh` 守着（已进 test.sh）
- **Qt 6 Demo**：四屏（登录 / 拨号 / 记录 / 设置）+ 通话浮窗四态 + 九宫格，
  **经 C ABI 调引擎**，与集成方同一条路。默认不构建（`IMRTC_BUILD_DEMO=OFF`）——
  engine 与测试不该因为一个 Demo 就依赖 Qt
- **《接入指南》**：[docs/INTEGRATION_GUIDE.md](docs/INTEGRATION_GUIDE.md)，
  给「要把它装进自己 Windows / macOS 应用」的人看，不必读引擎源码

约 10100 行 C++17。`./scripts/test.sh` **65 个用例全绿**（macOS），
ASan / UBSan / TSan 都干净。

**已经对着真服务端跑通一整轮，且全程经 C ABI**（与 Qt / C# 宿主同一条路）：
握手 → 拨号 → `onCallEnd(offline)`。

```bash
cd ../im-rtc-server && ./scripts/dev.sh     # 起本地服务端
cd -                && ./scripts/smoke.sh   # 跑一轮（命令行）
./scripts/demo.sh alice bob                 # 跑一轮（Qt Demo，要装 Qt 6）
./scripts/demo-shots.sh                     # 不要服务端，把各界面态渲染成 PNG
```

装 Qt 6（macOS，最小集，实测 1.8 GB）：

```bash
pip install aqtinstall && aqt install-qt mac desktop 6.8.3 clang_64 --archives qtbase qtsvg qttranslations qttools -O ~/Qt
```

**还没有的**：真实媒体实现（`WebRTCAdapter`，已决定推迟等机器）、设备枚举、共享屏幕。
**Windows 一次都没编译过。**
逐层状态见 `im-rtc-server/docs/CLIENT_PARITY.md` §1.1，本文不重复。
