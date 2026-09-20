# Current Task — im-rtc-desktop（C++17 Engine + C ABI + Qt Demo）

> **活快照**：就地覆盖、不追加。历史见 `git log` 与 [current_task.archive.md](current_task.archive.md)（末节「2026-09-17（SDK 1.0.0 公网发布后精简）：精简前全文」）。
> 规范 [CONVENTIONS.md](CONVENTIONS.md)（**§2 是 C ABI 边界，本仓最重要的一条**）· 分期 server `docs/design/RTC_CALL_DESIGN.md` §8（桌面）/ §10 · 发版 server `docs/ops/RELEASE.md`。
> ✅ 状态只写在 `../im-rtc-server/docs/CLIENT_PARITY.md`，桌面按 §1.1 交付分层表逐行填，**不许用「桌面 ✅」一个格子结账**。

## 当前焦点

- **09-19 新增 `fetchCallHistory`**（引擎 `CallEngineHistory.cpp` + 可注入 `HttpClient`，真实实现 `IxHttpClient`；C ABI `imrtc_v1_fetch_call_history`，类型拆在 `imrtc_c_history.h`；包装头 `CallEngine.hpp` 已接；`GET /v1/calls`，游标翻页，`next_cursor == 0` 即到底，只返回本人）：`./scripts/test.sh` 8 步全过（178 个引擎用例含 10 条通话记录，Demo 测试含 `CallHistoryModelTest`，Darwin）；Demo 通话记录页改成调它，本地拼记录那套已删。**没验**：Demo 真连服务端翻页、Windows。 同日记录页对齐 Android 版式（圆角卡片、图标 / 名字加两行摘要 / 右侧时间，群通话第一行「群通话 · N 人」），时间按发起时间四档（今天 `HH:mm` / `昨天 HH:mm` / `M月d日 HH:mm` / `yyyy年M月d日 HH:mm`，`callstrings::callTime` 纯函数 + `HistoryTimeTest` 11 条，界面未截图目测）。**Demo 测试要显式构建才会跑**：`cmake -S . -B build/demo-check -DIMRTC_BUILD_DEMO=ON -DCMAKE_PREFIX_PATH=~/Qt/6.8.3/macos && cmake --build build/demo-check`，再跑 `build/demo-check/demo/imrtc_demo_*_test`（7 组全过）；`./scripts/test.sh` 第 8 步在 `macos-clang` 预设下跑的是旧二进制，不会编 Demo 新代码。

**2026-09-19：握手等应答超时不再干等服务端读超时。** `Connection::handleHelloOk` 失败时，若是本地 `SignalingTimeout`
就摘监听、以 1001 `hello timeout` 关掉并自己走一次 `onTransportClosed`（按退避重连）；服务端明确拒绝的仍把关闭码留给服务端。
起因是 Web 真机在 `silence` 注入下抓到同一类缺口（协议 §1.2 已补规则）。新测 `helloTimeoutClosesAndReconnects`（deferClose 下验迟到的 close 不重复收场），撤掉修复会失败。
`test.sh` 全绿，**只有单测，没上真机**。09-18 的 `publish_deferred` 已提交。

**同批（09-18 晚～09-19，均已推送、只有单测）**：
- **发布没等到应答不再判死**（`19c1ebb`）：2003 / 2004 / 2007 通话与会议房都发 `publish_deferred`，挂起等重连后补发；原先 2003 分支不给 `room.publish` 调 rollback，`publishing` 原地悬空、重连也不重放。
- **回前台 / 网络变化立即重连**（`fd9c16b`）：`Connection::appForeground` / `networkChanged`（`ConnectionNudge.cpp`），两次至少隔 2 s；C ABI 追加 `imrtc_v1_set_app_foreground` / `imrtc_v1_notify_network_changed`（34 → 36）；Qt Demo 接了 `applicationStateChanged` 与 `QNetworkInformation`（编过、未手点）。
状态见 CLIENT_PARITY `[^pubdefer]` `[^netchange]`（桌面均 🟡）。

## 下一步

0. **`ping_interval_sec` 钳到 [5,60]（缺省 / 非正数按 15）**：Android、iOS 已做，桌面没做（`Connection.cpp` 的 `handleHelloOk` 直接 `readInt(data, "ping_interval_sec")`）。做的时候配单测，同值。
1. **Windows 过一遍**：编译 + 手点；`install()` / `imrtcConfig.cmake` / `find_package`（`.lib` 进 ARCHIVE、`.dll` 进 RUNTIME 只是照惯例写的）；`imrtc_v1_call_ex` 与结构体尾部追加字段 `dumpbin /exports` + 联调。等机器 / 集成方。
2. **C ABI 与三端余下一处不对等**：observer 没有「票快到期」回调（得先在 engine 造到期计时器，形状没定）。`forceEnd` 已补（`0397c97`），Demo 红键还没接看门狗。
3. **宿主对接 M1/M8 收尾**：Demo 没有「按 call_id 加入」入口与群号 / user_data 展示。
4. **2.0.0**：code-review 通过后等用户通知发版（改 `Version.h` → tag → `package.sh` → release）；Demo 的 login 没接结果回调（失败退回 `on_error`）。
5. 静默失败清单（P0×2 / P1×4 / P2×6）：`../im-rtc-server/docs/ops/silent-failure/desktop.md`。第一条界面层没接（`MainWindow` 没按 `will_reconnect` 分情况展示），第二条没动。
6. 异步口子的形状（一次定完）：渲染路径 B 原始帧回调 + `probeMicrophone` / `startLocalPreview` 出 C ABI。
7. `WebRTCAdapter`（推迟，等 Apple Silicon 或 Windows 机器）。
8. 零碎：后台来电提醒四条 + 设置页详细日志（archive 09-11）；`Shots` 设置页截图（高度 680）没重新生成；UX_FLOWS §07 关窗语义 / 托盘常驻没排期；日志环形缓冲 + `exportDiagnostics()`；按需 C# / P/Invoke 绑定；README 状态 / 依赖 / 开发几节的数字是旧的。
9. **体量**（阈值 600，预警 480）：**`imrtc_c.h` 591 贴线，再往里加东西先拆**（`CallEngine.hpp` 09-18 拆出值类型后 540）；`demo/MainWindow.cpp` 563。

## 已知坑 / 限制

- **没有头像 / 名字解析口子（暂缓，2026-09-19 核对）**：engine / capi 无 profile 概念，Qt Demo 的来电横幅、1v1 大头像页与名字（`CallOverlay::applyPhase`）、格子（`CallOverlayMembers.cpp` 的 `setIdentity(uid, uid)`）全部直接用 uid，`paintAvatar` 只画首字母色块、无图片入口。1v1 标题栏是固定文案不显示对方。
  要做时照 server `docs/design/HOST_PROFILE_DISPLAY_DESIGN.md` §3、§10 的清单（7 处界面、头像叠放、底色按 uid、读宿主数据不自建缓存），iOS 已补齐可对照。没有真实桌面宿主前不做。

**发布打包 / Demo 三档**
- 版本号唯一来源 `engine/include/imrtc/Version.h` 的 `kSdkVersion`；下次发版：改它 → tag → `./scripts/package.sh` → `gh release create`。`imrtcConfigVersion.cmake` 是 `SameMajorVersion`，还没被第二个小版本验证过。
- `check-abi.sh` 对 universal dylib 改认「第一列是十六进制偏移量」的行：改这个脚本要拿 `dist/` 里的 universal 库回归，别只拿 debug 单架构库。
- `imrtcConfig.cmake` **没有 `find_dependency()` 是刻意的**（IXWebSocket PRIVATE、静态链进 dylib）；capi 若 PUBLIC 链第三方库要补。
- `IMRTC_SDK_DIR` 档下 `tests/` 完全不构建，回归引擎逻辑只能在源码档。三档 build 目录各自独立。
- **`IMRTC_SDK_DIR` 档的 rpath 只能追加**（`target_link_options(-Wl,-rpath,…)`）：用 `INSTALL_RPATH` 属性会替换掉 Qt 的 build-tree rpath → `Library not loaded: @rpath/QtWidgets.framework`。
- **`xxx | grep -q` + `pipefail` 有竞态**：`grep -q` 提前退出 → 左边 SIGPIPE 非零 → 误报「没找到」。输出可能超过一个 pipe buffer 时先捕获进变量再 grep（`package.sh` 的 `ZIP_LISTING`）。

其余既有已知坑（媒体推迟、后台来电提醒、C ABI 群通话 / 不兼容改动、协议 / 引擎、Qt / 构建 / 联调）原文见 archive「2026-09-16（发布打包 + Demo 三档之前）：精简前全文」一节，内容未变。

## 关联工程 / 常用命令

- 五仓（本地同级）：server（**协议契约，只读引用**）· ios · web · **desktop**（本仓）· android。
- 集成方：公司现有 Windows/Mac Qt 项目（不在本机，需对方配合）。
  ```bash
  ./scripts/install-hooks.sh                     # 新 clone 跑一次
  ./scripts/test.sh                              # 唯一测试入口：体量 + 配置 + 编译 + 单测 + ABI
  ./scripts/package.sh                           # 打发布包：dist/imrtc-desktop-2.0.0-macos.zip
  IMRTC_SDK=local  ./scripts/demo.sh             # Demo 链本机包（先跑 package.sh）
  IMRTC_SDK=public ./scripts/demo.sh             # Demo 链 GitHub Release 包
  cmake --preset macos-clang && cmake --build --preset macos-clang
  cmake -S . -B build/asan -G Ninja -DIMRTC_ASAN=ON && cmake --build build/asan   # ASan/UBSan
  ```
