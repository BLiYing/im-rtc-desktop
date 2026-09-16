# Current Task — im-rtc-desktop（C++17 Engine + C ABI + Qt Demo）

> **活快照**：就地覆盖、不追加。历史见 `git log` 与 [current_task.archive.md](current_task.archive.md)（末节「2026-09-16（发布打包 + Demo 三档之前）：精简前全文」）。
> 规范 [CONVENTIONS.md](CONVENTIONS.md)（**§2 是 C ABI 边界，本仓最重要的一条**）· 分期 server `docs/design/RTC_CALL_DESIGN.md` §8（桌面）/ §10。
> ✅ 状态只写在 `../im-rtc-server/docs/CLIENT_PARITY.md`，桌面按 §1.1 交付分层表逐行填，**不许用「桌面 ✅」一个格子结账**。

## 当前焦点

**2026-09-16（第三轮，本仓部分，已提交（标题「构建: SDK 公网发布准备」，未推送），代码审查零问题）：SDK 公网发布打包 + Demo 三档 SDK 开关。** 仓库要发 GitHub Release
（tag `1.0.0`，等用户确认才建），交付物是 zip，第三方 `find_package(imrtc)` 集成。全部已实现并真跑过验证。

- **capi 新增 `install()`**：只装动态库 `im_rtc_engine_capi`（静态库那份仍是仓内测试专用、不装）+ 两个头
  （`imrtc_c.h`/`CallEngine.hpp`）+ `install(EXPORT)`，导出目标名 `imrtc::capi`（与源码档 Demo 链的别名同名，
  `demo/CMakeLists.txt` 不用分两档写）。新增 `capi/cmake/imrtcConfig.cmake.in` → 生成 `imrtcConfig.cmake` +
  `imrtcConfigVersion.cmake`（`write_basic_package_version_file` COMPATIBILITY **SameMajorVersion**——ABI 红线 2
  「只追加字段」保证同大版本内新版 dylib 对旧头永远兼容，大版本号只在协议不兼容时才升，所以选它而非
  ExactVersion）。install_name 显式设成 `@rpath/libim_rtc_engine_capi.dylib`（原本就是 CMake 默认值，这次写成
  显式属性、不再依赖隐式默认）。`imrtcConfig.cmake` **没有 `find_dependency()`**：transport_ix/IXWebSocket 是
  engine 内部 PRIVATE 依赖、静态链进 dylib 内部，已确认宿主感知不到、也不需要装它的头或库。
- **LICENSE（MIT，2026-09-16 用户补充拍板）**：仓根已有 `LICENSE`（MIT，Copyright (c) 2026 BLiYing，未改动其内容），
  `capi/CMakeLists.txt` 新增 `install(FILES ../LICENSE DESTINATION .)` 把它装到发布包**根目录**（zip 解压后
  第一层就能看见，不在 `include/`/`lib/` 底下）；`scripts/package.sh` 装完后核对 `dist/.../LICENSE` 存在、
  打完 zip 再核对 `unzip -l` 里确实有 `imrtc-desktop-1.0.0-macos/LICENSE` 这一条；README「第三方集成」一节
  补了一句 MIT 出处。
- 版本号唯一来源仍是 `engine/include/imrtc/Version.h` 的 `kSdkVersion`：顶层 CMakeLists.txt 用
  `file(STRINGS)` 抠出设成 `IMRTC_VERSION` 给 `install()` 用；`scripts/package.sh` 自己也抠一遍给 zip 文件名用，
  抠完立刻跟 CMake 配置日志那行核对一致（不一致就失败）——两处独立抠、互相校验。
- **新脚本 `scripts/package.sh`**：release preset（`macos-clang-release`，universal）配置 → 只编
  `im_rtc_engine_capi` 目标 → `cmake --install` 到 `dist/imrtc-desktop-1.0.0-macos/` → `otool -D`（@rpath 名）+
  `lipo -archs`（x86_64+arm64）+ 复用 `scripts/check-abi.sh` 核对导出符号（33 个）→ 打
  `dist/imrtc-desktop-1.0.0-macos.zip`。`dist/` 已进 `.gitignore`。
- **顺带修了 `scripts/check-abi.sh` 一个真 bug**：它原来假设 `dyld_info -exports` 只有一份 3 行表头
  （`tail -n +4`），universal（x86_64+arm64）库会按架构各打一份表头，第二份表头被当成数据行，误判出
  「stray 符号」——之前一直没暴露，因为这脚本至今只跑过 debug 单架构构建。改成只认「第一列是十六进制
  偏移量」的行（`awk '$1 ~ /^0x[0-9A-Fa-f]+$/'`），`sort -u` 去重两个架构的重复符号。修完 universal 库也是
  干净的 33 个。
- **Demo 三档 SDK 开关**：顶层 CMakeLists.txt 新增缓存变量 `IMRTC_SDK_DIR`——设了它就整个切轨：不
  `add_subdirectory` engine/transport/capi/tools，不建引擎测试，改
  `find_package(imrtc ${IMRTC_VERSION} CONFIG REQUIRED PATHS ${IMRTC_SDK_DIR} NO_DEFAULT_PATH)` 再照常
  `add_subdirectory(demo)`；配置时印 `message(STATUS "Demo 用的 SDK：源码 / 发布包 <路径>")`——两档版本号都是
  1.0.0，只有这行分得清。
- `demo/CMakeLists.txt`：`IMRTC_SDK_DIR` 档下用 `POST_BUILD` 把 `imrtc::capi` 的 dylib 拷进
  `imrtc_demo.app/Contents/Frameworks`，另**追加**一条 `@executable_path/../Frameworks` 的 rpath（
  `target_link_options(... -Wl,-rpath,...)`）；`imrtc_demo_shots`（非 bundle）拷到可执行文件旁边、追加
  `@executable_path`。源码档完全不走这段（`if(IMRTC_SDK_DIR)` 整段跳过），行为不变。**踩过一个坑**：第一版
  用 `set_target_properties(... BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "@executable_path/../Frameworks")`，
  这是**替换**而不是追加——CMake 自动算出的那份 build-tree rpath（指向 Qt6 的 `lib/` 目录）被整个顶掉，
  实跑 `imrtc_demo` 直接 `Library not loaded: @rpath/QtWidgets.framework/...`。第一次验证没测出来是因为
  `DYLD_PRINT_LIBRARIES` 的输出被 `grep -i libim_rtc_engine_capi` 过滤了，把 Qt 那条报错挡在看不见的地方；
  换 `scripts/demo.sh` 跑真正的 `exec` 路径（不加任何 grep 过滤）才暴露。改成 `target_link_options` 追加后，
  `otool -l` 能看到三条 `LC_RPATH`（`@executable_path/../Frameworks` + dist 里 capi 的绝对路径 + Qt 的
  `lib/`），`imrtc_demo.app` 完整启动（`DYLD_PRINT_LIBRARIES` 确认 1404 行库加载记录、零 "not loaded"、
  `libim_rtc_engine_capi.dylib` 确实是从 `Contents/Frameworks/` 那份加载的）。
- `scripts/demo.sh` 加 `IMRTC_SDK=source|local|public`（默认 source，行为不变）：`local` 用
  `dist/imrtc-desktop-1.0.0-macos`（不存在就报错提示先跑 `package.sh`），配置到独立目录
  `build/macos-clang-sdk-local`；`public` 从
  `https://github.com/BLiYing/im-rtc-desktop/releases/download/1.0.0/imrtc-desktop-1.0.0-macos.zip` 下载到
  `dist/public/` 解压再用，配置到 `build/macos-clang-sdk-public`；现在还没发布，实测 `curl` 拿到 404，脚本报
  「大概率是还没发布……先用 IMRTC_SDK=local 验」，不是裸 404 堆栈。三档各用各的 build 目录，互不污染缓存。
- README 新增「第三方集成（CMake 最小示例）」一节：zip 布局、`find_package` + `target_link_libraries(imrtc::capi)`
  的最小 CMakeLists、`main.cpp` 最小调用（create/destroy）、`.app`/非 bundle 两种拷 dylib + 布 rpath 的写法、
  「不需要 IXWebSocket」说明；「跑一轮」那节补了 `IMRTC_SDK` 三档说明。

**验证（真跑过）**：
- `./scripts/test.sh` 全绿：**129 个用例**、ABI 导出 **33 个**、包装头回调 **26/26**、Demo 界面测试 5 组全过——
  这是在完成上述全部 CMake 改动**之后**重新跑的，确认没破坏源码档默认路径。
- `./scripts/package.sh`：产出 `dist/imrtc-desktop-1.0.0-macos.zip`（约 720K）；装出来的 dylib `otool -D` =
  `@rpath/libim_rtc_engine_capi.dylib`，`lipo -archs` = `x86_64 arm64`，导出符号 33 个。
- **本地包档**：`-DIMRTC_SDK_DIR=$(pwd)/dist/imrtc-desktop-1.0.0-macos -DIMRTC_BUILD_DEMO=ON` 配置打出
  「Demo 用的 SDK：发布包 …」；`cmake --build ... --target imrtc_demo imrtc_demo_shots` 通过；
  `build/macos-clang-sdk-local/` 下**没有** engine/capi 源码目标产物（只有 `demo/` 一个子目录）；`otool -L`
  看 `imrtc_demo` 引用 `@rpath/libim_rtc_engine_capi.dylib`，dylib 确实在
  `imrtc_demo.app/Contents/Frameworks/` 里；`DYLD_PRINT_LIBRARIES=1` 跑 `imrtc_demo_shots`（离线截图工具，
  不用服务端）打印实际加载的是 build 目录里拷进去的那份，`cmp` 过与 `dist/` 那份逐字节相同，跑完自己退出；
  另外**真正启动了 `imrtc_demo.app` 本体**（不是只测 shots）确认 Qt 的库也一起解析成功、零 "not loaded" 错误、
  `libim_rtc_engine_capi.dylib` 确实从 `Contents/Frameworks/` 加载——上面那条 rpath 坑就是这一步测出来的。
- **仓外最小消费者**：`scratchpad/desktop-consumer`（不进仓库）只
  `find_package(imrtc 1.0.0 CONFIG REQUIRED)` + `target_link_libraries(imrtc::capi)`，`main.cpp` 调
  `imrtc_v1_version()`/`imrtc_v1_engine_create`/`imrtc_v1_engine_destroy`；配置 + 编译 + 运行通过，打印
  `1.0.0` / create 成功 / destroy 完成。

**没做的 / 已知限制**：
- `IMRTC_SDK=public` 只验证了「还没发布时报错清楚」，**没验证过真下载**——得等用户确认建 tag `1.0.0` 的
  Release、上传这次打的 zip 才能验。
- **Windows 侧的 `install()`/`imrtcConfig.cmake` 完全没跑过**（`.lib`/`.dll` 那一路、`dumpbin /exports`），
  本机只有 macOS。
- `imrtcConfigVersion.cmake` 选了 `SameMajorVersion`——现在只有 1.0.0 一个版本号，这条策略还没被
  「发第二个小版本」真正验证过。
- README 其余章节（状态/依赖/开发）里的行数、用例数、导出面数字是旧的（写于更早阶段），这次只新增了
  「第三方集成」一节与「跑一轮」里的三档说明，没有通篇校准——不在本次任务范围内。

以上已提交、**未推送**；tag / Release 等用户逐项确认后再做。

---

**整体状态**：P5 C ABI + Qt Demo 已交付（macOS `test.sh` 八步全绿）。导出面 33 个 `imrtc_v1_*`；现在多了一条
公网分发链路（打包 + 三档 Demo + 第三方最小集成示例，均已验证）。**媒体推迟、按纯信令模式交付**：能拨号、
进房、收到全部状态回调，就是没有声音和画面。还没有：真实媒体（SDP / ICE / 声画）、设备枚举、渲染路径 B
（原始帧回调）、共享屏幕、C# 绑定、群通话 Kit 选人页（M2，桌面没有 Kit，本产品设计已注明不适用）。
**Windows 一次都没编译过**（含这次新增的 install()/find_package 路径）。

## 下一步

0. **GitHub Release**：等用户确认后建 tag `1.0.0`，把 `dist/imrtc-desktop-1.0.0-macos.zip` 传上去；传完把
   `IMRTC_SDK=public` 那条路真跑一次（现在只验证了 404 报错路径）。
1. **Windows 侧新增一条**：`install()`/`imrtcConfig.cmake`/`find_package` 这一路要在 Windows 上过一遍——
   `.lib` 导入库进 `ARCHIVE DESTINATION`、`.dll` 进 `RUNTIME DESTINATION` 目前只是照 CMake 惯例写的，
   没有实机验证过。
2. **§A 发布被拒收场：用故障注入上真端走一遍**（先 `FAULT_INJECTION=1 ./scripts/dev.sh`）：通话接通后
   `curl -X POST $B/v1/dev/faults -d '{"action":"reject","uid":"<本端uid>","frame_type":"room.publish","code":1302}'`，
   再开一次麦 / 摄像头 → 本端收场、结束原因 error、对端收到挂断。过了把 CLIENT_PARITY 那一行 🟡 转 ✅。
   代码已提交（`git log` 标题「call: 发布 / 订阅被拒要收场」），真机验收后续再做。
3. **宿主对接 M1/M8 收尾**：① Demo 没有「按 call_id 加入」入口与群号/user_data 的界面展示；② 提醒维护
   `CLIENT_PARITY.md` 的人：`call.invite_more`/`call.join` 桌面早就实现了，若那张表桌面列还写 ⬜ 是文档漂了；
   ③ `imrtc_v1_call_ex`/结构体尾部追加字段也要在 Windows 上过一遍 `dumpbin /exports` 与联调。
4. **C ABI 与其余三端的两处不对等**（仍未做）：① 没有 `forceEnd`；② observer 没有「票快到期」回调，补
   `on_token_will_expire` 得先在 engine 里造一个到期计时器，形状还没定。导出面加 `force_end` 会从 33 到 34，
   记得同步这次改过的 `scripts/check-abi.sh`（阈值判断是「≥20」不是写死某个数，不用改）与
   `capi/exported_symbols.txt`（通配符不用真改）。
5. 到 Windows 上编译并手点（等机器 / 集成方）。后台来电提醒四条 + 设置页详细日志，见 archive 09-11 那条。
6. `Shots` 的设置页截图（高度改到 680）没重新生成、没人看过。
7. 静默失败点清单（P0×2 / P1×4 / P2×6）：`../im-rtc-server/docs/ops/silent-failure/desktop.md`。第一条
   数据层不缺了，界面层还没接（`MainWindow` 还没按 `will_reconnect` 分情况展示）；第二条没动。
8. **异步口子的形状**（一次定完）：渲染路径 B 原始帧回调 + `probeMicrophone`/`startLocalPreview` 出 C ABI。
9. `WebRTCAdapter`（推迟，等 Apple Silicon 或 Windows 机器）。
10. UX_FLOWS §07 关窗语义/托盘常驻没排期。
11. 体量预警（阈值 600，预警线 480）：`demo/MainWindow.cpp` 563、`capi/include/imrtc/imrtc_c.h` 535、
    `capi/include/imrtc/CallEngine.hpp` 503、`engine/src/CallEngine.cpp` 502（这次
    `./scripts/check-file-size.sh` 看到的一条 WARN，本次没碰这个文件，是既有体量、不是新引入）。
12. 日志下一步：环形缓冲 + `exportDiagnostics()`。按需：C# / P/Invoke 绑定。

## 已知坑 / 限制

**发布打包 / Demo 三档（2026-09-16 新增）**
- `check-abi.sh` 对 **universal（多架构）dylib** 的解析方式变了（改认「第一列是十六进制偏移量」，见上）——
  以后谁再改这个脚本，记得拿 `dist/` 里那份 universal 库回归一次，别只拿 debug 单架构库测。
- `imrtcConfig.cmake` **没有 `find_dependency()`**：这是刻意的（IXWebSocket 是 PRIVATE、静态链进 dylib），
  不是漏写；以后如果 capi 换成 PUBLIC 链接某个第三方库，这里要跟着补。
- `IMRTC_SDK_DIR` 档下 `tests/`（引擎单测、一致性向量）完全不构建——这档只验证 Demo 的「对外集成」，
  不是「换个方式跑全部测试」，回归引擎逻辑仍然只能在源码档做。
- 三档 build 目录（`build/macos-clang`、`build/macos-clang-sdk-local`、`build/macos-clang-sdk-public`）都在
  `.gitignore` 的 `build/` 规则下，`dist/` 单独加了一条。
- **`IMRTC_SDK_DIR` 档下的 rpath 只能追加，不能用 `set_target_properties(... INSTALL_RPATH ...)` 替换**：
  Demo 除了 `imrtc::capi` 还链着 Qt6 好几个 framework，CMake 自动算出的 build-tree rpath 里带着 Qt 的
  `lib/` 目录，替换掉就是 `Library not loaded: @rpath/QtWidgets.framework/...`。改用
  `target_link_options(... "-Wl,-rpath,..." )` 是追加，别再改回 `INSTALL_RPATH` 属性那种写法。
- **LICENSE 是仓根现成文件，`install()` 只是原样拷贝**，不是这次新写的许可证文本——`capi/CMakeLists.txt`
  改动前确认过内容与用户给的一致，没有动它。
- **踩过第二个坑，这次在 `scripts/package.sh` 自己身上**：验证「zip 里有没有 LICENSE」最初写成
  `unzip -l "$ZIP_PATH" | grep -q "...LICENSE\$"`，间歇性报「zip 里没有」——但 `unzip -l` 单独重跑、
  或者去掉 `-q` 都必现「文件其实在」。根因是 `grep -q` 一找到匹配就提前退出、不读完管道，这时 `unzip`
  可能还在写后面的文件列表，写入命中已关闭的读端触发 SIGPIPE、非零退出；脚本开了 `set -o pipefail`，
  这个非零状态会盖过 grep「找到了」那个结果，于是 `if !` 判断为真、误报没找到——**现象是时序竞态，
  不是文件真的丢了**，这也是为什么手动重跑那条管道总是"修复"了问题（连续两次调度时机不同）。
  修法：`ZIP_LISTING=$(unzip -l "$ZIP_PATH")` 先把输出整份捕获进变量，再对变量 `grep`，管道两端不再
  直接相连，没有提前关闭读端这回事。**以后往这类脚本里加 `xxx -l | grep -q` 这种写法要留个心眼**：
  只要左边命令的输出可能超过一个 pipe buffer（多行、体量不小），配合 `-q`/`pipefail`就有这个坑；
  `echo "$var" | grep -qw ...` 这种左边是单行小字符串、一次 write() 能写完的，不受影响，原样保留。

（其余既有已知坑——媒体推迟、后台来电提醒、C ABI 群通话/不兼容改动、协议/引擎、Qt/构建/联调——原文见
[current_task.archive.md](current_task.archive.md)「2026-09-16（发布打包 + Demo 三档之前）：精简前全文」一节，
内容未变，这里不重复贴。）

## 关联工程 / 常用命令

- 五仓（本地同级）：server（**协议契约，只读引用**）· ios · web · **desktop**（本仓）· android。
- 集成方：公司现有 Windows/Mac Qt 项目（不在本机，需对方配合）。
  ```bash
  ./scripts/install-hooks.sh                     # 新 clone 跑一次
  ./scripts/test.sh                              # 唯一测试入口：体量 + 配置 + 编译 + 单测 + ABI
  ./scripts/package.sh                            # 打发布包：dist/imrtc-desktop-1.0.0-macos.zip
  IMRTC_SDK=local  ./scripts/demo.sh              # Demo 链本机包（先跑 package.sh）
  IMRTC_SDK=public ./scripts/demo.sh              # Demo 链 GitHub Release 包（还没发布，会 404）
  cmake --preset macos-clang && cmake --build --preset macos-clang
  cmake -S . -B build/asan -G Ninja -DIMRTC_ASAN=ON && cmake --build build/asan   # ASan/UBSan
  ```
