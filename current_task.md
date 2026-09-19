# Current Task — im-rtc-desktop（C++17 Engine + C ABI + Qt Demo）

> **活快照**：就地覆盖、不追加。历史见 `git log` 与 [current_task.archive.md](current_task.archive.md)（末节「2026-09-17（SDK 1.0.0 公网发布后精简）：精简前全文」）。
> 规范 [CONVENTIONS.md](CONVENTIONS.md)（**§2 是 C ABI 边界，本仓最重要的一条**）· 分期 server `docs/design/RTC_CALL_DESIGN.md` §8（桌面）/ §10 · 发版 server `docs/ops/RELEASE.md`。
> ✅ 状态只写在 `../im-rtc-server/docs/CLIENT_PARITY.md`，桌面按 §1.1 交付分层表逐行填，**不许用「桌面 ✅」一个格子结账**。

## 当前焦点

**2026-09-18 深夜：`room.publish` 没等到应答时挂起、会话恢复后重放（对齐 iOS 已完成的实现）。已改完，未提交，等 review。** `test.sh` 八步全绿（166 例），只 macOS。
- 起因：iOS 真机通话中 `room.publish` 超时，引擎按 `reason=error` 把整通电话强杀，而 9 秒后连接就在恢复窗口内 resume 成功了——超时/断线不是服务端的答复，不该判死。
- `engine/src/CallEngineRequests.cpp`：`rollback()` 加 `code` 参数；`room.publish` 失败先判 `isUnansweredCode`（2003 网络不可达 / 2004 请求超时 / 2007 未登录）——不论通话还是会议房，一律发 internal `publish_deferred`（args = 那条 `room.publish` 帧的 data）；其余码保持原逻辑（通话里 `call_failed` 强杀、会议房 `publish_failed` 只摘记账）。原先 2003 分支根本不给 `room.publish` 调 `rollback`（只认退出帧/`room.leave`），是比 iOS 那次更隐蔽的一个变种——`publishing` 记账原地悬空，连「判死」都不会，重连也不会重放。
- `engine/src/state/RoomMachine.cpp`：新增 `deferPublish`——只认 `publishing`，摘掉后把 `{op:"publish", args}` 塞回 `buffered`，`resumeRoom` 回 `joined` 时 `replayBuffered` 原路重放（走 `reduceRoomAct`，不是补发旧帧）。
- `engine/src/state/EngineMachine.cpp`：`isRoomInternal` 加 `publish_deferred`——**这张表是唯一的路由闸门，漏登记的话 `rollback` 发得出去、`deferPublish` 也认，但事件被静默丢给通话机**（iOS 当年正是栽在这里，桌面这次一开始就确认了）。
- `tests/RoomFsmTest.cpp`：`internal` 步骤支持同级 `args`（`MachineInput::internal` 本来就有默认参数重载，不用改签名）；`room_fsm.json` 新增的三条向量原样跑通，未改向量文件。
- 新测（`tests/CallEngineTest.cpp`，`MediaHarness` + `FakeMediaAdapter`）：`enginePublishDeferredReplayedAfterResumeInMeeting`、`enginePublishDeferredReplayedAfterResumeInCall`——发布请求发出后断线，重连 `hello.ok resumed=true` 后新连接上重发同一 cid，通话/房间都没被收场。改之前跑过一遍确认失败（`findSent` 找不到 `room.publish`）。
- 工作区有一处别人未提交的注释改动（`engine/src/signaling/Connection.cpp:131`），未动。

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
