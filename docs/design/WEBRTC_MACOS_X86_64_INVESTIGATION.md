# macOS x86_64（Intel）音视频媒体层调研

> 背景：`WebRTCAdapter`（`engine/src/media/`，尚未落地的文件）需要静态链接 libwebrtc。
> 现有三端交付用的预编译源 `shiguredo-webrtc-build` 没有 macOS x86_64 产物，导致 Intel Mac 上
> 桌面端只能跑纯信令模式（无画面无声音）。本文档记录「能不能补上、怎么补」这个问题的调研过程与结论，
> 供以后不用从头重查。原始决定见 `current_task.archive.md`
> 「✅ 已决定（2026-09-06）：媒体推迟，Intel Mac 上先不支持声音与视频」一节。

## TL;DR（2026-09-23）

- **shiguredo 官方到今天（最新 `m155.8059.1.0`，2026-09-19 发布）依旧没有 `macos_x86_64` 产物**，而且
  **他们自己的构建脚本 `run.py` 已经在代码层面砍掉了这个 target**（2022-06 下线，不是「CI 不编但脚本还能用」）。
  照他们脚本传参数编是走不通的。
- 查了三个社区/同行方案，只有一个（`tg_owt`）技术上站得住脚，但成本比预想的高：
  | 方案 | 能不能用 | 卡点 |
  |---|---|---|
  | `webrtc-sdk/libwebrtc`（m150.7871.xx） | ❌ | macOS 只有 ObjC XCFramework 一个 universal slice，Windows/Linux 又是另一套自定义 C 壳 API，三端三套接口；且最近两个 patch release 里这个 slice 直接消失过，维护不稳定 |
  | `bengreenier/webrtc` darwin-x64 | ❌（沿用旧结论） | 冻在 2023 年 M115，与目标 M150 差 35 个里程碑 |
  | `stasel/WebRTC` XCFramework | ❌（沿用旧结论） | ObjC API，Windows 用不了，两套适配器 |
  | **`tg_owt`**（`desktop-app/tg_owt`，Telegram Desktop 的 WebRTC 分支） | 🟡 技术可行，**成本没有想象中低** | 见下 |
  | 自己从源码编原生 WebRTC（绕开 shiguredo，直接用 Google 官方 depot_tools/GN） | 🟡 备选 | 无现成脚本，GN 参数要自己摸，没有 shiguredo 的 macOS patch |

- **磁盘**：本机重启后约 40GB 可用，技术上可以编（源码 checkout ~5.6GB + 工具链 1-3GB + 编译产物预估
  10-20GB），但没有安全余量，且 macOS 的「可用空间」里含 purgeable（本地 Time Machine 快照等），实测可能比
  显示的更紧张。更稳的做法是用外接移动硬盘（必须先格式化成 **APFS**，exFAT/NTFS 不支持符号链接和 Unix 权限，
  depot_tools/gn/ninja 这套工具链在上面会编译失败）。
- **GitHub Actions 云端编译**：重新核实（09-22）发现比预想的更紧张——`macos-13` 已彻底下架，`macos-14`
  （Intel）已进入弃用、**2026-11-02 彻底停止支持**，唯一剩的 Intel 选项 `macos-15-large` / `macos-15-intel`
  是**付费大机型 runner**，不是免费默认档（免费的 `macos-latest` 现在指向 arm64）。这条路要么尽快做，要么
  改用租用的云端 Intel Mac（AWS EC2 Mac1/Mac2、MacStadium 等）手动编译，一次性任务没必要为此配置付费 CI。

## 详细调研记录

### 1. shiguredo 现状复核（2026-09-22 / 09-23）

- 最新 release `m155.8059.1.0`（2026-09-19）资产列表：`webrtc.macos_arm64.tar.gz` 有，
  `webrtc.macos_x86_64.*` **没有**（列表见下，仅摘录相关行）：
  ```
  webrtc.macos_arm64.tar.gz
  webrtc.windows_arm64.zip
  webrtc.windows_x86_64.zip
  WebRTC.xcframework.zip   # 解包确认：只有 ios-arm64 / ios-arm64-simulator 两个 slice，没有 macOS
  ```
- `run.py`（shiguredo-webrtc-build 主仓）第 1392 行：
  ```python
  elif platform.system() == "Darwin":
      return target in ("macos_arm64", "ios", "ios_sdk")
  ```
  `macos_x86_64` 不在白名单里，传这个 target 会被脚本主动 `raise Exception` 拒绝。`DEVELOPMENT.md` 里还留着
  `macos_x86_64` 的字样，是没更新的旧文档，不代表工具还支持。
- 结论：**「照 shiguredo 脚本换个 target 参数编」这条路已经不存在**，往下想办法都得绕开这套脚本。

### 2. `webrtc-sdk/libwebrtc`（2026-09-22 查）

- GitHub 组织 `webrtc-sdk`，release 版本号 `libwebrtc.m150.7871.xx`，和本项目目标版本号一致，一度以为是现成答案。
- 下载 `libwebrtc.m150.7871.01` 的 `WebRTC.xcframework.zip` 解包确认：
  - 里面确实有 `macos-arm64_x86_64`（arm64+x86_64 通用）一个 slice。
  - 但暴露的头文件全是 **ObjC API**（`RTCPeerConnectionFactory`、`RTCConfiguration` 等），不是原生 C++ 接口。
- 下载同版本 `libwebrtc-win-x64-release.zip` 对比：Windows/Linux 产物是**另一套自定义 C++ 壳**
  （`rtc_peerconnection.h`、`libwebrtc.h`），跟上面的 ObjC API、跟 shiguredo 给 Windows/arm64-mac 用的原生
  Google API **三者互不相同**。用这个源等于要维护三套适配器方言，比现状更糟。
- 稳定性也有问题：`m150.7871.02`、`m150.7871.03` 两个 patch release 里 `WebRTC.xcframework.zip` 直接消失了，
  只有 `.00`、`.01` 有，构建似乎偶尔失败，不是稳定维护的目标。
- **结论：排除。**

### 3. `tg_owt`（`desktop-app/tg_owt`，Telegram Desktop 的 WebRTC 分支，2026-09-23 查）

用户提出「Telegram 的音视频就支持 Intel macOS」，顺着这个线索查到 Telegram Desktop 用的不是 shiguredo，而是
自己维护的 WebRTC 分支。

**基本信息**
- 仓库：`desktop-app/tg_owt`，**BSD-3-Clause**（协议和本项目现有依赖策略兼容），最近一次更新在
  2026-09-14（查证当周内），活跃维护。
- 注意区分：Telegram 还有个 `tgcalls` 仓库（通话信令/调度层）是 **LGPL-3.0**，不能直接用；
  能用的是 `tg_owt` 这个纯 WebRTC 分支本身。
- 顶层 `CMakeLists.txt` 是独立的 `project(tg_owt ...)`，`add_library(tg_owt)`，默认
  `BUILD_SHARED_LIBS OFF`（产出静态库），**可以脱离整个 Telegram Desktop 工程单独编**。
- 第 59-66 行明确区分 arm64 / x86_64 两条分支（`CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64"` → `set(is_x64 1)`），
  确认 x86_64 是被支持的目标。
- 内部源码路径是标准 Google WebRTC 目录结构（`pc/external_hmac.cc`、
  `modules/audio_device/mac/audio_device_mac.cc` 等），API 风格应该比 `webrtc-sdk` 那套自定义 C 壳更接近
  shiguredo 原生 API，适配器改造成本相对更低（未实测验证）。

**代价：不是自包含的单一产物**
`CMakeLists.txt` 第 148-155 行无条件要求链接 8 个外部库，不像 shiguredo 的 tar.gz 那样全部内置：
```
link_openssl(tg_owt)
link_ffmpeg(tg_owt)
link_opus(tg_owt)
link_libabsl(tg_owt)
link_libopenh264(tg_owt)
link_libsrtp(tg_owt)
link_libvpx(tg_owt)
link_crc32c(tg_owt)
```
这些库要么走系统 `find_package`/`pkg-config`（「packaged build」模式，依赖 Homebrew 装好的版本，**不适合分发
给宿主的 SDK**，因为宿主机器不一定有 Homebrew），要么走 `TG_OWT_XXX_INCLUDE_PATH` 手动指向自己编译好的静态库
（Telegram 生产实际用的模式）。

**Telegram 官方构建流程与数字**（`telegramdesktop/tdesktop` 仓库 `docs/building-mac.md` +
`Telegram/build/prepare/prepare.py`）：
- 官方文档原话：*"The full build process will require approximately **55 GB** of free space. This includes:
  **~35 GB** for libraries (when building for both x64 and arm64 architectures), **~20 GB** for the
  compiled Telegram app"*。我们只需要静态库，**~20GB 的 App 编译那部分可以省掉**，但「依赖库」这 ~35GB 是绕不开的。
- `prepare.py`（近 2000 行）是把上面 8 个依赖库全部从源码编出来的脚本，**从头到尾按「arm64 + x86_64 一起编，
  最后 `lipo -create` 合并成 universal」的方式写死**（`configureFFmpeg arm64` / `configureFFmpeg x86_64` 各跑
  一遍这种写法贯穿全文件）。想只编 x86_64 单架构、省时间省盘，得把脚本里每个依赖的构建函数都手动拆开去掉
  arm64 那一半，属于机械但不轻的改动。
- **FFmpeg 许可证核查**：翻了 `prepare.py` 里 FFmpeg 的实际 `./configure` 参数（约第 1201-1360 行），
  **没有开 `--enable-gpl` / `--enable-nonfree`，没有链 libx264**，编解码用的是 OpenH264 / libvpx / dav1d /
  Opus（都是宽松协议）+ Apple 自己的硬件解码（`--enable-hwaccel=h264_videotoolbox` 等）。不会踩到 GPL。
  但 **FFmpeg 默认协议是 LGPL**，静态链接 LGPL 库本身仍带有「需支持重新链接替换该库」一类的开源义务，
  跟前面排除掉的 `tgcalls`（LGPL-3.0）是同一类问题，只是程度更轻——**是否接受，需要单独决策**，
  这个项目目前的依赖策略（IXWebSocket 选 BSD-3-Clause）是刻意全部避开 copyleft 协议的。

**未解决的问题（下一步如果要推进，先查这些）**
1. FFmpeg 在 tg_owt 里到底是不是通话核心路径（音视频编解码/传输）必需，还是只服务于 Telegram 自己的
   视频消息/GIF 播放这类附加功能——如果是后者，有没有可能从 `tg_owt` 源码里摘掉这个依赖，绕开 LGPL 问题。
2. 能不能把 `prepare.py` 改成只编 x86_64 单架构（跳过 arm64 那一半），把 ~35GB 的量级降下来、缩短编译时间。
3. `tg_owt` 编出来的 API 和 shiguredo 给 macOS arm64 / Windows 用的原生 API 具体差多少，
   `WebRTCAdapter.cpp` 要改到什么程度（三端会变成「两个不同版本/来源的 libwebrtc」，需要评估行为一致性风险，
   参照一致性向量跑一遍）。

### 4. 备选：绕开 shiguredo，自己用 Google 官方 depot_tools/GN 编原生 WebRTC

- 官方文档给的 checkout 大小：**Mac（含 iOS 支持）约 5.6GB**（用 `--no-history` 不带 git 历史的前提下）。
  这个数字本身不大，比 tg_owt 那条路依赖链的 ~35GB 小得多。
- 代价：没有现成脚本可抄，GN 参数要自己对着 shiguredo 的 patch 列表摸索，编出来的库**不带 shiguredo 那几个
  macOS 专属 patch**（比如 `macos_screen_capture.patch`）——如果不需要屏幕共享功能，这个影响应该可以接受。
- 还没有验证过具体可行性，只是从官方文档数字上看，磁盘成本比 tg_owt 路线低。

### 5. GitHub Actions 云端编译可行性复核（2026-09-22）

查 `actions/runner-images` 仓库最新镜像清单（非记忆，是当场查的实际列表）：
- `macos-13` **已经不在清单里**，彻底下架，不是「能用到 2027」。
- `macos-14`（Intel/x64）**已进入弃用流程**（2026-07-06 开始），**2026-11-02 彻底停止支持**——离现在只剩几周。
- 唯一还在的 Intel/x64 macOS 选项是 `macos-15-large` / `macos-15-intel` 标签，属于 **付费 larger runner**
  （2026 年价格约 $0.062/分钟起），免费默认档 `macos-latest` 现在指向 arm64。
- shiguredo 自己的 CI（`build.yml`）编 `macos_arm64` 用的是标准 `runs-on: macos-26`（免费档），
  侧面说明单 target Release 构建在标准盘位下是够用的——这个数据点支持磁盘不是本质障碍，主要是 target 支持
  被砍了。

### 6. 磁盘与外接盘注意事项

- 本机重启后约 40GB 可用，但 macOS 的「可用空间」统计包含 purgeable（本地 Time Machine 快照等系统认为能
  随时腾出来的空间），持续大量写入时系统腾挪可能跟不上，实际可能比显示的紧张。编译前可以查一下：
  ```bash
  diskutil apfs list | grep -i "capacity in use by snapshots\|purgeable"
  tmutil listlocalsnapshots /
  ```
- 外接 USB 移动硬盘可以用，但**必须先用「磁盘工具」格式化成 APFS**——exFAT/NTFS 不支持符号链接和 Unix
  可执行权限，depot_tools/gn/ninja 这套工具链在上面会直接编译失败。格式化会清空盘上原有数据，动手前先确认
  盘上没有需要保留的东西。
- 如果是机械硬盘（非 SSD），ninja 编译海量小文件随机读写会明显更慢，预期编译时间比 SSD 长数倍。

## 现状结论（截至 2026-09-23）

三条路线里，`tg_owt` 是目前唯一经过验证、协议基本兼容、且有真实产品（Telegram Desktop）在 Intel Mac 上
量产验证的方案，但成本是「跑一遍 Telegram 自己的原生依赖构建流水线」（~35GB+、大概率数小时）+ 处理 LGPL
FFmpeg 的问题 + 重写适配器，不是「改个编译参数」那么轻。是否投入，取决于：
1. FFmpeg 能不能从 tg_owt 里摘掉（回避 LGPL）——待查。
2. 团队能不能接受 LGPL FFmpeg 的开源义务——待决策，不是技术问题。
3. 是否值得为 x86_64 单架构去改造 Telegram 那份写死双架构的构建脚本。

尚未决定动手，也未占用任何磁盘/机器资源。下一步如果要推进，从「问题 1」（FFmpeg 是否为核心路径必需）
查起最省成本。
