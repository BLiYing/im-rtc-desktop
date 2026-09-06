# im-rtc-desktop — 项目说明（供 Claude 读取）

## 项目简介
`im-rtc` 音视频产品的 **桌面端**（Windows + macOS **共用一套 C++17 代码**）。

**2026-09-06 定的边界（重要）**：engine **零 Qt 依赖**，对外只导出**纯 C ABI 动态库**。
Qt 从此只是众多宿主之一——MFC / WPF+C# / Delphi / Java / Python / Flutter / Swift 都能接。
理由与红线见 [CONVENTIONS.md](CONVENTIONS.md) §2 与设计文档 §8。

| 产物 | 是什么 | 谁用 |
|---|---|---|
| **`im_rtc_engine`** | **无 UI、零 Qt 依赖**的 C++17 Engine 参考实现：信令、通话状态机、媒体（libwebrtc）、设备控制 | 内部 |
| **C ABI 动态库** | **对外唯一边界**：`imrtc_v1_*`，libwebrtc 静态链在内，`.dll` / `.dylib` + 一个 C 头 | 所有宿主 |
| **C++ 包装头** | header-only RAII，**自己也走 C ABI** | Qt / MFC 项目 |
| **Qt Demo** | 登录 / 拨号 / 通话记录 / 通话界面，**经 C ABI 调引擎**，验证 Engine 够用 | 集成方对照参考 |

**为什么单独有这个仓**：公司现有 Windows/Mac 项目共用一套 **Qt** 代码（但交付面不绑 Qt，见上）。
腾讯等厂商**没有桌面版含 UI 通话组件**，所以桌面端只能「媒体用 libwebrtc + 信令接我们自己的协议」——
这正是整个方案选择**自建信令 + 自建 SFU** 的直接原因之一。

**跨平台策略（已定，别再翻案）**：五端**不共享代码，共享「协议 + 状态机 + 测试向量」**。
iOS 用 Swift、Web 用 TS、桌面用 C++17、Android 用 Kotlin，各写各的，靠 `im-rtc-server/docs/conformance/*.json`
的一致性向量钉死行为一致。真要选一门统一语言，答案是 C++17（**libwebrtc 是 C++ 且不提供 C ABI**，这一层跑不掉），
但在「iOS 独立 + Web 必 TS + 桌面本就 C++」的组合下，Rust/KMP 都是净负担。
**注意别把这句读成「所以只服务 C++/Qt 宿主」**：核心语言是 C++17，**交付边界是 C**。

## 技术栈
- 语言：**C++17**。**engine 零 Qt 依赖（含 QtCore）**；Qt 6 只用于 `demo/`
  （于是「是否兼容 Qt 5.15」这个悬案作废——宿主用 Qt 几与引擎无关）
- 对外边界：**纯 C ABI 动态库**，符号 `imrtc_v1_*`，`visibility=hidden`
- 媒体：**libwebrtc 预编译包**（`shiguredo-webrtc-build`，或自建），原生 C++ API，**静态链进动态库内部**
- 信令：**IXWebSocket v12.0.1**（BSD-3-Clause，**不用 QWebSocket**），锁死版本。
  TLS 走平台自带（macOS SecureTransport / Windows mbedTLS）——**不是**原先设想的
  「复用 libwebrtc 的 BoringSSL」，那条路要换 Boost.Beast 才走得通，代价是引入 boost。
  信令是低频小帧，包里有两份 TLS 实现这个代价可以接受；决定见 current_task
- 构建：**CMake**（`CMakePresets.json` 覆盖 Windows/macOS 两套工具链）
- 测试：Catch2 或 GoogleTest（P5 定）+ C ABI 冒烟 + Python ctypes 跑一致性向量
- 目标系统：**Windows 10+ / macOS 11+**；macOS 产物 x86_64 + arm64 universal

## 工程结构（`*` = 已落地，其余是规划）
```
im-rtc-desktop/
├── CMakeLists.txt *                   # 顶层：C++17、visibility=hidden、-Wconversion
├── CMakePresets.json *                # macos-clang / macos-clang-release / windows-msvc
├── engine/ *                          # 无 UI，**零 Qt 依赖**
│   ├── include/imrtc/ *               # 引擎内部公开头（**不是对外交付面**，对外见 capi/）
│   │   ├── Json.h Errors.h Enums.h Reasons.h *
│   │   ├── Envelope.h FieldSpec.h Frames.h Registry.h Transport.h Connection.h *
│   │   ├── MachineTypes.h CallMachine.h RoomMachine.h EngineMachine.h *
│   │   └── CallEngine.h CallEngineObserver.h *   # **门面与 §7.5 回调总表**
│   └── src/
│       ├── json/ *                    # 手写 JSON：数字**按值**判定（1e3 是整数、15e-1 不是）
│       ├── signaling/ *               # 信封 + 编码硬规则 + 声明式帧表 + 注册表
│       │   ├── Connection *           # 握手/心跳/应答配对/退避重连。**不持有定时器**
│       │   └── （Transport 接口在 engine/include/imrtc/Transport.h）
├── transport/ *                       # **唯一需要第三方依赖的目标**（IXWebSocket）
│   └── src/IxTransport.cpp *          # 回调跨线程投递：IX 在自己的线程收帧，poll() 里放出来
│       ├── state/ *                   # 通话机 / 房间机 / 合成层，纯逻辑、跑一致性向量
│       ├── CallEngine.cpp *           # 门面：宿主方法 ↔ 状态机 ↔ 连接。**时钟在这里收口**
│       ├── CallEngineEvents.cpp *     # 回调名 → 观察者方法的纯映射表
│       ├── media/                     # MediaAdapter 接口 + WebRTCAdapter（P5 第四刀）
│       └── devices/                   # 麦克风/摄像头/扬声器枚举与切换（P5 第四刀）
├── capi/                              # **对外唯一边界**（P5 第五刀）
│   ├── include/imrtc/imrtc_c.h        # 纯 C 头：不透明句柄 + POD + 函数指针回调
│   ├── include/imrtc/CallEngine.hpp   # header-only C++ RAII 包装（自己也走 C ABI）
│   └── src/                           # C++ → C 的转换层（异常在这里被吃掉转错误码）
├── tools/ *                           # 联调工具（需要真服务端，不进 test.sh）
│   └── Smoke.cpp *                    # 握手 → 拨号 → 终局，跑一轮给人看
├── demo/                              # Qt 6 Demo，**经 capi 调引擎**（P5 第六刀）
├── tests/ *                           # 自制 harness（120 行）+ 五份向量的 runner
└── scripts/ *                         # 门禁与测试入口
```

## 工作约定
- **每次开始主要回复前，先读 `current_task.md` 恢复上下文**，改动后更新它。
- **`current_task.md` 是「活快照」不是流水账**：固定四节，**就地覆盖、禁止追加 Status 块**。
- **工程规范见 [CONVENTIONS.md](CONVENTIONS.md)**（分层 / 体量 / RAII / 线程 / 日志 / 跨平台 / 测试）。
- **engine 里出现任何 `Q` 开头的类型 = 直接打回**（见 CONVENTIONS §1）；
  **C 头里出现 `std::` / 虚函数 / libwebrtc / Qt 类型 = 直接打回**（见 CONVENTIONS §2）。
- **协议契约在 `im-rtc-server/docs/RTC_PROTOCOL.md`，本仓只读引用**，不得单方面加字段。
  改协议 = 改五个仓 + 同步一致性向量。
- **单文件体量红线**：非测试 `.cpp`/`.h` **> 600 行**要按职责拆分。
  硬闸：`scripts/check-file-size.sh`（pre-commit + `test.sh` 第 1 步）。新 clone 跑 `./scripts/install-hooks.sh`。
- 文档引用代码**不写行号**，写文件路径 + 符号名：`engine/src/media/WebRTCAdapter.cpp` 的 `attachView()`。

## 工作流程与「完成的定义」
动手前（Read，不靠记忆）：
- 改代码前先 Read [CONVENTIONS.md](CONVENTIONS.md)；涉及协议字段再 Read `../im-rtc-server/docs/RTC_PROTOCOL.md`。
- 加/改**公开 API** 前，先 Read 设计文档 §7.5 回调总表——**回调名四端同名**。

声明「完成」前必须全部满足，并在回复中**贴出 `./scripts/test.sh` 的输出**：
1. 新功能配套单测，由测试目标自动纳入。
2. `./scripts/test.sh` 全绿（体量门禁 + CMake 配置 + 编译 + 单测）。
3. 更新 `current_task.md`；里程碑完成同步更新 server 仓设计文档 §10 的状态与日期（YYYY-MM-DD）。
4. 明确说清楚「没做什么 / 已知限制 / TODO」，不假装完成。
5. **C ABI 冒烟测试要过**（create → 注册回调 → 调用 → destroy，ASan 干净），
   且 Qt Demo **确实是经 capi 调的引擎**——走内部 C++ 接口会掩盖全部 ABI 问题。
6. **两个平台都要说清楚状态**：只在 macOS 编译过就明说 Windows 未验证。
   本机是 macOS，**Windows 侧验证需要集成方配合**——不许把「macOS 过了」写成「桌面端完成」。

主动建议（不必用户开口）：
- 完成较大功能后建议跑 `/code-review` 自审。
- 触及 token / 权限 / 媒体密钥时建议跑 `/security-review`。

## 构建 / 测试
```bash
./scripts/install-hooks.sh                     # 新 clone 跑一次
cmake --preset macos-clang && cmake --build --preset macos-clang
./scripts/test.sh                              # 唯一测试入口：体量 + 配置 + 编译 + 单测
```
> 脚本与 CMake 工程随 P5 落地补齐；当前仓库只有文档与体量门禁。

## 关联仓库
| 仓库 | 内容 |
|---|---|
| [im-rtc-server](https://github.com/BLiYing/im-rtc-server) | 控制面 + SFU + **协议契约**（本仓只读引用） |
| [im-rtc-ios](https://github.com/BLiYing/im-rtc-ios) | Engine + Kit + Demo（Swift） |
| [im-rtc-web](https://github.com/BLiYing/im-rtc-web) | engine + uikit + Demo（TS/React） |
| **im-rtc-desktop**（本仓） | C++17 Engine（零 Qt）+ C ABI 动态库 + Qt Demo |
| [im-rtc-android](https://github.com/BLiYing/im-rtc-android) | Engine + UIKit + Demo（Kotlin） |

**本仓在分期里是 P5**，排在协议、SFU、Web、iOS 之后。
在此之前它的价值是**接收契约**：协议与一致性向量必须从第一天就考虑 C++ 端能实现
（例如不用 JS/Swift 特有的数据结构表达帧）。
