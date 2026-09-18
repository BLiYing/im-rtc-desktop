# Current Task — im-rtc-desktop（C++17 Engine + C ABI + Qt Demo）

> **活快照**：就地覆盖、不追加。历史见 `git log` 与 [current_task.archive.md](current_task.archive.md)（末节「2026-09-17（SDK 1.0.0 公网发布后精简）：精简前全文」）。
> 规范 [CONVENTIONS.md](CONVENTIONS.md)（**§2 是 C ABI 边界，本仓最重要的一条**）· 分期 server `docs/design/RTC_CALL_DESIGN.md` §8（桌面）/ §10 · 发版 server `docs/ops/RELEASE.md`。
> ✅ 状态只写在 `../im-rtc-server/docs/CLIENT_PARITY.md`，桌面按 §1.1 交付分层表逐行填，**不许用「桌面 ✅」一个格子结账**。

## 当前焦点

**2026-09-18 晚：回前台 / 网络变化立即重连（五端对齐，CLIENT_PARITY `[^netchange]`）。** `test.sh` 全绿 164 例（ASan 同绿），只 macOS。
- 引擎 `Connection::appForeground` / `networkChanged`（`engine/src/signaling/ConnectionNudge.cpp`）：正等着重连的**下一个 tick** 就连、退避归零；
  连着的发 ping 探 3 s，到期没下行就走心跳判死同一条路；正在连的失败后不走退避；两次至少隔 2 s。**不在关闭回调里同步 connect**（会析构正在回调的 Transport）。
- C ABI 追加 `imrtc_v1_set_app_foreground` / `imrtc_v1_notify_network_changed`（导出 34 → 36），包装头同名方法；
  包装头贴线，值类型拆进 `capi/include/imrtc/CallEngineTypes.hpp`（install / package.sh / 接入指南已跟上）。
- Qt Demo `EngineBridge::watchSystemSignals`：`applicationStateChanged` → 回前台，`QNetworkInformation` 上线 / 换介质 → 网络变化。Demo 编过，**没手点**。
- 桌面上「回前台」= App 重新激活；**睡眠唤醒没有专门的信号**，要用户点回来才触发，否则靠心跳。

**2026-09-18 凌晨：会议房 M2 的协议那一份已做完并提交（server `docs/design/MEETING_ROOM_DESIGN.md` §6 表里「desktop 跟协议版本、`auto_subscribe`、收帧上限」那一行）。`test.sh` 八步全绿（152 例），只 macOS。**
- 协议 2：`sys.hello` 的 `protocol_version` 默认值 1 → 2；收帧上限拆成两个数（发仍 `kMaxFrameBytes` 64 KiB，收按 `kMaxReceivedFrameBytes` 256 KiB）。
- `room.join.auto_subscribe` 布尔 → 三档字符串 `all | audio | none`（`autoSubscribeModes()`，兜底 `all`）；`RoomContext::autoSubscribe` 跟着从 `bool` 变 `std::string`。
- **比原计划多做了一件**：`engine/src/state/RoomPaging.cpp` 的按页订阅翻译。桌面端本期没有会议界面，但这套翻译在引擎里必须有——五端跑同一份 `room_fsm.json`，新增的 `meeting_audio_auto_video_by_page` 那一组不实现就红；而且少了它，`audio` 档的房间里 `setRemoteLayer` 会对一条根本没订阅的流发换层帧，服务端回 1301、画面永远不来。
- 五秒迟滞按**桌面的做法**走：`CallEngine` 记截止时刻，`tick()` 到点喂内部事件（与 `Heartbeat` 同一套「不持有定时器」）。
- 新测 `tests/RoomPagingTest.cpp` 6 例（16 路上限、翻回来撤迟滞、通话房护栏都在里面）。
- **没做**：会议分页画廊的界面（跟随桌面 UI 排期）；Windows 侧照旧一次都没编译过。

**2026-09-17 夜：「调用结果回给调用方」（server `docs/design/ACTION_RESULT_DESIGN.md` → 2.0.0）已改完，未提交，等 code-review。** `test.sh` 八步全绿（146 例，ASan 同绿），只 macOS。
- C ABI 按 D4 **原地改** 11 个发起类函数签名，末尾 `imrtc_v1_result_cb cb, void* user_data`（cb 为 NULL 失败退回 `on_error`）；导出仍 34 个。C++ 包装加 `std::function<void(Result<T>)> done`。
- 引擎：状态机本地拒绝改成输出里的 `reject`；发帧 / 结算 / 回滚 / 本地收场挪到 `engine/src/CallEngineRequests.cpp`；补上 accept / join 被拒回 idle。
- 与 Web 的出入（记在 CLIENT_PARITY `[^actionresult]`）：等应答时断线只回 2003、不回滚发起类；destroy 后调用是未定义行为，改为 destroy 时悬着的结果回 2005。
- 整体状态不变：**媒体推迟、纯信令模式**；**Windows 一次都没编译过**。`forceEnd` / `onUserRinging`（`0397c97` / `c57b492`）已推送。

## 下一步

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
  ./scripts/package.sh                           # 打发布包：dist/imrtc-desktop-1.0.0-macos.zip
  IMRTC_SDK=local  ./scripts/demo.sh             # Demo 链本机包（先跑 package.sh）
  IMRTC_SDK=public ./scripts/demo.sh             # Demo 链 GitHub Release 包
  cmake --preset macos-clang && cmake --build --preset macos-clang
  cmake -S . -B build/asan -G Ninja -DIMRTC_ASAN=ON && cmake --build build/asan   # ASan/UBSan
  ```
