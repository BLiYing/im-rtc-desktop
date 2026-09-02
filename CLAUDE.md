# im-rtc-desktop — 项目说明（供 Claude 读取）

## 项目简介
`im-rtc` 音视频产品的 **桌面端**（Windows + macOS **共用一套 C++/Qt 代码**）。交付两样东西：

| 产物 | 是什么 | 谁用 |
|---|---|---|
| **`im_rtc_engine_cpp`** | **无 UI** C++17 Engine 参考实现：信令、通话状态机、媒体（libwebrtc）、设备控制，能力通过**回调**暴露 | 已有 Qt 项目的团队 |
| **Qt Demo** | 登录 / 拨号 / 通话记录 / 通话界面，验证 Engine 够用 | 集成方对照参考 |

**为什么单独有这个仓**：公司现有 Windows/Mac 项目共用一套 **Qt** 代码。
腾讯等厂商**没有桌面版含 UI 通话组件**，所以桌面端只能「媒体用 libwebrtc + 信令接我们自己的协议」——
这正是整个方案选择**自建信令 + 自建 SFU** 的直接原因之一。

**跨平台策略（已定，别再翻案）**：四端**不共享代码，共享「协议 + 状态机 + 测试向量」**。
iOS 用 Swift、Web 用 TS、桌面用 C++17，各写各的，靠 `im-rtc-server/docs/conformance/*.json`
的一致性向量钉死行为一致。真要选一门统一语言，答案是 C++17（Qt 与 libwebrtc 都是它），
但在「iOS 独立 + Web 必 TS + 桌面本就 C++」的组合下，Rust/KMP 都是净负担。

## 技术栈
- 语言：**C++17**；GUI **Qt 6**（Qt 5.15 兼容视情况保留）
- 媒体：**libwebrtc 预编译包**（`shiguredo-webrtc-build`，或自建），原生 C++ API
- 信令：`QWebSocket`（或独立 WS 库），JSON
- 构建：**CMake**（`CMakePresets.json` 覆盖 Windows/macOS 两套工具链）
- 测试：Catch2 或 GoogleTest（P5 定）

## 工程结构（规划，落地时按此展开）
```
im-rtc-desktop/
├── CMakeLists.txt
├── CMakePresets.json                  # windows-msvc / macos-clang 两套预设
├── engine/                            # 无 UI，不依赖 Qt Widgets
│   ├── include/imrtc/                 # 公开头文件（对外 API 就是这些）
│   │   ├── CallEngine.h               # 门面：login/call/accept/hangup/joinRoom…
│   │   └── CallEngineObserver.h       # 回调接口（对应设计文档 §7.5 回调总表）
│   └── src/
│       ├── signaling/                 # WS 客户端 + 帧编解码 + 重连退避
│       ├── state/                     # 通话与房间状态机（纯逻辑、跑一致性向量）
│       ├── media/                     # MediaAdapter 接口 + WebRTCAdapter（libwebrtc）
│       └── devices/                   # 麦克风/摄像头/扬声器枚举与切换
├── demo/                              # Qt Demo：登录 / 拨号 / 通话记录 / 通话界面
├── tests/                             # 状态机 + 帧编解码 + 一致性向量
└── scripts/                           # 门禁与测试入口
```

## 工作约定
- **每次开始主要回复前，先读 `current_task.md` 恢复上下文**，改动后更新它。
- **`current_task.md` 是「活快照」不是流水账**：固定四节，**就地覆盖、禁止追加 Status 块**。
- **工程规范见 [CONVENTIONS.md](CONVENTIONS.md)**（分层 / 体量 / RAII / 线程 / 日志 / 跨平台 / 测试）。
- **协议契约在 `im-rtc-server/docs/RTC_PROTOCOL.md`，本仓只读引用**，不得单方面加字段。
  改协议 = 改四个仓 + 同步一致性向量。
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
5. **两个平台都要说清楚状态**：只在 macOS 编译过就明说 Windows 未验证。
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
| **im-rtc-desktop**（本仓） | C++17 Engine + Qt Demo |

**本仓在分期里是 P5**，排在协议、SFU、Web、iOS 之后。
在此之前它的价值是**接收契约**：协议与一致性向量必须从第一天就考虑 C++ 端能实现
（例如不用 JS/Swift 特有的数据结构表达帧）。
