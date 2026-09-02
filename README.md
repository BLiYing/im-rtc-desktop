# im-rtc-desktop

`im-rtc` 音视频产品的**桌面端**：Windows + macOS **共用一套 C++17 / Qt 代码**。

| 产物 | 是什么 |
|---|---|
| **`im_rtc_engine_cpp`** | **无 UI** C++17 Engine：信令 / 状态机 / 媒体（libwebrtc）/ 设备，能力通过**回调**暴露 |
| **Qt Demo** | 登录 / 拨号 / 通话记录 / 通话界面，验证 Engine 够用 |

## 为什么单独有这个仓

主流厂商（腾讯等）**没有桌面版含 UI 通话组件**。桌面端只能「媒体用 libwebrtc + 信令接自己的协议」——
这正是整个方案选择**自建信令 + 自建 SFU** 的直接原因之一。

## 跨平台策略（已定）

四端**不共享代码，共享「协议 + 状态机 + 一致性测试向量」**。
iOS 用 Swift、Web 用 TS、桌面用 C++17，靠 `im-rtc-server/docs/conformance/*.json` 钉死行为一致。

## 边界

**不做集成方的业务界面**：公司 Qt 项目有自己的视觉体系，UI 由他们自画。
本仓的 Qt Demo 是**参考实现**。

## 文档

| 文档 | 内容 |
|---|---|
| [CLAUDE.md](CLAUDE.md) | 项目说明、结构、工作流程与「完成的定义」 |
| [CONVENTIONS.md](CONVENTIONS.md) | 工程规范（分层 / 体量 / **RAII** / 线程 / **跨平台** / 测试） |
| [current_task.md](current_task.md) | 当前进度活快照 |
| 协议契约 | 在 [im-rtc-server](https://github.com/BLiYing/im-rtc-server) 的 `docs/RTC_PROTOCOL.md`，本仓只读引用 |

## 开发

```bash
./scripts/install-hooks.sh
cmake --preset macos-clang && cmake --build --preset macos-clang
./scripts/test.sh
```

**本机只有 macOS**：Windows 侧编译与验证需要集成方配合。
不许把「macOS 过了」写成「桌面端完成」。

## 状态

**P5，尚未开工**（排在协议 → SFU → Web → iOS → 群通话之后）。
当前阶段本仓的价值是**接收契约**：协议设计必须从第一天就考虑 C++ 端能实现。
