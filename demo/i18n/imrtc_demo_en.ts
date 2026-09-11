<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="en_US">
<context>
    <name>CallOverlay</name>
    <message>
        <source>纯信令模式：媒体还没接入，这个按钮现在不会有任何效果。</source>
        <translation>Signalling-only build: media is not wired up, so this button does nothing yet.</translation>
    </message>
    <message>
        <source>共享屏幕是后续期的事，当前构建没有。</source>
        <translation>Screen sharing is planned for a later phase; this build has none.</translation>
    </message>
    <message>
        <source>正在重连…</source>
        <translation>Reconnecting…</translation>
    </message>
    <message>
        <source>会议房间 %1</source>
        <translation>Meeting room %1</translation>
    </message>
    <message>
        <source>群通话 · %1 人</source>
        <translation>Group call · %1 people</translation>
    </message>
    <message>
        <source>视频通话</source>
        <translation>Video call</translation>
    </message>
    <message>
        <source>语音通话</source>
        <translation>Voice call</translation>
    </message>
    <message>
        <source>正在呼叫…</source>
        <translation>Calling…</translation>
    </message>
    <message>
        <source>%1 · %2</source>
        <translation>%1 · %2</translation>
    </message>
    <message>
        <source>静音</source>
        <translation>Mute</translation>
    </message>
    <message>
        <source>已静音</source>
        <translation>Muted</translation>
    </message>
    <message>
        <source>摄像头</source>
        <translation>Camera</translation>
    </message>
    <message>
        <source>已关闭</source>
        <translation>Camera off</translation>
    </message>
    <message>
        <source>共享屏幕</source>
        <translation>Share screen</translation>
    </message>
    <message>
        <source>共享中</source>
        <translation>Sharing</translation>
    </message>
    <message>
        <source>接听</source>
        <translation>Answer</translation>
    </message>
    <message>
        <source>拒绝</source>
        <translation>Decline</translation>
    </message>
    <message>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>离开</source>
        <translation>Leave</translation>
    </message>
    <message>
        <source>挂断</source>
        <translation>Hang up</translation>
    </message>
    <message>
        <source>纯信令模式 · 没有声音和画面</source>
        <translation>Signalling only · no audio or video</translation>
    </message>
</context>
<context>
    <name>DialPage</name>
    <message>
        <source>人数超了</source>
        <translation>Too many participants</translation>
    </message>
    <message>
        <source>群通话最多 %1 人，现在填了 %2 个。</source>
        <translation>A group call takes at most %1 people; you entered %2.</translation>
    </message>
    <message>
        <source>退出</source>
        <translation>Sign out</translation>
    </message>
    <message>
        <source>设置</source>
        <translation>Settings</translation>
    </message>
    <message>
        <source>单人通话</source>
        <translation>One-to-one call</translation>
    </message>
    <message>
        <source>对方 ID</source>
        <translation>Their user ID</translation>
    </message>
    <message>
        <source>例如 bob</source>
        <translation>e.g. bob</translation>
    </message>
    <message>
        <source>语音通话</source>
        <translation>Voice call</translation>
    </message>
    <message>
        <source>视频通话</source>
        <translation>Video call</translation>
    </message>
    <message>
        <source>多人通话（最多 %1 人）</source>
        <translation>Group call (up to %1)</translation>
    </message>
    <message>
        <source>成员</source>
        <translation>Members</translation>
    </message>
    <message>
        <source>bob、carol、dave</source>
        <translation>bob, carol, dave</translation>
    </message>
    <message>
        <source>发起群语音</source>
        <translation>Start group voice</translation>
    </message>
    <message>
        <source>发起群视频</source>
        <translation>Start group video</translation>
    </message>
    <message>
        <source>会议房间</source>
        <translation>Meeting room</translation>
    </message>
    <message>
        <source>房间号</source>
        <translation>Room number</translation>
    </message>
    <message>
        <source>8827-1190</source>
        <translation>8827-1190</translation>
    </message>
    <message>
        <source>加入房间</source>
        <translation>Join room</translation>
    </message>
    <message>
        <source>新建会议房</source>
        <translation>New meeting room</translation>
    </message>
</context>
<context>
    <name>EngineBridge</name>
    <message>
        <source>服务端没有返回 token（免密登录只在开发构建可用）</source>
        <translation>The server returned no token (passwordless login exists only in development builds)</translation>
    </message>
    <message>
        <source>建会议房失败：%1</source>
        <translation>Could not create the meeting room: %1</translation>
    </message>
    <message>
        <source>建会议房失败：服务端没有返回 room_id</source>
        <translation>Could not create the meeting room: the server returned no room_id</translation>
    </message>
    <message>
        <source>换进房票失败：%1</source>
        <translation>Could not get a room token: %1</translation>
    </message>
    <message>
        <source>换进房票失败：服务端没有返回 room_token</source>
        <translation>Could not get a room token: the server returned no room_token</translation>
    </message>
</context>
<context>
    <name>HistoryPage</name>
    <message>
        <source>清空通话记录</source>
        <translation>Clear call history</translation>
    </message>
    <message>
        <source>清空之后不可恢复，确定吗？</source>
        <translation>This cannot be undone. Clear it?</translation>
    </message>
    <message>
        <source>群通话</source>
        <translation>Group call</translation>
    </message>
    <message>
        <source>通话记录</source>
        <translation>Call history</translation>
    </message>
    <message>
        <source>清空</source>
        <translation>Clear</translation>
    </message>
    <message>
        <source>还没有通话记录。

这一页的每一行都由 onCallEnd 一个回调拼出来：
对方、方向、媒体类型、结束原因、时长。</source>
        <translation>No calls yet.

Every row on this page is built from a single onCallEnd callback:
who, which direction, which media, why it ended, how long it lasted.</translation>
    </message>
</context>
<context>
    <name>IncomingBanner</name>
    <message>
        <source>摄像头</source>
        <translation>Camera</translation>
    </message>
    <message>
        <source>已关闭</source>
        <translation>Camera off</translation>
    </message>
    <message>
        <source>拒绝</source>
        <translation>Decline</translation>
    </message>
    <message>
        <source>接听</source>
        <translation>Answer</translation>
    </message>
    <message>
        <source>纯信令模式：媒体还没接入，这个按钮现在不会有任何效果。</source>
        <translation>Signalling-only build: media is not wired up, so this button does nothing yet.</translation>
    </message>
</context>
<context>
    <name>LoginPage</name>
    <message>
        <source>正在登录…</source>
        <translation>Signing in…</translation>
    </message>
    <message>
        <source>登录</source>
        <translation>Sign in</translation>
    </message>
    <message>
        <source>im-rtc Demo</source>
        <translation>im-rtc Demo</translation>
    </message>
    <message>
        <source>SDK %1 · 经 C ABI 调引擎</source>
        <translation>SDK %1 · through the C ABI</translation>
    </message>
    <message>
        <source>服务器</source>
        <translation>Server</translation>
    </message>
    <message>
        <source>用户 ID</source>
        <translation>User ID</translation>
    </message>
    <message>
        <source>昵称</source>
        <translation>Nickname</translation>
    </message>
    <message>
        <source>选填</source>
        <translation>Optional</translation>
    </message>
    <message>
        <source>例如 alice</source>
        <translation>e.g. alice</translation>
    </message>
    <message>
        <source>http://127.0.0.1:8787</source>
        <translation>http://127.0.0.1:8787</translation>
    </message>
    <message>
        <source>集成方式</source>
        <translation>Integration</translation>
    </message>
    <message>
        <source>Kit（整套 UI）</source>
        <translation>Kit (full UI)</translation>
    </message>
    <message>
        <source>桌面端不提供 UI Kit：交付的是引擎 + C ABI + C++ 包装头。</source>
        <translation>The desktop product ships no UI kit: it delivers the engine, a C ABI and a C++ wrapper header.</translation>
    </message>
    <message>
        <source>Engine（自画 UI）</source>
        <translation>Engine (your own UI)</translation>
    </message>
    <message>
        <source>Demo 使用服务端内置的免密登录（仅开发构建可用）。
生产请改用你自己的 /v1/tokens 换票。
桌面端只有「自画 UI」一条路——本 Demo 就是那条路的参考实现。</source>
        <translation>This demo uses the server's built-in passwordless login (development builds only).
In production, mint tokens through your own /v1/tokens endpoint.
The desktop product offers only the “your own UI” path — this demo is its reference implementation.</translation>
    </message>
    <message>
        <source>当前为纯信令模式：能拨号、能进房、能收到全部状态回调，但没有声音和画面（媒体已决定推迟，等 Apple Silicon 或 Windows 机器）。</source>
        <translation>Signalling-only build: calls, rooms and every state callback work, but there is no audio or video. Media is deliberately deferred until an Apple Silicon or Windows machine is available.</translation>
    </message>
</context>
<context>
    <name>MainWindow</name>
    <message>
        <source>请填用户 ID。</source>
        <translation>Please enter a user ID.</translation>
    </message>
    <message>
        <source>取票失败：%1
服务端起来了吗？（scripts/dev.sh）</source>
        <translation>Could not get a token: %1
Is the server running? (scripts/dev.sh)</translation>
    </message>
    <message>
        <source>设置</source>
        <translation>Settings</translation>
    </message>
    <message>
        <source>%1 · 已恢复连接</source>
        <translation>%1 · connection resumed</translation>
    </message>
    <message>
        <source>%1 · 已连接</source>
        <translation>%1 · connected</translation>
    </message>
    <message>
        <source>正在重连…</source>
        <translation>Reconnecting…</translation>
    </message>
    <message>
        <source>已在别处登录，或票据已失效。请重新登录。</source>
        <translation>Signed in elsewhere, or the token expired. Please sign in again.</translation>
    </message>
    <message>
        <source>出错了：%1（%2），来自 %3</source>
        <translation>Error %1 (%2) from %3</translation>
    </message>
    <message>
        <source>先填一个对方 ID。</source>
        <translation>Enter a user ID first.</translation>
    </message>
    <message>
        <source>拨不出去：%1（%2）</source>
        <translation>Could not place the call: %1 (%2)</translation>
    </message>
    <message>
        <source>%1 忙线中</source>
        <translation>%1 is busy</translation>
    </message>
    <message>
        <source>%1 无人接听</source>
        <translation>%1 did not answer</translation>
    </message>
    <message>
        <source>%1 已拒绝</source>
        <translation>%1 declined</translation>
    </message>
    <message>
        <source>%1 取消了通话</source>
        <translation>%1 cancelled the call</translation>
    </message>
    <message>
        <source>通话中，已自动回复 %1 忙线</source>
        <translation>Already in a call — %1 was told you are busy</translation>
    </message>
    <message>
        <source>已在其他设备接听</source>
        <translation>Answered on another device</translation>
    </message>
    <message>
        <source>已在其他设备拒绝</source>
        <translation>Declined on another device</translation>
    </message>
    <message>
        <source>会议房已创建：%1</source>
        <translation>Meeting room created: %1</translation>
    </message>
    <message>
        <source>先填房间号，或点「新建会议房」。</source>
        <translation>Enter a room number, or create a new meeting room.</translation>
    </message>
    <message>
        <source>进房失败：%1（%2）</source>
        <translation>Could not join the room: %1 (%2)</translation>
    </message>
    <message>
        <source>im-rtc Demo</source>
        <translation>im-rtc Demo</translation>
    </message>
    <message>
        <source>im-rtc Demo —— 桌面端参考实现（经 C ABI）</source>
        <translation>im-rtc Demo — desktop reference client (through the C ABI)</translation>
    </message>
</context>
<context>
    <name>SettingsPage</name>
    <message>
        <source>（未连接）</source>
        <translation>(not connected)</translation>
    </message>
    <message>
        <source>界面</source>
        <translation>Interface</translation>
    </message>
    <message>
        <source>语言</source>
        <translation>Language</translation>
    </message>
    <message>
        <source>本次会话</source>
        <translation>This session</translation>
    </message>
    <message>
        <source>用户 ID</source>
        <translation>User ID</translation>
    </message>
    <message>
        <source>信令端点</source>
        <translation>Signalling endpoint</translation>
    </message>
    <message>
        <source>设备 ID</source>
        <translation>Device ID</translation>
    </message>
    <message>
        <source>会话 ID</source>
        <translation>Session ID</translation>
    </message>
    <message>
        <source>这个构建能做什么</source>
        <translation>What this build can do</translation>
    </message>
    <message>
        <source>SDK %1，经 C ABI 调用（与集成方拿到的是同一个 .dylib / .dll + 一个 C 头）。

✅ 已经是真的：登录、心跳、断线重连、拨号、来电、接听/拒接/取消/挂断、群通话成员事件、加入与离开房间、通话记录。

⬜ 还没有：声音与画面。媒体面（MediaAdapter / MediaPlane）已经接好并测全，但真正干活的 WebRTCAdapter 还没写——libwebrtc 没有 macOS x86_64 的预编译包，已决定等 Apple Silicon 或 Windows 机器。

⬜ 也还没有：设备枚举与热插拔、共享屏幕、Windows 侧的任何验证。</source>
        <translation>SDK %1, called through the C ABI — the same .dylib / .dll and C header an integrator receives.

✅ Real already: login, heartbeat, reconnect, dialling, incoming calls, accept/reject/cancel/hang up, group participant events, joining and leaving rooms, call history.

⬜ Not yet: audio and video. The media plane (MediaAdapter / MediaPlane) is wired and fully tested, but the WebRTCAdapter that does the actual work is unwritten — libwebrtc ships no macOS x86_64 build, so this waits for an Apple Silicon or Windows machine.

⬜ Also not yet: device enumeration and hot-plug, screen sharing, any verification on Windows.</translation>
    </message>
</context>
<context>
    <name>VideoTile</name>
    <message>
        <source>我</source>
        <translation>You</translation>
    </message>
    <message>
        <source>呼叫中…</source>
        <translation>Calling…</translation>
    </message>
    <message>
        <source>已离开</source>
        <translation>Left</translation>
    </message>
</context>
<context>
    <name>callstrings</name>
    <message>
        <source>已取消</source>
        <translation>Cancelled</translation>
    </message>
    <message>
        <source>对方已拒绝</source>
        <translation>They declined</translation>
    </message>
    <message>
        <source>无人接听</source>
        <translation>No answer</translation>
    </message>
    <message>
        <source>对方忙线中</source>
        <translation>They are busy</translation>
    </message>
    <message>
        <source>对方不在线</source>
        <translation>They are offline</translation>
    </message>
    <message>
        <source>已在其他设备接听</source>
        <translation>Answered on another device</translation>
    </message>
    <message>
        <source>已在其他设备拒绝</source>
        <translation>Declined on another device</translation>
    </message>
    <message>
        <source>你已被移出通话</source>
        <translation>You were removed from the call</translation>
    </message>
    <message>
        <source>通话已结束</source>
        <translation>Call ended</translation>
    </message>
    <message>
        <source>连接已断开</source>
        <translation>Disconnected</translation>
    </message>
    <message>
        <source>%1 人</source>
        <translation>%1 people</translation>
    </message>
    <message>
        <source>群通话 · %1 · %2</source>
        <translation>Group call · %1 · %2</translation>
    </message>
    <message>
        <source>群通话 · %1</source>
        <translation>Group call · %1</translation>
    </message>
    <message>
        <source>呼出</source>
        <translation>Outgoing</translation>
    </message>
    <message>
        <source>来电</source>
        <translation>Incoming</translation>
    </message>
    <message>
        <source>已拒绝</source>
        <translation>Declined</translation>
    </message>
    <message>
        <source>视频</source>
        <translation>video</translation>
    </message>
    <message>
        <source>语音</source>
        <translation>voice</translation>
    </message>
    <message>
        <source>未接来电 · %1</source>
        <translation>Missed %1 call</translation>
    </message>
    <message>
        <source>呼出 · 已取消</source>
        <translation>Outgoing · cancelled</translation>
    </message>
    <message>
        <source>呼出 · 对方已拒绝</source>
        <translation>Outgoing · declined</translation>
    </message>
    <message>
        <source>呼出 · 无人接听</source>
        <translation>Outgoing · no answer</translation>
    </message>
    <message>
        <source>呼出 · 对方忙线</source>
        <translation>Outgoing · busy</translation>
    </message>
    <message>
        <source>呼出 · 对方不在线</source>
        <translation>Outgoing · offline</translation>
    </message>
    <message>
        <source>呼出 · 未接通</source>
        <translation>Outgoing · not connected</translation>
    </message>
    <message>
        <source>昨天</source>
        <translation>Yesterday</translation>
    </message>
    <message>
        <source>M月d日</source>
        <translation>MMM d</translation>
    </message>
    <message>
        <source>邀请你加入群通话</source>
        <translation>is inviting you to a group call</translation>
    </message>
    <message>
        <source>邀请你视频通话</source>
        <translation>is inviting you to a video call</translation>
    </message>
    <message>
        <source>邀请你语音通话</source>
        <translation>is inviting you to a voice call</translation>
    </message>
</context>
<context>
    <name>language</name>
    <message>
        <source>跟随系统</source>
        <translation>Follow system</translation>
    </message>
</context>
</TS>
