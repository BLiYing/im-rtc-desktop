# Current Task — im-rtc-desktop（C++17 Engine + C ABI + Qt Demo）

> **活快照**：只记当前状态，**就地覆盖、不追加**。历史见 `git log` 与 [current_task.archive.md](current_task.archive.md)。
> 工程规范见 [CONVENTIONS.md](CONVENTIONS.md)（**§2 是 C ABI 边界，本仓最重要的一条**）；
> 分期见 `im-rtc-server` 的 `docs/design/RTC_CALL_DESIGN.md` §8（桌面）与 §10（分期）。

## 当前焦点

**P5 进行中。第一~三刀 + 门面 + 媒体面接线 + 第五刀 capi + 第六刀 Qt Demo 已落地。**
`./scripts/test.sh` **七步全绿**（macOS）：66 个引擎用例 + 21 个 Demo 界面用例，
约 15300 行 C++17。第七步只在 `IMRTC_BUILD_DEMO=ON` 时存在（需要 Qt）。
落地明细见 [current_task.archive.md](current_task.archive.md)。

**对外交付物已经成立**：`libim_rtc_engine_capi.dylib` + 一个 C 头，
**导出面只有 26 个 `imrtc_v1_*` 符号**（`scripts/check-abi.sh` 守着，已进 test.sh）。
联调工具 `scripts/smoke.sh` **改成经 C ABI 走**——与 Qt / C# 宿主同一条路，
对着真服务端跑通：握手 → 拨号 → `onCallEnd(offline)`。

**媒体已决定推迟（2026-09-06 定，见「已知坑」第一条）**：libwebrtc 桌面预编译包**没有 macOS x86_64**，
本机是 Intel Mac，所以第四刀只做了上半——`MediaAdapter` 接口 + `MediaPlane` 接线，用假适配器测全。
真正的 `WebRTCAdapter` **等 Apple Silicon 或 Windows 机器**再做。在那之前
**桌面端按纯信令模式交付**：能拨号、能进房、能收到全部状态回调，就是没有声音和画面。

**Qt 6.8.3 已装好**（2026-09-06，`~/Qt/6.8.3/macos`，占 1.8 GB）：
`aqt install-qt mac desktop 6.8.3 clang_64 --archives qtbase qtsvg qttranslations qttools`，
官方包是 universal（`x86_64 arm64`），Intel Mac 上正常。

**第六刀已落地**：`demo/` 四屏（登录 / 拨号 / 记录 / 设置）+ 通话浮窗四态 + 九宫格，
**经 C ABI 调引擎**。已对着真服务端多实例互打验过（2026-09-06）：

| 场景 | 验到了什么 |
|---|---|
| 1v1 `offline` | 拨不在线的人 → 记录「呼出 · 对方不在线」 |
| 1v1 `no_answer` | 振铃 30s 超时，两侧各落一条（被叫是「未接来电」） |
| 1v1 `hangup` | 真接通 → 计时 → 挂断，两侧 `connected=true`、时长一致 |
| 1v1 `busy` | 第三方拨通话中的人 → 他收 `onCallBusy`，被拨方落一条 `on_call_missed` |
| **群通话 4 人** | `onUserAccept`/`onUserEnter` 逐个到；**`invite_more` 中途加人**，被加者收到 `group=1` 的来电并进同一个房 |
| **群通话离场规则（协议 §4 规则 6）** | 主叫先走→只他自己 `ended{hangup}`，其余人继续；第二个人走；**最后一人也收 `ended`**。三人时长各 6 / 10 / 10，各算各的 |
| **会议房** | `POST /v1/rooms` 建房 → `POST /v1/rooms/{id}/tokens` 换票（273 字符 JWT）→ `room.join` → 双方互见 → `onRoomLeft`。**没有 `onCallEnd`**，会议房不产生通话记录 |

**时长是服务端给的**：1v1 那轮设了 6 秒挂断，记录里是 5 秒——正是不变量 I8 要防的那个差。
《接入指南》在 [docs/INTEGRATION_GUIDE.md](docs/INTEGRATION_GUIDE.md)。
`IMRTC_BUILD_DEMO` 默认 OFF——engine 与测试不依赖 Qt 这条不能破。

**渲染路径 A 的宿主侧已经走通**（九宫格 2026-09-06，1v1 2026-09-07）：
格子与 1v1 那一屏里都塞了真的原生子窗口（macOS `NSView*`），句柄递给引擎，
几何 / DPI / 层级 / 生命周期都有回归测试。1v1 是**两层**：远端铺满 + 本端小窗
160×90 压在右下角。**但引擎侧没通电**——没有媒体适配器时那两个方法直接返回。
等 `WebRTCAdapter` 落地就自动生效，Demo 这边不用改。`--fake-video` 能看到它。

**C ABI 加了两个符号**（2026-09-07，都是追加式，已有的一个没动，25 → 27）：

1. `imrtc_v1_attach_local_view(engine, handle)` —— **本端预览**。做 1v1 那一屏时
   才发现缺：`attachView` 是按 uid 找**远端**轨道的，而引擎不知道自己的 uid，
   本端画面也来自采集侧、根本没有 trackId。
2. `imrtc_v1_set_remote_layer(engine, uid, layer)` —— **层上界**（协议 §3.5）。
   底下全是现成的（`RoomMachine::updateLayer` + `remoteTracks` 记账早就有了），
   缺的只是门面与 C ABI 这两层。Web 端一直有，桌面端漏了。

**层上界这一条是唯一一个不用等媒体的**：`room.update_layer` 是纯信令帧。
已对真服务端验过（2026-09-07）——用 `rtc-cli` 的 `publishStub` 那条路造一个
只登记 Track、不做媒体协商的发布者占住会议房，桌面端进同一个房、收到
`onUserVideoAvailable`、报 `l`，服务端回 `.ok`（持房 18 秒 > 10 秒请求超时，
没有 2004，所以应答确实回来了）。Demo 里九宫格报 `l`、1v1 报 `h`。

**顺带发现一个服务端的洞（已开任务，不在本仓修）**：协议 §3.5 说非法
`max_layer` 回 1306，但**服务端不校验**——实测发 `"zzz"` 照样回 `.ok`、日志干净，
而 `internal/sfu/layer.go` 的 `layerRank()` 把不认识的值兜底成 `0`（等同 `l`）。
后果是写错一个字母那条流被**永久锁在最低层，没有报错也没有日志**。
所以桌面端在 **C ABI 边界上自己挡了**这一类值（`isValidLayer`，名单从
`imrtc::layers()` 读，不另抄一份）。

**边界上还有一个洞（已知，未修）**：`probeMicrophone` / `startLocalPreview`
在 `CallEngine.h` 的公开面上，**但不在 27 个导出符号里**——走纯 C ABI 的宿主
（Qt / C#）根本调不到。原因是它们带 completion 回调，跨 C ABI 要另定形状，
**与渲染路径 B 是同一类问题**，该一起定。现在没人踩到是因为没有媒体适配器时
它们本来就是空操作。

**还没有的**：真实媒体（没有 SDP、没有 ICE、没有声音画面）、设备枚举、
渲染路径 B（原始帧回调）、共享屏幕、C# 绑定。**Windows 一次都没编译过。**

## 下一步

1. **异步口子的形状**（一件事，两处用）：渲染路径 B 的原始帧回调，与
   `probeMicrophone` / `startLocalPreview` 这两个还没出 C ABI 的方法，
   卡的是同一个问题——**带 completion / 高频回调的东西怎么过 C ABI**。
   要定：帧格式（I420/NV12）、那块内存谁 free、回调频率（30fps 跨 ABI 是真问题）、
   completion 的 `user_data` 生命周期。**实现等媒体，形状现在就能定**，
   而且该一次定完，别分两次定出两套风格。
2. **`WebRTCAdapter`**：等 Apple Silicon 或 Windows 机器。宿主侧的坑已经清完了，
   到时候只剩一个文件。
2. ~~**P5 第四刀下半 · `WebRTCAdapter`**~~ —— **已推迟，不在当前排期内**（2026-09-06 定）。
   等 Apple Silicon 或 Windows 机器到手再做，做完与 Web/iOS 各互打一次。
   - **做的时候别漏 ICE 重启**（协议 §3.3，2026-09-06 夜四端已落地，桌面端是唯一还没有的）：
     规则是「**各自重启自己 offer 的那条**」——`pub` 由客户端救、`sub` 由服务端救，
     沿用固定 offerer 的设计，所以**不需要新协议帧**，也不会两边同时 offer 打架。
     触发用 `failed` 不用 `disconnected`（后者是几秒的抖动）。
     不做的后果不是"画质差一点"：切网 / 休眠唤醒之后人**永久掉出这通通话**，
     对端格子从此是一块黑，而界面上一切正常、计时还在走、谁也不挂断。
     另有一个坑四端都钉了用例：**「要重启」和「要补一次协商」必须分开记**——
     忙的时候重启请求只能先记成待办，待办里不带「重启」这一位，补出来的就是个普通 offer，
     那条连接**永远重连不上，而日志里一切正常**。
3. **按需**：C# / P&#8203;Invoke 绑定（C ABI 已经定型，这一层是薄的）。

## 已知坑 / 限制

- **✅ 已决定（2026-09-06）：媒体推迟，Intel Mac 上先不支持声音与视频。**
  这是一个**决定**，不是一条待办——不要再拿它当「卡住了」重新讨论。
  桌面端在此期间按**纯信令模式**交付；影响面被 `MediaAdapter` 关死在
  **一个还没写的文件 `WebRTCAdapter.cpp`** 里，其余全部代码与测试不受影响。
  起因是 libwebrtc 桌面预编译包的平台矩阵：

  | 平台 | shiguredo 预编译包 | 备注 |
  |---|---|---|
  | macOS **arm64** | ✅ `webrtc.macos_arm64.tar.gz`（319 MB） | 要 Apple Silicon |
  | macOS **x86_64** | ❌ **近 100 个 release 一个都没有** | **本机是 Intel，卡在这** |
  | Windows x86_64 | ✅ `webrtc.windows_x86_64.zip` | 集成方那边有 |
  | Ubuntu x86_64 | ✅ | 与本产品无关 |

  **决定：等机器**——Apple Silicon Mac 或 Windows，哪台先到手就在哪台做。
  已经排除掉的三条路，别再回头试：
  - ❌ **`bengreenier/webrtc` 的 `darwin-x64` 包**（273 MB）—— 它**确实存在**，能在 Intel Mac 上用，
    但冻在 2023 年的 branch 5735（约 **M115**），与我们要对齐的 **M150** 差 35 个里程碑。
    为一台过渡机器去适配一套三年前的 API，代价高于收益。
  - ❌ **自己从源码编 macOS x86_64 的 libwebrtc** —— depot_tools + 数十 GB + 数小时，还要长期维护。
    本机数据卷已 97% 满，连磁盘都不够。
  - ❌ **换成 stasel/WebRTC 的 XCFramework** —— 那是 ObjC API，Windows 上用不了，
    会变成两套媒体适配器，正好违背「一套代码两平台」。

  生态背景（说明这不是我们选型失误）：Homebrew 已于 2026-09 停发 Intel macOS bottle，
  macOS 27 起 Apple 不再支持 Intel，GitHub Actions 2027 下线 Intel runner——
  **x86_64 macOS 正在被整个生态放弃**，不只是 libwebrtc 一家。

  版本打算锁 **m150.7871.3.2**（与 Android 那条线的 M150 对齐，见 CLIENT_PARITY §3），
  落地时再确认。
- **本机只有 macOS**：Windows 侧编译与验证需要集成方配合，时间未定。
  **不许把「macOS 过了」写成「桌面端完成」**——每次交付分平台说清楚。
- **Qt 6.8 + Xcode 26 会撞 `ld: framework 'AGL' not found`**。macOS 26 SDK 删掉了 `AGL.framework`，
  而 Qt 6.8 的 `FindWrapOpenGL.cmake` 在找不到时会**无条件回退到硬编码的 `-framework AGL`**。
  **注意别用 `find_library` 探测**：AGL 还留在**运行系统**的 `/System/Library/Frameworks/` 下，
  只是从 **SDK** 里删了，而 `ld` 只看 SDK——`find_library` 会误报「存在」。
  必须直接查 SDK 目录。修法已进 Demo 的 `CMakeLists.txt`，也要写进《接入指南》——
  集成方只要是 Xcode 26 + Qt 6.8 就会撞同一个坑。
- **Demo 必须经 capi 调引擎**。走内部 C++ 接口会掩盖全部 ABI 问题，那样「Demo 跑通」不等于「宿主接得通」。
  已经是这么接的（`demo/EngineBridge.cpp` 用 `imrtc::capi::Engine`，链的是动态库）。
- **本机 `lupdate` 跑不起来 —— 这是个连带依赖，不是我们用了 QML**（2026-09-06 查证）。
  `lupdate` 要能从 `.qml` 里抠 `qsTr()`，所以它链了 **QtQml**；而 QtQml 属于
  `qtdeclarative`（QML / Qt Quick 那一整套：QtQml、QtQuick、QtQuickControls2…），
  最小装法没装它。**Demo 一行 QML 都没有**（用的是 Widgets，在 `qtbase` 里）。

  | 工具 | 依赖 | 状态 |
  |---|---|---|
  | `lrelease`（.ts → .qm） | 只要 QtCore | ✅ 正常，构建与运行都不受影响 |
  | `lupdate`（源码 → .ts） | QtCore + QtNetwork + **QtQml** | ❌ 起不来 |

  **决定：不装。** 装它只买到「一个构建期工具能启动」，代价 430 MB 下载 /
  约 2.8 GB 磁盘，而那 2.8 GB 里 99% 的东西永远不会被加载；集成方装的是全量 Qt，
  他们那边 `lupdate` 本来就能跑，所以这条限制**只影响本机，不影响交付物**。
  真要装：`aqt install-qt mac desktop 6.8.3 clang_64 --archives qtdeclarative -O ~/Qt`
  （只写 qtdeclarative，会并进已有的 `~/Qt/6.8.3/macos`，不重装 qtbase）。

  代价是 `demo/i18n/imrtc_demo_en.ts`（132 条）**手工维护**：
  **加了新的 `tr()` 之后记得同步 .ts**，否则那条在英文下会静默退回中文，不报错。
- **层上界的合法性只有我们这一道校验**：协议 §3.5 定义了 1306 `layer_unavailable`，
  但**服务端从未使用它**（`internal/errcode` 里定义了，没有任何调用点），
  `Room.UpdateLayer` 原样存下非法值，`internal/sfu/layer.go` 的 `layerRank()`
  再 `default: return 0` 把它兜底成最低层。所以非法层名的症状是
  **画面糊 + 日志全干净**。桌面端在 `imrtc_v1_set_remote_layer` 里同步挡回
  `BAD_PARAMS`。服务端那边已开任务，**修好之前别把这道校验当冗余删掉**。
- **报层要在 `onUserVideoAvailable` 里报，不能只在建格子时报**。格子通常在
  `onUserEnter` 就建好，那时对方视频轨还没发布，引擎手里没有 track_id，
  这次调用会被**静默丢掉**（返回 0，不是错误——先建格子后到轨道是正常时序）。
  只报一次的结果是永远按默认的 `m` 下发，**没有任何症状**。
- **本端预览必须走 `attachLocalView`，不是 `attachView(自己的 uid, …)`**。
  引擎不知道自己的 uid，本端画面也没有远端 trackId。别为此约定魔法 uid。
- **隐藏控件不腾地方**：想让画面铺满时把头像 `hide()` 掉是不够的，
  同一个布局里的 `addStretch()` 会把空间全吃掉——实测画面只分到 520×**16**。
  用 `QStackedWidget` 分页。
- **原生子窗口会盖住同一个窗口里所有 Qt 绘制**，与 Qt 的 z 序无关（`raise()` 没用）。
  所以格子的名字标签 / 静音角标 / 发言描边不能由格子自己 `paintEvent` 画——
  必须放进**画面之后创建的另一个原生子窗口**（`demo/VideoTile.cpp` 的 `TileChrome`）。
- **原生视图的尺寸滞后于 Qt 的 `resizeEvent`**：那一刻读 `view.layer.bounds`
  拿到的是上一次的值，而且**不会再有第二次 resizeEvent 来纠正**。
  实测格子 157×92 / 控件 153×88 / layer 停在 116×86。几何一律**以 Qt 控件尺寸为准**。
  另：`autoresizingMask` 与显式 `frame` **不能同时开**，会相乘（153×88 → 190×90）。
- **带原生子窗口的界面截图要用 `QScreen::grabWindow`**。`QWidget::grab()` 能看到
  原生层但**内容滞后一帧**（实测与系统合成差 5.11/255，先跑一次系统合成后只差 0.54）。
  `[CATransaction flush]` 不管用。`screencapture` 那条路要「屏幕录制」授权。
- **test.sh 里不许写死测试可执行文件名**：按用例分文件之后，写死会在改名后
  **静默跑一个过时的二进制**。已经踩过一次，现在是遍历 `imrtc_demo_*_test`。
- **红按钮：动作按「有没有 call」分叉，文案按人数分叉——依据不一样，别混。**
  会议房 → `leaveRoom()`；**1v1 与群通话都是 `hangup()`**（接通前 cancel/reject）。
  文案则是 1v1「挂断」、群通话与会议房「离开」。
  **我们写错过**：设计稿 §05 原话是「群/会议 → leaveRoom()」，照抄之后群通话的离开者
  **永远收不到 `onCallEnd`**（服务端对 `room.leave` 只广播 `participant_left`），
  记录落不下来且违反 I1。Web / iOS 的代码本来就只判 `isMeeting`，是对的。
  设计稿已于 2026-09-06 更正，别再照旧版写。规则本身抽在
  `CallOverlay::dangerAction()` 里（单一真相源），`demo/tests/` 有回归测试守着，
  做过变异测试：改回旧规则，两个用例当场红。
- **联调时每轮换一次用户名**。上一轮被 kill 的进程在服务端还挂着「通话中」
  （30s 恢复窗口），复用同一个 uid 会让下一轮直接回 `busy`——
  `scripts/smoke.sh` 早就是这么做的（`smoke-$$`），Demo 联调也照办。
- **提示不许用 `QMessageBox` 静态函数**。它开一个嵌套事件循环并**等人点确定**，
  在没人看着的场合会把后面的动作全挡住——实测中它挡掉过一次自动挂断，
  害我以为是引擎少发了 `onCallEnd`。`MainWindow::toast()` 现在是非模态自动消失的提示条。
  只有「清空通话记录」这类破坏性操作才该用模态。
- **同机开两个实例要加 `--profile`**。macOS 的 `QStandardPaths` **不理会 `$HOME`**
  （它走密码库里的真实家目录），所以靠环境变量隔离不了，两个实例会写同一份
  `call-history.json` 互相覆盖。`scripts/demo.sh` 已自动按用户名传 `--profile`。
- **静音 / 摄像头按钮在 Demo 里是禁用态**：没接媒体适配器时引擎的 `openMic()`
  是**静默空操作**，留个假开关比画成禁用更糟。`WebRTCAdapter` 落地后
  把 `CallOverlay` 里的 `setBlocked({})` 打开即可。
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
