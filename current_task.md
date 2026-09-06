# Current Task — im-rtc-desktop（C++17 Engine + C ABI + Qt Demo）

> **活快照**：只记当前状态，**就地覆盖、不追加**。历史见 `git log` 与 [current_task.archive.md](current_task.archive.md)。
> 工程规范见 [CONVENTIONS.md](CONVENTIONS.md)（**§2 是 C ABI 边界，本仓最重要的一条**）；
> 分期见 `im-rtc-server` 的 `docs/design/RTC_CALL_DESIGN.md` §8（桌面）与 §10（分期）。

## 当前焦点

**P5 进行中。第一~三刀 + 门面 + 媒体面接线 + 第五刀 capi 已落地。**
`./scripts/test.sh` **65 个用例全绿**（macOS），约 10100 行 C++17。
落地明细见 [current_task.archive.md](current_task.archive.md)。

**对外交付物已经成立**：`libim_rtc_engine_capi.dylib` + 一个 C 头，
**导出面只有 25 个 `imrtc_v1_*` 符号**（`scripts/check-abi.sh` 守着，已进 test.sh）。
联调工具 `scripts/smoke.sh` **改成经 C ABI 走**——与 Qt / C# 宿主同一条路，
对着真服务端跑通：握手 → 拨号 → `onCallEnd(offline)`。

**现在卡在一个平台问题上**：libwebrtc 桌面预编译包**没有 macOS x86_64**，而本机是 Intel Mac。
所以第四刀只做了上半——`MediaAdapter` 接口 + `MediaPlane` 接线，用假适配器测全；
真正的 `WebRTCAdapter` 要换机器。出路见「已知坑」第一条。

**还没有的**：真实媒体（没有 SDP、没有 ICE、没有声音画面）、设备枚举、Qt Demo。
**Windows 一次都没编译过。**

## 下一步

1. **P5 第六刀 · Qt Demo + 《接入指南》**：四屏，**经 capi 调引擎**。这一刀要装 Qt 6，
   但不需要 libwebrtc——纯信令模式下四屏的状态流转都能演。
2. **P5 第四刀下半 · `WebRTCAdapter`**：libwebrtc C++ API 的真实实现。
   **换机器才能做**（见已知坑第一条）。做完与 Web/iOS 各互打一次。
3. **按需**：C# / P&#8203;Invoke 绑定（C ABI 已经定型，这一层是薄的）。

## 已知坑 / 限制

- **⚠️ 本机（Intel Mac）做不了媒体那一刀**。libwebrtc 桌面预编译包的平台矩阵：

  | 平台 | shiguredo 预编译包 | 备注 |
  |---|---|---|
  | macOS **arm64** | ✅ `webrtc.macos_arm64.tar.gz`（319 MB） | 要 Apple Silicon |
  | macOS **x86_64** | ❌ **近 100 个 release 一个都没有** | **本机是 Intel，卡在这** |
  | Windows x86_64 | ✅ `webrtc.windows_x86_64.zip` | 集成方那边有 |
  | Ubuntu x86_64 | ✅ | 与本产品无关 |

  三条出路，**建议 ② 或 ①**：
  ① 换一台 Apple Silicon Mac —— 最省事；
  ② 直接在 Windows 上做 `WebRTCAdapter` —— 反正 Windows 侧终究要验，顺序换一下而已；
  ③ 自己从源码编 macOS x86_64 的 libwebrtc —— depot_tools + 数十 GB + 数小时，还要长期维护。
  **不要走「换成 stasel/WebRTC 的 XCFramework」**：那是 ObjC API，Windows 上用不了，
  会变成两套媒体适配器，正好违背「一套代码两平台」。

  版本打算锁 **m150.7871.3.2**（与 Android 那条线的 M150 对齐，见 CLIENT_PARITY §3），
  落地时再确认。
- **本机只有 macOS**：Windows 侧编译与验证需要集成方配合，时间未定。
  **不许把「macOS 过了」写成「桌面端完成」**——每次交付分平台说清楚。
- **Demo 必须经 capi 调引擎**。走内部 C++ 接口会掩盖全部 ABI 问题，那样「Demo 跑通」不等于「宿主接得通」。
- **`engine/` 里出现任何 `Q` 开头的类型 = 直接打回**（CONVENTIONS §1）。WS 换独立库，
  TLS 复用 libwebrtc 自带的 BoringSSL，不再引第二份 OpenSSL。
- **C ABI 的头号崩因是生命周期**：`destroy` 必须阻塞到所有回调线程静默；宿主是 C#/Java 这类
  GC 语言时它没法帮你保活。见 CONVENTIONS §5。
- **回调线程是宿主的责任**：engine 零 Qt，不认识宿主的事件循环，**切 UI 线程由宿主自己做**。
- **时钟在门面收口（已定）**：状态机与连接层都不读时钟（前者是不变量 I4，后者是为了可测），
  `CallEngine` 持有 `Clock` 并在 `tick()` 里往下喂。**engine 不自己起线程**——
  宿主的事件循环长什么样我们不知道，多起一条就等于把「回调在哪个线程」甩给宿主。
  宿主按 ~200ms~1s 的粒度调 `tick()` 即可。
- **`logout()` 会本地合成 `onCallEnd(reason=network)`**：服务端随后也会结束那通电话，
  但那条 `call.ended` 到不了我们手里。不合成的话中途登出的通话会从宿主的记录里凭空消失
  （记录只由 onCallEnd 拼出来）。**待与协议确认**：§5.1 的 I8 目前只写了「重连恢复失败」
  一个例外，logout 是第二个——要么写进 I8，要么给 reason 加一个值。
- **C ABI 的三条要写进《接入指南》第一页**：① 回调在 Engine 的线程上抛，切 UI 线程是宿主的事；
  ② 回调里给出的指针只在该次回调期间有效；③ `imrtc_v1_engine_destroy` 阻塞到回调静默，
  返回之后绝不会再有回调——宿主是 C# / Java 这类 GC 语言时尤其要紧。
- **导出面靠脚本守，不靠自觉**：`-fvisibility=hidden` **挡不住 libc++ 的 RTTI**
  （weak-def，实测漏 42 个 `std::function` 的 typeinfo）。白名单在
  `capi/exported_symbols.txt`，守门的是 `scripts/check-abi.sh`（test.sh 第 5 步）。
  Windows 侧由 `__declspec(dllexport)` 天然收口，但仍应在集成方那边用 `dumpbin /exports` 核一次。
- **`Connection` 不是线程安全的**：所有方法（含 `tick`）要在同一个线程上调用。
  `IxTransport` 已经替它把 IX 后台线程的回调排队投递到 `poll()`（`tick()` 里调），
  但**宿主自己也必须在同一个线程上调 Engine 的方法**。这条要写进《接入指南》。
- **包里会有两份 TLS 实现**：信令走平台 TLS（macOS SecureTransport / Windows mbedTLS），
  媒体那一刀引入的 libwebrtc 自带 BoringSSL。原先设想的「TLS 只有一份」要换 Boost.Beast
  才走得通，代价是引入 boost。信令是低频小帧，这个代价当前可以接受——
  **但打包体积与 Windows 侧的 mbedTLS 依赖要在第四刀复核一次。**
- **4401 必须有重试上限**：重连带的是同一枚 token，没有上限就是拿同一把坏钥匙永远敲同一扇门
  （Web 端实测重试到第 19 次还在敲）。连续 3 次后抛 `onKickedOut` 回登录页换票。
- **libwebrtc 桌面预编译包**（`shiguredo-webrtc-build`）随 Chromium 里程碑更新，两平台同一版本号；
  API 变化由 `MediaAdapter` 隔离；**静态链进动态库内部**。
- **macOS 权限 SDK 代劳不了**：`NSMicrophoneUsageDescription` / `NSCameraUsageDescription` 与
  Hardened Runtime entitlement 必须由**宿主 App 的 bundle** 声明。Demo 自己就是第一个宿主。
- **Electron / CEF / Tauri 宿主不接本仓**，直接用 `im-rtc-web` 的 `@im-rtc/call-engine`。

## 关联工程 / 常用命令

- **各端能力对照表：`../im-rtc-server/docs/CLIENT_PARITY.md`**（单一真相源，✅ 只写在那里）。
  桌面端按 **§1.1 交付分层表**逐行填，**不许用「桌面 ✅」一个格子结账**。
- 五仓（本地同级 `/Users/liying/IOSProject/im-rtc/`）：
  [im-rtc-server](https://github.com/BLiYing/im-rtc-server)（**协议契约在这里，只读引用**）·
  [im-rtc-ios](https://github.com/BLiYing/im-rtc-ios) · [im-rtc-web](https://github.com/BLiYing/im-rtc-web) ·
  **im-rtc-desktop**（本仓）· [im-rtc-android](https://github.com/BLiYing/im-rtc-android)。
- 集成方：公司现有 Windows/Mac Qt 项目（不在本机，需对方配合）。
- 常用命令：
  ```bash
  ./scripts/install-hooks.sh                     # 新 clone 跑一次
  ./scripts/test.sh                              # 唯一测试入口：体量 + 配置 + 编译 + 单测
  cmake --preset macos-clang && cmake --build --preset macos-clang
  cmake -S . -B build/asan -G Ninja -DIMRTC_ASAN=ON && cmake --build build/asan   # ASan/UBSan
  ```
