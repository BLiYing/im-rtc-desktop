# Current Task — im-rtc-desktop（C++17 Engine + C ABI + Qt Demo）

> **活快照**：就地覆盖、不追加。历史见 `git log` 与 [current_task.archive.md](current_task.archive.md)（末节「2026-09-17（SDK 1.0.0 公网发布后精简）：精简前全文」）。
> 规范 [CONVENTIONS.md](CONVENTIONS.md)（**§2 是 C ABI 边界，本仓最重要的一条**）· 分期 server `docs/design/RTC_CALL_DESIGN.md` §8（桌面）/ §10 · 发版 server `docs/ops/RELEASE.md`。
> ✅ 状态只写在 `../im-rtc-server/docs/CLIENT_PARITY.md`，桌面按 §1.1 交付分层表逐行填，**不许用「桌面 ✅」一个格子结账**。

## 当前焦点

**2026-09-17：SDK 1.0.0 已公网发布（GitHub Release，只 macOS），手上没有在做的改动**（main 已推、工作区干净）。
- Release `1.0.0` + `imrtc-desktop-1.0.0-macos.zip`（universal dylib + `imrtc_c.h` / `CallEngine.hpp` + `imrtcConfig.cmake` + LICENSE，SHA-256 `af4ec8c4…`）；第三方 `find_package(imrtc)` + `imrtc::capi`。Demo `IMRTC_SDK=source|local|public`，公网档 09-17 真下载验过。
- 最近提交：§A 发布被拒收场 `d2fcdec`（单测过；Demo 没接媒体发不出 `room.publish`，停 🟡）· `inviter` `bb1ec1e` · API 命名对齐 `2463354` · 宿主对接 `imrtc_v1_call_ex` `6697868`。
- **整体状态**：P5 C ABI + Qt Demo 已交付（`test.sh` 八步，129 例，导出 33 个 `imrtc_v1_*`）。**媒体推迟、纯信令模式**：能拨号、进房、收全部状态回调，没有声音和画面。**Windows 一次都没编译过**。

## 下一步

1. **Windows 过一遍**：编译 + 手点；`install()` / `imrtcConfig.cmake` / `find_package`（`.lib` 进 ARCHIVE、`.dll` 进 RUNTIME 只是照惯例写的）；`imrtc_v1_call_ex` 与结构体尾部追加字段 `dumpbin /exports` + 联调。等机器 / 集成方。
2. **C ABI 与三端的两处不对等**：① 没有 `forceEnd`（加了导出面 33→34，`check-abi.sh` 阈值是 ≥20 不用改）；② observer 没有「票快到期」回调（得先在 engine 造到期计时器，形状没定）。补完同步 server `/guide/desktop`、`/guide/api`。
3. **宿主对接 M1/M8 收尾**：Demo 没有「按 call_id 加入」入口与群号 / user_data 展示。
4. **下个版本（协议批次，server 下一步 0）**：`call.ringing` 发给在场全员，engine 侧消费。
5. 静默失败清单（P0×2 / P1×4 / P2×6）：`../im-rtc-server/docs/ops/silent-failure/desktop.md`。第一条界面层没接（`MainWindow` 没按 `will_reconnect` 分情况展示），第二条没动。
6. 异步口子的形状（一次定完）：渲染路径 B 原始帧回调 + `probeMicrophone` / `startLocalPreview` 出 C ABI。
7. `WebRTCAdapter`（推迟，等 Apple Silicon 或 Windows 机器）。
8. 零碎：后台来电提醒四条 + 设置页详细日志（archive 09-11）；`Shots` 设置页截图（高度 680）没重新生成；UX_FLOWS §07 关窗语义 / 托盘常驻没排期；日志环形缓冲 + `exportDiagnostics()`；按需 C# / P/Invoke 绑定；README 状态 / 依赖 / 开发几节的数字是旧的。
9. **体量**（阈值 600，预警 480）：`demo/MainWindow.cpp` 563、`imrtc_c.h` 535、`CallEngine.hpp` 503、`engine/src/CallEngine.cpp` 502。

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
