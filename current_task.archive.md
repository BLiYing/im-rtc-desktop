# current_task 归档 —— im-rtc-desktop

> 只读。**活快照在 [current_task.md](current_task.md)**，退休的细节落到这里，不许再搬回去。
> 更细的历史在 `git log`。

---

## 2026-09-03 ~ 2026-09-06：建仓期（P5 未开工，只有文档与体量门禁）

以下是 2026-09-06 之前的活快照原文。这一版的核心判断已被 2026-09-06 的「引擎零 Qt 依赖 + 对外纯 C ABI」取代——
特别是「Qt 版本：是否兼容 Qt 5.15 …… P5 开工前要问清楚」这条**已作废**（引擎不碰 Qt，宿主用 Qt 几都无关）。

### Current Task — im-rtc-desktop（C++17 Engine + Qt Demo）

> **活快照**：只记当前状态，**就地覆盖、不追加**。历史见 `git log`。
> 工程规范见 [CONVENTIONS.md](CONVENTIONS.md)；方案与分期见 `im-rtc-server` 的
> `docs/design/RTC_CALL_DESIGN.md` §8（桌面）与 §10（分期）。

## 当前焦点

**仓库刚建（2026-09-03），只有文档与体量门禁，尚无一行代码。**

本仓在分期里是 **P5**，排在 P0 协议 → P1 SFU → P2 Web → P3 iOS → P4 群通话之后。

**在此之前本仓的价值是「接收契约」**：协议与一致性向量从第一天起就必须考虑 C++ 端能实现——
帧结构不得依赖 JS/Swift 特有的数据表达（如可选字段的隐式 undefined、关联值枚举）。
P0 定协议时如果发现某个设计 C++ 侧别扭，**现在就提，别等 P5**。

## 下一步

1. **等 P0~P4**。期间只做一件事：**评审协议对 C++ 的友好度**，发现问题回 server 仓提。
2. **P5 第一刀**：CMake 骨架 + `CMakePresets.json`（windows-msvc / macos-clang）+
   libwebrtc 预编译包接入（锁定版本）+ `scripts/` 三件套。
3. **P5 第二刀**：`engine/`——signaling + state，**先跑通一致性向量**（不需要媒体、不需要 GUI）。
4. **P5 第三刀**：`media/WebRTCAdapter`（libwebrtc C++ API）+ 1v1 语音/视频，与 Web/iOS 互通。
5. **P5 第四刀**：Qt Demo 四屏 + 《Qt 接入指南》，交付给公司项目。

## 已知坑 / 限制

- **本机只有 macOS**：Windows 侧编译与验证需要集成方配合，时间未定。
  **不许把「macOS 过了」写成「桌面端完成」**——每次交付都要分平台说清楚。
- **腾讯等厂商没有桌面版含 UI 通话组件**：这是整个方案选择自建信令 + 自建 SFU 的直接原因之一。
  桌面端只能「媒体用 libwebrtc + 信令接自己的协议」，没有捷径。
- **libwebrtc 桌面预编译包**（`shiguredo-webrtc-build`）随 Chromium 里程碑更新，
  两平台必须同一版本号；API 变化由 `MediaAdapter` 接口隔离。
- **Qt 版本**：Qt 6 为主；是否兼容 Qt 5.15 取决于集成方现状，**P5 开工前要问清楚**。
- **裸指针回调是 C++ 端最常见的崩因**（对象先死、回调后到）。观察者一律 weak_ptr 或显式注销，
  见 CONVENTIONS §4。
- **跨平台策略已定，别再翻案**：五端不共享代码，共享「协议 + 状态机 + 测试向量」。

## 关联工程 / 常用命令

- **各端能力对照表：`../im-rtc-server/docs/CLIENT_PARITY.md`**（逐端逐特性状态的**单一真相源**，✅ 只写在那里，本文件不重复）。

- 五仓（本地同级 `/Users/liying/IOSProject/im-rtc/`）：
  [im-rtc-server](https://github.com/BLiYing/im-rtc-server)（**协议契约在这里，只读引用**）·
  [im-rtc-ios](https://github.com/BLiYing/im-rtc-ios) · [im-rtc-web](https://github.com/BLiYing/im-rtc-web) ·
  **im-rtc-desktop**（本仓）·
  [im-rtc-android](https://github.com/BLiYing/im-rtc-android)。
- 集成方：公司现有 Windows/Mac Qt 项目（不在本机，需对方配合）。
- 常用命令（脚本随 P5 落地）：
  ```bash
  ./scripts/install-hooks.sh                     # 新 clone 跑一次
  cmake --preset macos-clang && cmake --build --preset macos-clang
  ./scripts/test.sh                              # 唯一测试入口
  ```
