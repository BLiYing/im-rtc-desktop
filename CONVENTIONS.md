# CONVENTIONS —— im-rtc-desktop 工程规范（C++17 / Qt）

> 本文是**本仓代码的硬约束**。`CLAUDE.md` 讲「这个项目是什么」，本文讲「代码必须长什么样」。
> 协议字段与回调命名以 `im-rtc-server/docs/RTC_PROTOCOL.md` 与设计文档 §7.5 为准，本文不重复。

## 1. 分层与目标划分

```
engine/   无 UI。**禁止依赖 Qt Widgets / QML**（QtCore 的 QWebSocket 可用，或换独立 WS 库）。
demo/     Qt UI。依赖 engine，只通过公开回调获取信息。
tests/    单测。依赖 engine。
```

**依赖方向单向**：`demo → engine`。**engine 绝不反向依赖 demo**。
engine 内部：`CallEngine（门面）→ signaling / state / media / devices`，
子模块通过抽象接口解耦（纯虚基类），不互相 include 具体实现头。

**公开面就是 `engine/include/imrtc/` 下的头文件**。这些头：
- 只 include 标准库与本目录内的头，**不得泄漏 libwebrtc / Qt 的类型**；
- 用 pimpl 隐藏实现细节，保证 ABI 与编译期解耦；
- 每个公开类型有 doc comment。

**新增东西放哪**：
| 新增 | 放哪 | 不要放哪 |
|---|---|---|
| 一个新回调 | `include/imrtc/CallEngineObserver.h` + 设计文档 §7.5 同步 | 加个 `std::function` 成员了事 |
| 一个新信令帧 | `src/signaling/frames.{h,cpp}` + 状态机分支 | 在 WS 回调里就地解析 |
| 一个新界面 | `demo/<场景>/` 独立文件 | 往 MainWindow 里塞 |
| 媒体能力 | `src/media/`，经 `MediaAdapter` 接口暴露 | demo 里直接调 libwebrtc |

## 2. 文件体量红线（防「上帝类」）

- 非测试 `.cpp` / `.h` **> 600 行**即失败。
- 硬闸：`scripts/check-file-size.sh` —— pre-commit + `scripts/test.sh` 第 1 步。
- **超标的正确处理是拆分，不是放宽阈值**：
  - 窗口类膨胀 → 抽协作对象（`XxxController` / `XxxPresenter`），不是堆私有槽函数充数。
  - 一个类多个关注点 → 拆成多个类，或把实现按关注点分到多个 `.cpp`。
  - 状态机膨胀 → 按状态族拆文件。
- 函数层面：**单个函数超过 ~60 行**就该拆；`switch` 的每个分支各自成函数。
- 头文件里**不写实现**（除模板与 `constexpr`）：内联实现会让编译期和体量一起膨胀。

## 3. 命名与风格

- 类型 `UpperCamelCase`，函数/变量 `lowerCamelCase`，成员变量 `m_` 前缀，常量 `kUpperCamel`。
- 命名空间统一 `imrtc`；公开头文件放 `include/imrtc/`。
- **回调名四端同名**：`onCallReceived` / `onCallBegin` / `onCallEnd` …（见设计文档 §7.5）。
- 固定缩写全大写：`SDP` `ICE` `RTP` `RTCP` `SFU` `PLI` `NACK` `TWCC` `UID`。
- 时间量带单位：`timeoutMs`、`durationSec`。
- 格式化交给 `.clang-format`（**入库、CI 校验**），不靠人手对齐。

## 4. 资源与内存（RAII 是硬要求）

- **禁止裸 `new` / `delete`**。所有权用 `std::unique_ptr`；确需共享才用 `shared_ptr`
  并在注释里说明为什么。
- 观察者/回调持有对象一律 `weak_ptr` 或显式注销，**禁止裸指针回调**
  （对象先死、回调后到 = 崩溃，这是 C++ 端最常见的线上崩因）。
- 遵守 Rule of Zero：能靠成员的 RAII 就不写析构；写了析构就把拷贝/移动一起想清楚。
- 跨接口传递大对象用 `const&` 或移动语义，不复制。
- **禁止裸 `char*` 字符串操作**；用 `std::string` / `std::string_view`。
- 资源（socket、track、设备句柄）必须有明确的关闭路径，且**析构里也要兜底关闭**。

## 5. 线程与媒体热路径

- **UI 线程只做 UI**。信令、状态机、媒体回调各自在自己的线程/队列上，
  **回调给上层前显式切到 UI 线程**（Qt 用 `QMetaObject::invokeMethod` 或队列连接）。
- 每个共享可变状态有明确的归属线程或锁，并**在成员声明处写注释说明**。
- 不在锁内做 IO、不在锁内回调外部代码（上层代码可能重入）。
- **禁止在媒体回调线程里做阻塞操作**（分配大内存、写文件、写日志、等锁）。
  需要通知上层就往有界队列里丢，**队列满了丢弃而不是阻塞**。
- Qt 信号槽跨线程一律用 `Qt::QueuedConnection`（默认的 Auto 在某些场景会变直连）。
- 线程必须可优雅退出：有停止标志 + join，禁止 detach 后不管。

## 6. 错误处理

- 公开 API **不抛异常跨越模块边界**；用返回码 + `errno` 风格的错误对象，
  错误码与 `im-rtc-server/internal/errcode` 保持同一套值。
- 内部可用异常，但要在模块边界捕获转换。
- **不吞错误**：忽略返回值必须紧跟注释说明为什么可以忽略。
- 断言（`assert`）只用于「不可能发生」的内部不变量；**用户输入与网络数据一律运行时校验**。

## 7. 日志

- **统一走 engine 的日志入口**（`imrtc::log`，可注入 sink），demo 共用。
- **禁止 `std::cout` / `printf` / `qDebug()` 直接出现在业务代码**。
- 必带字段：`callId` / `roomId` / `uid`（有哪个带哪个）。
- **脱敏**：token 类凭据、完整 SDP 不整条打印；凭据只打前 6 位 + 长度。
- **媒体热路径禁止日志**（每帧/每包都走的路径）。

## 8. 跨平台（Windows + macOS 共用一套代码）

- **平台差异集中在少数几个文件**，用编译期分支或平台实现类隔离；
  **禁止在业务逻辑里散落 `#ifdef _WIN32`**。
- 路径用 `std::filesystem`，不手拼分隔符；编码统一 UTF-8（Windows 侧注意 `wchar_t` 边界转换）。
- CMake 用 `CMakePresets.json` 管两套工具链（`windows-msvc` / `macos-clang`），
  **不要靠个人本地环境变量**。
- 第三方依赖（libwebrtc、Qt）**锁定版本并写进 README**，两平台用同一版本号。
- **写「已在哪个平台验证过」**：本机是 macOS，Windows 侧需要集成方配合。
  不许把「macOS 过了」说成「桌面端完成」。

## 9. 测试与「完成的定义」

- **每加一个功能就配单测**。状态机与帧编解码是**必须**有测试的部分。
- 状态机跑 `im-rtc-server/docs/conformance/*.json` 的**一致性向量**，与另外四端同一份。
- 纯逻辑（状态机、帧编解码、布局计算）不需要摄像头也不需要 Qt GUI，直接测。
- 媒体链路要真机/真设备验证，且写清楚测了什么。
- `./scripts/test.sh` 是唯一测试入口。

## 10. 提交与协作

- 提交信息格式：`类型(模块): 描述`，例如 `feat(engine): 通话状态机跑通一致性向量`。
  类型取 `feat / fix / perf / refactor / docs / test / chore`。
- **直接在 main 提交**（本项目约定，不先开分支）。
- 提交前 pre-commit 跑体量门禁；被拦了就拆分，别 `--no-verify`。

## 11. 不做什么（刻意的边界）

- **不做宿主业务界面**：集成方（公司 Qt 项目）有自己的视觉体系，UI 由他们自画。
  本仓的 Qt Demo 是**参考实现**，不是要求他们照抄。
- **不做 Android**：Android 是独立的 [im-rtc-android](https://github.com/BLiYing/im-rtc-android) 仓，
  **Kotlin 独立实现，不共享本仓的 C++ 核心**（2026-09-05 拍板，理由见设计文档 §8）。
- **engine 不认业务概念**：只认 `userId` / `roomId` / `callId`，不认「群」「会话」「好友」。
- **不引重型第三方框架**：boost 之类除非有不可替代的理由。
