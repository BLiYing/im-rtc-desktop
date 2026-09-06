# 接入指南 —— im-rtc 桌面端

> 面向**把 im-rtc 装进自己 Windows / macOS 应用**的人。
> 你不需要读引擎的源码，也不需要用 Qt——本指南提到 Qt 的地方只是举例。
>
> 配套参考实现在 `demo/`（Qt 6 Widgets），它**经的是与你完全相同的 C ABI**，
> 没有任何内部捷径。看不懂本文某一段时，去 `demo/` 里找对应的那几行。

---

## 0. 你拿到的三样东西

| 文件 | 是什么 | 必需 |
|---|---|---|
| `libim_rtc_engine_capi.dylib` / `im_rtc_engine_capi.dll` | 引擎本体。libwebrtc 静态链在里面 | ✅ |
| `imrtc/imrtc_c.h` | **纯 C 头**，对外唯一边界。284 行，没有一个 `std::` | ✅ |
| `imrtc/CallEngine.hpp` | header-only 的 C++ RAII 包装。**它自己也走 C ABI** | 可选 |

**边界是 C，不是 C++。** 这不是保守，是必须：C++ 没有跨编译器 ABI——
MSVC 与 MinGW 不通、`/MD` 与 `/MT` 不通、Debug 与 Release 的 CRT 不通、
libstdc++ 与 libc++ 不通。导出 C++ 类等于「只服务与我们同工具链的宿主」；
导出 C 等于 Qt / MFC / WPF+C# / Delphi / Java / Python / Flutter / Swift 都能接。

动态库的导出面只有 **26 个 `imrtc_v1_*` 符号**，有脚本守着（`scripts/check-abi.sh`）。
你可以自己核一遍：macOS 用 `dyld_info -exports`，Windows 用 `dumpbin /exports`。

---

## 1. 五分钟：最小可运行

用 C++ 包装的话，一屏就够：

```cpp
#include "imrtc/CallEngine.hpp"

class MyObserver : public imrtc::capi::Observer {
public:
  void onConnected(const std::string& sessionId, bool resumed) override {
    // 界面切到「已连接」
  }
  void onCallReceived(const std::string& callId, const std::string& caller,
                      const std::vector<std::string>& calleeIds,
                      const std::string& mediaType, bool isGroup) override {
    // 弹来电页
  }
  void onCallEnd(const std::string& callId, const std::string& reason,
                 std::int64_t durationSec, const std::string& endedBy) override {
    // 收界面 + 落一条通话记录
  }
};

MyObserver observer;
imrtc::capi::Engine engine("wss://rtc.example.com/v1/ws", "win-8f3a2b");
engine.setObserver(&observer);
engine.login(tokenFromYourServer);

// 在你自己的定时器里，~200ms 一次：
engine.tick();
```

纯 C 的接法见 `capi/include/imrtc/imrtc_c.h` 的注释，形状是一样的：
`imrtc_v1_engine_create` → 填一张 `imrtc_v1_observer` 函数指针表 →
`imrtc_v1_login` → 循环 `imrtc_v1_engine_tick` → `imrtc_v1_engine_destroy`。

**token 从哪来**：你自己的服务端调 `/v1/tokens` 换。Demo 走的是服务端内置的
免密登录（`-demo-login`），**那只在开发构建里有**，不要照抄进生产。

---

## 2. 三条会让你崩溃的规矩

这三条排在最前面，因为它们出问题时的现象都是「随机崩溃」，很难倒推。

### 2.1 回调发生在 Engine 的线程上，切回 UI 线程是**你的责任**

引擎**不自己起线程**——我们不知道你的事件循环长什么样，多起一条就等于把
「回调在哪个线程」这个问题甩给你。所有事情都由 `tick()` 推动。

于是有一个很省事的做法：**在 UI 线程上 tick**。那样回调也落在 UI 线程上，
你可以直接改界面。Demo 就是这么做的（`QTimer` 每 200ms 调一次 `tick()`），
并且每次回调都 `assert` 一遍这个前提还成立。

如果你在别的线程上 tick，就必须自己切：

```cpp
// Qt
QMetaObject::invokeMethod(widget, [=]{ /* 改界面 */ }, Qt::QueuedConnection);
// C#
dispatcher.Invoke(() => { /* 改界面 */ });
// MFC
PostMessage(hwnd, WM_APP_CALL_STATE, 0, 0);
```

引擎的**方法**也一样：`Connection` 不是线程安全的，
**所有方法（含 `tick`）必须在同一个线程上调**。

### 2.2 回调里给的指针只在**该次回调期间**有效

```cpp
void on_call_received(void* u, const imrtc_v1_call_invite* invite) {
  g_callId = invite->call_id;          // ❌ 悬垂指针，回调返回后那块内存就没了
  g_callId = std::string(invite->call_id);  // ✅ 拷一份
}
```

数组同理（`on_active_speakers` / `on_network_quality` 的 `count` 个元素）。
用 `CallEngine.hpp` 的话这一层已经替你拷好了，参数都是 `std::string` / `std::vector`。

### 2.3 `imrtc_v1_engine_destroy` **阻塞到回调静默**，返回之后绝不会再有回调

这是刻意的，也是宿主是 C# / Java 这类 GC 语言时最要紧的一条——
GC 没法替你保证「回调打进来的时候那个对象还活着」。

```cpp
// 顺序：先摘观察者，再销毁。
engine.setObserver(nullptr);
// ~Engine() 里会调 imrtc_v1_engine_destroy，阻塞到静默为止
```

C# 侧尤其注意：**委托必须自己保住引用**，否则 GC 会在引擎还持有函数指针时把它回收掉：

```csharp
// ❌ 临时委托会被 GC
native.SetObserver(new Observer { OnCallEnd = (a,b,c,d) => {...} });
// ✅ 用字段保住
private readonly OnCallEndDelegate _onCallEnd;   // 与 Engine 同寿
```

---

## 3. 生命周期

```
create ──► set_observer ──► login ──► [tick tick tick …] ──► logout ──► destroy
                                          │
                                          ├─ call / accept / hangup / join_room …
                                          └─ 回调从这里抛出来
```

- **`tick()` 是心跳、请求超时、退避重连的唯一动力**。不 tick 就等于断线。
  粒度 200ms ~ 1s 都行；越大，状态跳转看起来越迟钝。
- **`update_token()` 换票**：下次重连才生效，**不打断当前连接**。
  票快过期时提前换，不要等被踢。
- **`logout()`** 会本地合成一条 `onCallEnd(reason=network)`——
  中途登出的通话必须在你的记录里留下痕迹，否则它会凭空消失。

---

## 4. 回调 → 界面该画什么

| 回调 | 你该做的 |
|---|---|
| `on_connected(session_id, resumed)` | 切到「已连接」。`resumed=true` 表示是断线恢复，不必重建界面 |
| `on_disconnected` | 显示「正在重连…」。**不要**清空通话——引擎在重连 |
| `on_kicked_out` | 回登录页换票。别原地重试，那是拿同一把坏钥匙敲同一扇门 |
| `on_error(code, name, for_type)` | 见 §5 |
| `on_call_received(invite)` | 弹来电页 / 来电横幅 |
| `on_call_begin(begin)` | 切到通话中，起计时器 |
| **`on_call_end(end)`** | **所有结束分支的唯一出口。**收界面 + 落记录，只在这里落 |
| `on_call_missed(missed)` | 通话中被第三个人呼叫、服务端已替你回了忙线。**不要弹来电页**，只写一条未接记录 |
| `on_handled_on_other_device` | 同账号别的设备接了/拒了。收掉来电页，别再响 |
| `on_user_enter / leave / accept / reject / no_response` | 更新九宫格里那一格 |
| `on_user_audio_available / video_available` | 格子上的静音角标 / 有没有画面 |
| `on_active_speakers` | 「谁在说话」的绿色描边 |
| `on_network_quality` | 弱网提示 |
| `on_room_joined / left / closed` | 会议房的进出 |
| `on_call_cancelled / rejected / busy / no_answer` | **便利回调，只在 1v1 抛**。它们**不是终局**——终局永远是 `on_call_end` |

---

## 5. 错误码 → 你自己的文案

**我们只给码和机读名，不给给用户看的文案。** 跨 ABI 传本地化字符串是个坑
（编码、生命周期、谁负责翻译都说不清），所以多语言完全在你这边：
拿 `code` 查你自己的本地化表。

`imrtc_v1_error_name(2007)` 返回的是 `"NotLoggedIn"` 这样的**符号名**，
给开发者和日志看的，**不要直接显示给用户**。

必须处理的那几个：

| 码 | 名 | 建议文案（中文） | 建议动作 |
|---|---|---|---|
| 1101 | `token_invalid` | 登录已失效，请重新登录 | 回登录页 |
| 1102 | `token_expired` | 登录已过期，请重新登录 | 换票后重试 |
| 1104 | `kicked_out` | 您的账号已在其他设备登录 | 回登录页 |
| 1202 | `room_full` | 通话人数已满 | 提示，留在原地 |
| 1205 | `room_closed` | 通话已结束 | 收界面 |
| 1402 | `call_ended` | —— | **静默吞掉**，不弹任何东西 |
| 1405 | `invalid_call_state` | —— | 一般是界面按钮该禁没禁，查自己的状态 |
| 1406 | `too_many_callees` | 最多只能邀请 8 人 | 提示 |
| 1407 | `not_call_owner` | 只有发起人可以邀请他人 | 把加人按钮藏起来 |
| 1408 | `already_in_call` | 您正在另一通电话中 | 提示 |
| 2001 | `device_permission_denied` | 需要麦克风 / 摄像头权限 | 引导去系统设置 |
| 2002 | `device_not_found` | 没有找到可用的设备 | 降级为语音 |
| 2003 | `network_unreachable` | 网络连接失败 | 引擎在自动重连，只提示 |
| 2004 | `signaling_timeout` | 网络较慢，请重试 | 提示 |
| 2005 | `invalid_state` | —— | 同 1405，查自己的状态 |
| 2007 | `not_logged_in` | —— | 你在 `login()` 之前调了业务方法 |

其余的码（协议层、媒体协商层）归到一句通用文案即可，例如
「通话出了点问题，请重试」，同时把 `code` 与 `name` 写进日志。
完整 45 个码见 `im-rtc-server/docs/conformance/error_codes.json`。

---

## 6. 五个一定会踩的坑

### 6.1 红按钮：**动作按「有没有 call」分叉，文案按人数分叉**

这两件事分叉的依据**不一样**，混在一起就必错一边。

```cpp
// 动作：只看是不是会议房
if (isMeetingRoom)              engine.leaveRoom();  // join_room 进来的，没有 call
else if (phase == Outgoing)     engine.cancel();     // 主叫，接通前
else if (phase == Incoming)     engine.reject();     // 被叫，接通前
else                            engine.hangup();     // 1v1 与**群通话**，接通后

// 文案：只看人数
const QString caption = (isGroup || isMeetingRoom) ? "离开" : "挂断";
```

于是**群通话上写着「离开」，调的却是 `hangup()`**——这一格最容易写错。

两个方向的坑各踩过一次：

- **会议房用了 `hangup()`** → 被本地拒成 `2005`，红按钮点了没反应（Web 端炸过）。
- **群通话用了 `leaveRoom()`** → 更隐蔽：界面看起来正常退出了，但**离开者永远收不到
  `on_call_end`**。服务端对 `room.leave` 只广播 `room.participant_left`，
  而协议 §4 规则 6 规定「离开的人自己收 `ended{hangup, ended_by:<自己>}`」——
  那是 `call.hangup` 才会产生的。后果是他那通电话的记录永远落不下来，
  也违反不变量 I1（`on_call_begin` / `on_call_end` 各恰好一次）。
  **本仓的 Demo 就是这么写错的**，在真服务端上跑群通话时才发现。

### 6.2 通话时长用 `on_call_end` 给的 `duration_sec`，**不要自己拿时间戳减**

本地时钟与服务端不同步，自己算出来的时长会和账单、和对端的记录都对不上。
未接通时这个值恒为 0。

### 6.3 便利回调不是终局

`on_call_busy` / `on_call_no_answer` / `on_call_rejected` / `on_call_cancelled`
**只在 1v1 抛**（群通话请看成员事件），而且它们后面**一定还会跟一条
`on_call_end`**。所以：用它们弹提示可以，用它们收界面、写记录不行——
那样群通话会一条记录都没有，1v1 会写两条。

### 6.4 `1402 call_ended` 必须静默吞掉

「你挂断的同时对方也挂断了」是常态，不是错误。弹出来只会让用户困惑。

### 6.5 收到 `on_kicked_out` 就回登录页，**别自己重试**

它有两个来源，你不需要分辨，处置是同一个：

- **WS 关闭码 4403**（同一账号在别处登录）——文案「您的账号已在其他设备登录」。
- **WS 关闭码 4401 连续 3 次**（票是废的）。重连带的是**同一枚 token**，
  所以「换新 token 后重连」这句话只有配上一个上限才成立，否则一枚废票能自己
  重试到天荒地老。Web 端实测过：服务端重启换了签名密钥，一个没关的标签页
  重试到第 19 次还在敲，日志里全是 `token_invalid`，把真正的问题淹掉了。
  引擎侧的上限是 3 次（`Connection::kMaxAuthFailures`），到了就停止重连并抛
  `on_kicked_out`。

**抛出之后引擎不会再自己重连**，等你换票重新 `login()`。

---

## 7. 渲染路径 A：把画面交给引擎

`imrtc_v1_attach_view(engine, uid, nativeHandle)`：你给一个原生窗口句柄
（macOS `NSView*`、Windows `HWND`），引擎把那个人的画面画进去。零拷贝，性能最好，
**默认推荐**。传 `nullptr` 卸载。

自绘宿主走路径 B（原始帧回调），那条口子等媒体落地后开。

### 7.1 Qt 里怎么拿到句柄

```cpp
class VideoSurface : public QWidget {
public:
  explicit VideoSurface(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_NativeWindow, true);            // 真的有一个 NSView / HWND
    setAttribute(Qt::WA_DontCreateNativeAncestors, true);  // 别把祖先一路提升
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
  }
  void* handle() { return reinterpret_cast<void*>(winId()); }
};
```

`WA_DontCreateNativeAncestors` 不是可选项：不加的话 Qt 会把整条父链都变成原生窗口，
连累其他控件的绘制与 z 序。

### 7.2 **原生子窗口会盖住同一个窗口里所有 Qt 绘制**

这是最容易吃亏的一条，而且**与 Qt 的 z 序无关**——`raise()` / `stackUnder()` 都没用。
macOS 上原生子窗口按 NSView 的兄弟顺序合成，Qt 自己画的内容一律在它们之下。

后果很具体：格子上的名字标签、静音角标、"正在说话"的绿色描边，只要是
`paintEvent` 画的，画面一来就**整块消失**。

解法是把这些东西放进**画面之后创建的另一个原生子窗口**：

```cpp
surface = new VideoSurface(tile);      // 先建画面
surface->show();
chrome  = new TileChrome(tile);        // 再建外壳，于是它在上面
chrome->setAttribute(Qt::WA_TranslucentBackground, true);
chrome->setAttribute(Qt::WA_TransparentForMouseEvents, true);  // 别挡住点击
chrome->show();
```

参考实现见 `demo/VideoTile.cpp` 的 `TileChrome`。

### 7.3 **原生视图的尺寸滞后于 Qt 的 `resizeEvent`**

`QWidget::resizeEvent` 触发时，Qt 还没把新几何推给底层的 `NSView`。
这时去读 `view.layer.bounds` 拿到的是**上一次**的尺寸，而且**不会再有第二次
resizeEvent 来纠正**——画面就永远卡在初始大小，格子边上露出一条底色。

实测数据：格子 157×92、Qt 控件 153×88，而 layer 停在 116×86（那是格子的最小尺寸）。

所以同步几何要**以 Qt 控件的尺寸为准**，或者在原生那一侧用 AppKit 自己的机制
（`NSViewFrameDidChangeNotification` / `autoresizingMask`）。

**两种伸缩机制不能同时开**：试过 `autoresizingMask` 叠加显式 `frame`，
结果两者相乘——控件 153×88，层变成 190×90。

### 7.4 生命周期：**先 detach，再销毁窗口**

```cpp
engine.attachView(uid, nullptr);   // 先摘
delete surface;                    // 再拆
```

反过来的话引擎手里剩的是一个已销毁的 `NSView` / `HWND`，下一帧画上去就崩，
而且崩在引擎的线程上，栈里看不到你的界面代码。

### 7.5 截图：`QWidget::grab()` 会抓到**上一帧**

做 UI 回归时会踩：`grab()` **能**看到原生子窗口，但内容可能滞后一帧。
实测（Qt 6.8.3 / macOS，逐像素平均每通道差值）：

| 抓法 | 与系统合成的差 |
|---|---|
| `QWidget::grab()` 单独跑 | **5.11 / 255**（原生层只画了一半） |
| 先跑一次 `QScreen::grabWindow` 再 `grab()` | 0.54 / 255 |
| `QScreen::grabWindow(winId)` | 基准 |

试过 `[CATransaction flush]`，没用。**带画面的界面要截图，用
`QScreen::grabWindow`**（它在某些环境需要「屏幕录制」授权，拿不到就要说清楚，
别拿一张滞后的图当证据）。

### 7.6 **本端预览走另一条口子**

```c
imrtc_v1_attach_view(engine, "bob", handle);   // 远端：按 uid
imrtc_v1_attach_local_view(engine, handle);    // 本端：没有 uid 这一说
```

不是同一个函数，也**不要**期待 `attach_view(你自己的 uid, …)` 能用：

- 引擎**不知道你的 uid**。那是你与服务端之间的事，token 里有，引擎不解析。
- 本端画面来自**采集侧**，根本不是一条「远端轨道」，没有 trackId 可查。

硬要复用就得约定一个魔法 uid（空串？`"self"`？），那是给将来埋雷。

1v1 那一屏正好两条都要：远端铺满 → `attach_view(对方uid, …)`，
右下角 160×90 的小窗 → `attach_local_view(…)`。摄像头还没开时可以先挂上，
开了自然就有画面；关摄像头**不需要**摘，画面自己没了。

> 这条口子是做 1v1 那一屏时才发现缺的，2026-09-07 加进 C ABI。
> **追加式变更**：新增一个符号，已有的一个都没动，老宿主不受影响。

### 7.7 布局：**隐藏控件不腾地方**

一个 Qt 的坑，不是原生窗口特有的，但在这里最容易撞：想让画面铺满时，
把原本占位的头像 / 名字 `hide()` 掉是**不够的**——如果那个布局里有
`addStretch()`，弹簧会把空间全吃掉，画面只分到十几个像素高。
实测：画面拿到 520×**16**。

用 `QStackedWidget` 分成两页（头像页 / 画面页）就没这个问题。
参考 `demo/CallOverlay.cpp` 里 `soloPane_` 的构造。

### 7.8 当前状态

上面这些**宿主侧**的坑已经在 `demo/` 里走通并有回归测试
（`demo/tests/NativeSurfaceTest.cpp`）。但**引擎侧还没有画面**：
没有媒体适配器时 `imrtc_v1_attach_view` 直接返回，连轨道都不去解析。
也就是说这条线现在是**接好了但没通电**——等 `WebRTCAdapter` 落地就自动生效，
你这边不用改。

## 8. 各宿主怎么接

### Qt

看 `demo/EngineBridge.h/.cpp` —— 那是这份指南里所有规矩的可运行版本：
`QTimer` 驱动 `tick()`、25 个回调翻译成 Qt 信号、`assert` 核线程前提。

**Qt 只是宿主之一**：引擎里没有一行 Qt（含 QtCore），
所以「你用 Qt 5 还是 Qt 6」与引擎无关。

### MFC / Win32

`tick()` 挂 `SetTimer`，回调里 `PostMessage` 回主线程。
渲染路径 A 直接把 `HWND` 传给 `imrtc_v1_attach_view`。

### C# / WPF

`[DllImport]` + `[UnmanagedFunctionPointer(CallingConvention.Cdecl)]`。三件事：

1. **委托要用字段保住**（见 §2.3），否则 GC 会先回收它。
2. **`imrtc_v1_bool` 是 `int32_t` 不是 `_Bool`**——C# 的 `bool` 默认按 4 字节
   BOOL 编组，直接对上 C 的 `_Bool` 会读到隔壁的字节。我们已经按 4 字节定义了，
   你照着 `int` 或 `[MarshalAs(UnmanagedType.Bool)]` 用即可。
3. 每个结构体第一个字段是 `struct_size`，**由你填 `Marshal.SizeOf`**。
   填错会被拒（`1004`），这是版本闸，不是找茬。

---

## 9. 构建与平台

### 结构体的兼容规矩

第一个字段永远是 `uint32_t struct_size`，**由宿主填 `sizeof(...)`**。
字段只许追加、永不重排、永不删除；枚举显式赋值，绝不依赖声明顺序。
所以你可以先升级 `.dll` 再升级头文件，反过来也行。

### macOS

- **部署目标**：引擎与 C ABI 是 **macOS 11+**。`demo/` 是 12.0，
  那是 **Qt 6.8 的**要求，不是我们的。
- **权限说明必须由你的 bundle 声明**，SDK 代劳不了：
  `NSMicrophoneUsageDescription` / `NSCameraUsageDescription`，
  以及 Hardened Runtime 的 audio-input / camera entitlement。
  缺了的话 macOS 会在第一次取设备时**直接杀进程**，不是返回错误。
  抄 `demo/Info.plist.in`。
- **Xcode 26 + Qt 6.8 会撞 `ld: framework 'AGL' not found`**。
  macOS 26 SDK 删掉了 `AGL.framework`，而 Qt 6.8 的 `FindWrapOpenGL.cmake`
  在找不到时会**无条件回退到硬编码的 `-framework AGL`**。
  **不能用 `find_library` 探测**——AGL 还留在运行系统的
  `/System/Library/Frameworks/` 下，只是从 SDK 里删了，而 `ld` 只看 SDK。
  修法（放在 `find_package(Qt6)` **之前**）：

  ```cmake
  if(APPLE AND NOT DEFINED CACHE{WrapOpenGL_AGL})
    set(_sdk "${CMAKE_OSX_SYSROOT}")
    if(NOT _sdk)
      execute_process(COMMAND xcrun --show-sdk-path
                      OUTPUT_VARIABLE _sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()
    if(NOT EXISTS "${_sdk}/System/Library/Frameworks/AGL.framework")
      set(WrapOpenGL_AGL "${_sdk}/System/Library/Frameworks/OpenGL.framework"
          CACHE FILEPATH "AGL shim")
    endif()
  endif()
  ```

### Windows

- 目标 **Windows 10+**，x64。
- `imrtc_c.h` 在 Windows 上走 `__declspec(dllimport)`；
  链你自己那份 `.lib` 导入库，运行时把 `.dll` 放在可执行文件旁边。
- 渲染路径 A 收 `HWND`。
- **⚠️ 本仓的 Windows 侧一次都没编译过**（开发机是 macOS）。
  第一次在 Windows 上构建时如果撞到问题，那大概率是真问题，不是你用错了——
  请直接反馈。

---

## 10. 当前构建**没有**什么

不写清楚这一段的话，你会按 Demo 的外观推断出错误的结论。

| | 状态 |
|---|---|
| 登录 / 心跳 / 断线重连 | ✅ macOS 验过 |
| 拨号 / 来电 / 接听 / 拒接 / 取消 / 挂断 | ✅ macOS 验过 |
| 群通话成员事件、进出房间、通话记录 | ✅ macOS 验过 |
| **声音与画面** | ⬜ **没有**。见下 |
| 设备枚举与热插拔 | ⬜ 没有 |
| 共享屏幕 | ⬜ 没有 |
| **Windows 侧的任何验证** | ⬜ **一次都没编译过** |

**为什么没有声音画面**：媒体面（`MediaAdapter` 契约 + `MediaPlane` 接线）
已经写好并测全，真正缺的只有一个 `WebRTCAdapter.cpp`。
libwebrtc 的桌面预编译包**没有 macOS x86_64 的产物**，而开发机是 Intel Mac，
所以这一刀已决定推迟，等 Apple Silicon 或 Windows 机器。

影响面被接口关死在那**一个还没写的文件**里：协议层、状态机、连接层、C ABI
都不受影响，你现在就可以照着本指南把信令这一半接完，媒体接上之后
你这边一行都不用改。

进度以 `im-rtc-server/docs/CLIENT_PARITY.md` §1.1 为准——
那是唯一真相源，**别信任何口头承诺**。
