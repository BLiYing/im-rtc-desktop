# CONVENTIONS —— im-rtc-desktop 工程规范（C++17，engine 零 Qt 依赖）

> 本文是**本仓代码的硬约束**。`CLAUDE.md` 讲「这个项目是什么」，本文讲「代码必须长什么样」。
> 协议字段与回调命名以 `im-rtc-server/docs/RTC_PROTOCOL.md` 与设计文档 §7.5 为准，本文不重复。

## 1. 分层与目标划分

```
engine/   无 UI，**零 Qt 依赖（含 QtCore，没有例外）**。WS 用独立库，TLS 复用 libwebrtc 的 BoringSSL。
capi/     对外唯一边界：纯 C ABI 动态库（`imrtc_v1_*`）+ 一个 C 头 + header-only C++ RAII 包装。
demo/     Qt 6 UI。**经 capi 调 engine**，与外部宿主走同一条路，只通过公开回调获取信息。
tests/    单测。依赖 engine（纯逻辑）与 capi（ABI 冒烟）。
```

**依赖方向单向**：`demo → capi → engine`。**engine 绝不反向依赖 capi / demo**。
**为什么 engine 不许碰 Qt**：宿主 Qt5 + 引擎 Qt6 同进程必炸；Qt 是 LGPL，动态链接的合规义务会传染给每个宿主；
非 Qt 宿主（MFC / WPF / Delphi / Java）凭什么为了打个电话装 Qt。
顺带一个好处：**「是否兼容 Qt 5.15」这个悬案自动消失**——宿主用 Qt 几与引擎无关。
engine 内部：`CallEngine（门面）→ signaling / state / media / devices`，
子模块通过抽象接口解耦（纯虚基类），不互相 include 具体实现头。

**引擎的内部公开面是 `engine/include/imrtc/` 下的头文件**（对外交付面见 §2）。这些头：
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
| 一个新公开能力 | 先在 `capi/` 的 C 头里定形，再往上包 | 只加 C++ 接口、C ABI 回头再补 |

## 2. 公开边界：纯 C ABI（本仓最重要的一条）

**对外交付物是「动态库 + 一个 C 头」，不是 C++ 头文件。**
C++ 没有跨编译器 ABI——MSVC↔MinGW、`/MD`↔`/MT`、Debug↔Release CRT、libstdc++↔libc++、
MSVC 各大版本之间的 STL 布局，任何一处不一致就链不上或运行时崩。导出 C++ 类等于「只服务与我们同工具链的宿主」，
导出 C 等于「所有语言都能接」（Qt / MFC / WPF+C# / Delphi / Java / Python / Flutter / Swift）。

**六条红线，违反即不许合入**：

1. **符号**：全部 `imrtc_v1_` 前缀；`visibility=hidden`，只导出这一族。libwebrtc **静态链进动态库内部**，
   宿主不需要知道它存在。
2. **类型**：不透明句柄（`imrtc_engine_t*`）+ POD 结构体 + 函数指针回调。
   每个对外结构体第一个字段是 `uint32_t struct_size`（宿主填，引擎据此判版本）；
   **只许追加字段，永不重排、永不删除**。枚举显式赋数值，绝不依赖声明顺序。
   C 头里**不得出现** `std::*` / 虚函数类 / libwebrtc / Qt 的任何类型。
3. **字符串**：一律 UTF-8 `const char*`。引擎内部拷贝；**回调里给出的指针只在该次回调期间有效**，
   宿主要留就自己拷。
4. **内存**：谁分配谁释放。**绝不让宿主 `free` 引擎分配的内存**（跨 CRT 堆 = 崩溃）；
   需要给出缓冲区就配一个 `imrtc_v1_*_free()`。
5. **错误**：异常绝不跨越 C ABI；全部 `int32_t` 错误码返回，值与 `im-rtc-server/internal/errcode` 同一套。
6. **生命周期**：注册回调时带 `void* user_data`。**`destroy` 必须阻塞到所有回调线程静默后才返回**——
   宿主是 C# / Java 这类 GC 语言时，它没法帮你保活，对象先死回调后到就是必崩。

**回调线程语义写在《接入指南》第一页**：回调发生在引擎内部线程，
**宿主必须自己切回 UI 线程**（Qt 用 `QueuedConnection`，C# 用 `Dispatcher.Invoke`）。不写清楚，接入方 100% 会踩。

**视频渲染给两条路**（C ABI 唯一抹不平的地方，两条都要）：

| 路径 | 形态 | 给谁 |
|---|---|---|
| **A（默认推荐）** | `attachView(uid, void* nativeHandle)`，Windows 收 `HWND`、macOS 收 `NSView*` | Qt（`winId()`）、MFC、WinForms、WPF（`HwndHost`）、AppKit |
| **B（逃生口）** | 原始帧回调：I420/NV12 + 宽高 + 时间戳 + `uid` | 自绘宿主、特殊合成、录制 |

只做 B 会逼每个宿主写一遍渲染器；只做 A 会挡住自绘宿主。

**Qt Demo 也必须经 C ABI + 包装头调引擎**。Demo 走内部 C++ 接口会掩盖全部 ABI 问题，
那样「Demo 跑通」就不等于「宿主接得通」。

> 将来若要做「独立进程 + 本地 IPC」的 sidecar 形态（任何语言、零 ABI 问题、崩溃不带走宿主），
> C ABI 的扁平设计让它是平移而不是重写。**现在不做**，但设计时不许出现无法序列化的口子。

## 3. 文件体量红线（防「上帝类」）

- 非测试 `.cpp` / `.h` **> 600 行**即失败。
- 硬闸：`scripts/check-file-size.sh` —— pre-commit + `scripts/test.sh` 第 1 步。
- **超标的正确处理是拆分，不是放宽阈值**：
  - 窗口类膨胀 → 抽协作对象（`XxxController` / `XxxPresenter`），不是堆私有槽函数充数。
  - 一个类多个关注点 → 拆成多个类，或把实现按关注点分到多个 `.cpp`。
  - 状态机膨胀 → 按状态族拆文件。
- 函数层面：**单个函数超过 ~60 行**就该拆；`switch` 的每个分支各自成函数。
- 头文件里**不写实现**（除模板与 `constexpr`）：内联实现会让编译期和体量一起膨胀。

## 4. 命名与风格

- 类型 `UpperCamelCase`，函数/变量 `lowerCamelCase`，成员变量 `m_` 前缀，常量 `kUpperCamel`。
- 命名空间统一 `imrtc`；公开头文件放 `include/imrtc/`。
- **回调名四端同名**：`onCallReceived` / `onCallBegin` / `onCallEnd` …（见设计文档 §7.5）。
- 固定缩写全大写：`SDP` `ICE` `RTP` `RTCP` `SFU` `PLI` `NACK` `TWCC` `UID`。
- 时间量带单位：`timeoutMs`、`durationSec`。
- 格式化交给 `.clang-format`（**入库、CI 校验**），不靠人手对齐。

## 5. 资源与内存（RAII 是硬要求）

- **禁止裸 `new` / `delete`**。所有权用 `std::unique_ptr`；确需共享才用 `shared_ptr`
  并在注释里说明为什么。
- 观察者/回调持有对象一律 `weak_ptr` 或显式注销，**禁止裸指针回调**
  （对象先死、回调后到 = 崩溃，这是 C++ 端最常见的线上崩因）。
- 遵守 Rule of Zero：能靠成员的 RAII 就不写析构；写了析构就把拷贝/移动一起想清楚。
- 跨接口传递大对象用 `const&` 或移动语义，不复制。
- **禁止裸 `char*` 字符串操作**；用 `std::string` / `std::string_view`。
- 资源（socket、track、设备句柄）必须有明确的关闭路径，且**析构里也要兜底关闭**。

## 6. 线程与媒体热路径

- **UI 线程只做 UI**。信令、状态机、媒体回调各自在自己的线程/队列上。
- **engine 不替宿主切 UI 线程**（它零 Qt 依赖，根本不认识宿主的事件循环）：回调在引擎线程抛出，
  **切回 UI 线程是宿主的责任**，这条必须写在《接入指南》第一页（见 §2）。
  `demo/` 自己用 `QMetaObject::invokeMethod` 切——它就是宿主的示范。
- 每个共享可变状态有明确的归属线程或锁，并**在成员声明处写注释说明**。
- 不在锁内做 IO、不在锁内回调外部代码（上层代码可能重入）。
- **禁止在媒体回调线程里做阻塞操作**（分配大内存、写文件、写日志、等锁）。
  需要通知上层就往有界队列里丢，**队列满了丢弃而不是阻塞**。
- Qt 信号槽跨线程一律用 `Qt::QueuedConnection`（默认的 Auto 在某些场景会变直连）——**这条只管 `demo/`**。
- 线程必须可优雅退出：有停止标志 + join，禁止 detach 后不管。

## 7. 错误处理

- 公开 API **不抛异常跨越模块边界**；用返回码 + `errno` 风格的错误对象，
  错误码与 `im-rtc-server/internal/errcode` 保持同一套值。
- 内部可用异常，但要在模块边界捕获转换。
- **不吞错误**：忽略返回值必须紧跟注释说明为什么可以忽略。
- 断言（`assert`）只用于「不可能发生」的内部不变量；**用户输入与网络数据一律运行时校验**。

## 8. 日志

- **统一走 engine 的日志入口**（`imrtc::log`，可注入 sink），demo 共用。
- **禁止 `std::cout` / `printf` / `qDebug()` 直接出现在业务代码**。
- 必带字段：`callId` / `roomId` / `uid`（有哪个带哪个）。
- **脱敏**：token 类凭据、完整 SDP 不整条打印；凭据只打前 6 位 + 长度。
- **媒体热路径禁止日志**（每帧/每包都走的路径）。用 `// HOTPATH-BEGIN` / `// HOTPATH-END`
  标出边界，闸门扫其间的日志调用。要观测就加原子计数器。
- **硬闸 `scripts/check-logging.sh`**（`scripts/test.sh` 第 2 步）查上面三条，
  并带 `--selftest`——闸门自己回归了会静默放行。
  机制与五仓对齐见 `../im-rtc-server/docs/mechanism/LOGGING.md`。

## 9. 跨平台（Windows + macOS 共用一套代码）

- **平台差异集中在少数几个文件**，用编译期分支或平台实现类隔离；
  **禁止在业务逻辑里散落 `#ifdef _WIN32`**。
- 路径用 `std::filesystem`，不手拼分隔符；编码统一 UTF-8（Windows 侧注意 `wchar_t` 边界转换）。
- CMake 用 `CMakePresets.json` 管两套工具链（`windows-msvc` / `macos-clang`），
  **不要靠个人本地环境变量**。
- 第三方依赖（libwebrtc、WS 库、Demo 的 Qt）**锁定版本并写进 README**，两平台用同一版本号。
- **分发形态**：Windows `im_rtc_engine.dll` + `.lib` + 一个 C 头；macOS `.dylib`，
  **x86_64 + arm64 universal**。最低系统 **Windows 10+**（近年 libwebrtc 已弃 Win7）、**macOS 11+**，写进 README。
- **权限是宿主的事，SDK 代劳不了**：macOS 的 `NSMicrophoneUsageDescription` /
  `NSCameraUsageDescription` 与 Hardened Runtime entitlement 必须由**宿主 App 的 bundle** 声明；
  dylib 还要能过宿主的公证流程。这是接入现场最容易卡半天的一条，写进《接入指南》。
- Windows 侧只依赖 MSVC 运行时：不要管理员权限、不装驱动、不注册 COM。
- **写「已在哪个平台验证过」**：本机是 macOS，Windows 侧需要集成方配合。
  不许把「macOS 过了」说成「桌面端完成」。

## 10. 测试与「完成的定义」

- **每加一个功能就配单测**。状态机与帧编解码是**必须**有测试的部分。
- 状态机跑 `im-rtc-server/docs/conformance/*.json` 的**一致性向量**，与另外四端同一份。
- 纯逻辑（状态机、帧编解码、布局计算）不需要摄像头也不需要 GUI，直接测。
- **C ABI 要有冒烟测试**：create → 注册回调 → 若干调用 → destroy，跑 ASan；
  再用 **Python ctypes 驱动同一份一致性向量**——一次同时验证引擎逻辑与 C ABI 两层，成本几乎为零。
- 媒体链路要真机/真设备验证，且写清楚测了什么。
- `./scripts/test.sh` 是唯一测试入口。

## 11. 提交与协作

- 提交信息格式：`类型(模块): 描述`，例如 `feat(engine): 通话状态机跑通一致性向量`。
  类型取 `feat / fix / perf / refactor / docs / test / chore`。
- **一律先在 worktree 上改，改完再合回 main。不许直接在 main 的工作区改代码。**
  （2026-09-10 起。此前的约定是「直接在 main 提交、不先开分支」，那是单会话时代的规矩。）

  **为什么**：现在同时有好几个会话在并行开发同一个仓。两个会话同时往 main 的工作区
  写文件会互相覆盖，而且**谁也看不见对方改了什么**——`git status` 里混着两个人的改动，
  提交时只能靠猜哪些是自己的。worktree 各有各的工作区，这类事从根上不会发生。

  ```bash
  git worktree add .claude/worktrees/<名字> -b <分支名>   # 开
  # ……在那个目录里改、跑 ./scripts/test.sh、提交……
  git merge --no-ff <分支名>                              # 回到 main 合
  git worktree remove .claude/worktrees/<名字>            # 收
  ```

  **合之前先 `git fetch` 并看一眼 main 动没动过**：并行开发里 main 随时可能已经前进。
  **合完要在 main 上再跑一次 `./scripts/test.sh`**——两个各自都绿的分支合到一起可以是红的，
  git 只保证文本不冲突，不保证语义。（真踩过：同一个文件被两边各加了几十行，
  各自都在 600 行体量红线内，合完就超了。）

  worktree 里跑 `./scripts/test.sh` **不需要再设 `RTC_CONFORMANCE_DIR`**
  （2026-09-10 修好了）。以前要设，是因为找一致性向量用的是 `../im-rtc-server`，
  而 worktree 的根在 `.claude/worktrees/<分支>/`，`..` 指向的是 worktrees 目录。

  > **顺带记一条教训**：「兄弟仓在哪」这种事，一个仓里往往有**两个地方**各自算了一遍——
  > `scripts/test.sh` 一处，测试代码里再一处（web 的 `test/vectors.ts`、
  > iOS 的 `Vectors.swift`）。**只修脚本那处更糟**：原先脚本先失败、报错还算清楚；
  > 修好之后测试才跑到，报出来的信息反而更难懂。
  > 现在统一成 Android 一直用的形状——**脚本算一次，`export` 给测试运行器**，
  > 测试代码那份改成「从自己往上逐级找同级的 im-rtc-server」，
  > 于是不走脚本直接 `npx vitest` / `swift test` 也能工作。

  **例外**：纯文档的小改（typo、补一句说明）可以直接在 main 上做，
  但只要动到代码或跨仓契约，就走 worktree。
- 提交前 pre-commit 跑体量门禁；被拦了就拆分，别 `--no-verify`。

## 12. 不做什么（刻意的边界）

- **不做宿主业务界面**：集成方（公司 Qt 项目）有自己的视觉体系，UI 由他们自画。
  本仓的 Qt Demo 是**参考实现**，不是要求他们照抄。
- **不做 Android**：Android 是独立的 [im-rtc-android](https://github.com/BLiYing/im-rtc-android) 仓，
  **Kotlin 独立实现，不共享本仓的 C++ 核心**（2026-09-05 拍板，理由见设计文档 §8）。
- **engine 不认业务概念**：只认 `userId` / `roomId` / `callId`，不认「群」「会话」「好友」。
- **不为 Electron / CEF / Tauri 宿主做原生接入**：它们的渲染进程自带完整 WebRTC，
  应当直接用 `im-rtc-web` 的 `@im-rtc/call-engine`。给一份说明即可，别写第二套桥。
- **不在 engine 里做 UI 线程调度**：engine 不认识宿主的事件循环（见 §6）。
- **不引重型第三方框架**：boost 之类除非有不可替代的理由（Boost.Beast 若被选作 WS 实现，
  只用 header-only 部分并在 README 写清楚）。
