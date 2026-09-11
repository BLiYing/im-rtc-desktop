# Current Task — im-rtc-desktop（C++17 Engine + C ABI + Qt Demo）

> **活快照**：就地覆盖、不追加。历史见 `git log` 与 [current_task.archive.md](current_task.archive.md)（末节「2026-09-11 精简前全文」）。
> 规范 [CONVENTIONS.md](CONVENTIONS.md)（**§2 是 C ABI 边界，本仓最重要的一条**）· 分期 server `docs/design/RTC_CALL_DESIGN.md` §8（桌面）/ §10。
> ✅ 状态只写在 `../im-rtc-server/docs/CLIENT_PARITY.md`，桌面按 §1.1 交付分层表逐行填，**不许用「桌面 ✅」一个格子结账**。

## 当前焦点

**没有进行中的改动。** 最近两刀（2026-09-11，macOS 已手点验，**Windows 未编译未运行**）：
- 窗口在后台时的来电提醒（UX_FLOWS §07 第 3 条）：`demo/IncomingAlert`（判据 + 状态，系统侧经 `Sink` 注入）· `demo/SystemAlertSink`（`QSystemTrayIcon::showMessage`）·
  `demo/SystemAlertAttention_mac.mm` / `_win.cpp`（`FlashWindowEx`）/ `_stub.cpp`。
- 设置页「详细日志」+ SDK 1.0.0（`acede6a`）。

**整体状态**：P5 C ABI + Qt Demo 已交付（macOS `test.sh` 八步全绿）。导出面只有 27 个 `imrtc_v1_*`（`scripts/check-abi.sh` 守）；`scripts/smoke.sh` 经 C ABI 对真服务端跑通。
**媒体推迟、按纯信令模式交付**：能拨号、进房、收到全部状态回调，就是没有声音和画面。
还没有：真实媒体（SDP / ICE / 声画）、设备枚举、渲染路径 B（原始帧回调）、共享屏幕、C# 绑定。**Windows 一次都没编译过。**

## 下一步

1. **到 Windows 上编译并手点**（等机器 / 集成方）。后台来电提醒四条：① 最小化 / 非活动时来电，任务栏按钮持续闪；② 对方取消后立刻停闪、不留橙色高亮；
   ③ 闪着时切回窗口系统自己停，之后接通 / 挂断不出错；④ 气泡靠藏托盘图标收起。设置页详细日志也点一次。
2. `Shots` 的设置页截图（高度改到 680）没重新生成、没人看过。
3. 静默失败点清单（P0×2 / P1×4 / P2×6）：`../im-rtc-server/docs/ops/silent-failure/desktop.md`，逐条状态只在那里。
   未修头两条：`onDisconnected()` 无参把关闭码 / 原因 / willReconnect 全抹平（Demo 永远显示「正在重连…」）、`deps.send` 对非请求帧无条件返回 true。
4. **异步口子的形状**（一次定完，别定出两套风格）：渲染路径 B 原始帧回调 + `probeMicrophone` / `startLocalPreview` 出 C ABI——帧格式（I420/NV12）、内存谁 free、
   30fps 跨 ABI 的回调频率、completion `user_data` 生命周期。实现等媒体，形状现在就能定。
5. **`WebRTCAdapter`**（推迟，等 Apple Silicon 或 Windows 机器；做完与 Web / iOS 各互打一次）。ICE 重启的信令半边已做完，只剩把 `restartPubICE()` 交给 `createOffer({iceRestart:true})` 并在 offer 生成后清掉。
   规则：各自重启自己 offer 的那条（pub 客户端救、sub 服务端救）；触发用 `failed` 不用 `disconnected`；「要重启」和「要补一次协商」必须分开记。
6. UX_FLOWS §07 第 1 条（关窗语义）/ 第 2 条（托盘常驻 + 绿点 / 菜单）没排期；说话指示器等媒体面。
7. 体量预警：`capi/src/imrtc_c.cpp` 538、`demo/MainWindow.cpp` 548（阈值 600，预警线 480），碰之前先看行数。
8. 日志下一步：环形缓冲 + `exportDiagnostics()`（LOGGING.md §7 的 P3），四端一起定形状。按需：C# / P/Invoke 绑定。

## 已知坑 / 限制

**媒体推迟（2026-09-06 已决定，不是待办，别再当「卡住了」讨论）**
- libwebrtc 桌面预编译包（shiguredo）没有 macOS x86_64，本机是 Intel → 等 Apple Silicon 或 Windows。已排除：`bengreenier/webrtc` darwin-x64（冻在约 M115）、
  自己从源码编（数十 GB，本机数据卷 97% 满）、stasel XCFramework（ObjC API，Windows 用不了）。版本打算锁 m150.7871.3.2。平台矩阵与生态背景见 archive。
- 影响面关死在还没写的 `WebRTCAdapter.cpp`。Demo 里静音 / 摄像头按钮是禁用态（没接适配器时 `openMic()` 静默空操作），落地后打开 `CallOverlay` 的 `setBlocked({})`。
- 本机只有 macOS：**不许把「macOS 过了」写成「桌面端完成」**，每次交付分平台说清楚。

**后台来电提醒 / 横幅（2026-09-11）**
- macOS 系统通知撤不掉（Qt 没接口，`withdraw()` 只能藏托盘图标）；通话结束后再点只激活应用、`IncomingAlert` 不展开（有用例）。Qt cocoa 走的是已废弃的 `NSUserNotificationCenter`。
- 托盘图标只在响铃期间出现（发通知必须先 `show()`，没托盘的 Linux 只剩注意请求）；「被挡住」只能按「不是活动窗口」判；
  **不在窗口激活时 `clear()`**（会和「点通知」回调抢先后，点了不展开）；Demo 没有铃声。
- `_win.cpp` 只在 Mac 上用 mingw-w64 头 + Qt 6.8.3 头 `-fsyntax-only` 过，MSVC 没编过。
- 横幅 / 后台提醒的 `MainWindow` 接线没有集成测试（要真引擎，两台实例手点）。Demo 没有 Web 的 `bannerFirst` 开关，固定横幅先行。
- 横幅用了 `QGraphicsDropShadowEffect`：别往横幅里放原生子窗口（会画到阴影外）。

**C ABI**
- 三处不向后兼容改动（趁 0.1.0 没宿主时改）：`on_kicked_out` 多了 `reason` 参数（导出面没变，`check-abi.sh` 拦不住，第一个宿主接入后补原因就得走版本协商）；
  `imrtc_v1_speaker` / `imrtc_v1_quality` 加首字段 `uint32_t struct_size`（唯二按数组交出去的结构体）；C++ 内部 `CallEngineOptions.wallClock`（`clock` 改为单调时钟，`wallClock` 只填信封 `ts`）。
- **《接入指南》第一页三条**：回调在 Engine 线程上抛、切 UI 线程是宿主的事；回调给出的指针只在该次回调期间有效；`imrtc_v1_engine_destroy` 阻塞到回调静默（GC 语言宿主尤其要紧）。
- `Connection` 不是线程安全的：宿主必须在同一线程调 Engine 方法（`IxTransport` 把 IX 回调排进 `poll()`）。engine 不自己起线程，时钟在 `CallEngine` 门面收口，宿主按 ~200ms~1s 调 `tick()`。
- 导出面靠脚本守：`-fvisibility=hidden` 挡不住 libc++ RTTI（实测漏 42 个 typeinfo），白名单 `capi/exported_symbols.txt`；Windows 侧仍要 `dumpbin /exports` 核一次。
- Demo 必须经 capi 调引擎（`demo/EngineBridge.cpp` 用 `imrtc::capi::Engine`）；`engine/` 里出现 `Q` 开头类型直接打回。Electron / CEF / Tauri 宿主不接本仓，用 `@im-rtc/call-engine`。
- 包里会有两份 TLS（信令走平台 TLS、libwebrtc 带 BoringSSL），打包体积与 Windows 的 mbedTLS 依赖在第四刀复核。libwebrtc 静态链进动态库内部，API 变化由 `MediaAdapter` 隔离。
- macOS 摄像头 / 麦克风权限与 Hardened Runtime entitlement 必须由宿主 App 的 bundle 声明，SDK 代劳不了。

**协议 / 引擎**
- `room.mute` 要服务端 `track_id` 不是 cid（`publishTrackIds` / `Deps::trackIdOfCid`）；`publish.ok` 回来前静音当前报 `invalid_state`，没做意图重放。
- 层名写错的症状是「画面糊而一切正常」（服务端按协议兜底不报错）：`imrtc_v1_set_remote_layer` 的同步 `BAD_PARAMS` 是宿主唯一反馈，别当冗余删掉。
- 报层要在 `onUserVideoAvailable` 里报：建格子时还没 track_id，调用被静默丢掉（返回 0），只报一次就永远按默认 `m` 下发。
- 本端预览走 `attachLocalView`，不是 `attachView(自己的 uid, …)`。
- 红按钮：动作按「有没有 call」分叉（会议房 `leaveRoom()`，1v1 与群通话 `hangup()`），文案按人数分叉；规则在 `CallOverlay::dangerAction()`，有变异测试守着。
- `logout()` 本地合成 `onCallEnd(reason=network)`；**待与协议确认**：§5.1 的 I8 只写了「重连恢复失败」一个例外。
- 4401 重试上限 3，连续 3 次抛 `onKickedOut` 回登录页换票。**2006 阈值「3」未校准、Kit 不接 2006**：见 server「已知坑」。
- 测试 `FakeTransport` 默认同步回调、真件异步：依赖「close() 返回时回调已抛完」的用例要开 `FakeNet::deferClose`。
- SDK 版本号只改 `engine/include/imrtc/Version.h`（五端统一 1.0.0）。详细日志存 QSettings `log/verbose`，但设了环境变量 `IMRTC_LOG_LEVEL` 以它为准（脚本联调时勾选「不生效」是这个原因）。

**Qt / 构建 / 联调**
- Qt 6.8 + Xcode 26 撞 `ld: framework 'AGL' not found`：修法在 Demo `CMakeLists.txt`（直接查 SDK 目录，别用 `find_library`，它会误报「存在」），也要写进《接入指南》。
- 本机 `lupdate` 起不来（链了 QtQml，没装 qtdeclarative；决定不装，只影响本机）：`demo/i18n/imrtc_demo_en.ts` 手工维护，**加了 `tr()` 记得同步 .ts**，否则英文下静默退回中文。
  真要装：`aqt install-qt mac desktop 6.8.3 clang_64 --archives qtdeclarative -O ~/Qt`。
- 原生子窗口盖住同一窗口里所有 Qt 绘制（`raise()` 没用）：格子的名字 / 角标 / 描边放进画面之后创建的原生子窗口（`demo/VideoTile.cpp` 的 `TileChrome`）。
- 原生视图尺寸滞后于 `resizeEvent`，几何一律以 Qt 控件尺寸为准；`autoresizingMask` 与显式 `frame` 不能同时开（会相乘）。
- 带原生子窗口的截图用 `QScreen::grabWindow`（`QWidget::grab()` 内容滞后一帧）。
- 隐藏控件不腾地方（`addStretch()` 吃空间），铺满用 `QStackedWidget` 分页。
- 提示不许用 `QMessageBox` 静态函数（嵌套事件循环等人点确定，挡掉过自动挂断），用 `MainWindow::toast()`；只有破坏性操作用模态。
- `test.sh` 里不许写死测试可执行文件名，遍历 `imrtc_demo_*_test`。
- 联调每轮换用户名（上一轮被 kill 的进程在服务端挂 30 s「通话中」→ `busy`）；同机两实例加 `--profile`（macOS `QStandardPaths` 不理 `$HOME`，`scripts/demo.sh` 已自动传）。

## 关联工程 / 常用命令

- 五仓（本地同级）：server（**协议契约，只读引用**）· ios · web · **desktop**（本仓）· android。
- 集成方：公司现有 Windows/Mac Qt 项目（不在本机，需对方配合）。
  ```bash
  ./scripts/install-hooks.sh                     # 新 clone 跑一次
  ./scripts/test.sh                              # 唯一测试入口：体量 + 配置 + 编译 + 单测
  cmake --preset macos-clang && cmake --build --preset macos-clang
  cmake -S . -B build/asan -G Ninja -DIMRTC_ASAN=ON && cmake --build build/asan   # ASan/UBSan
  ```
