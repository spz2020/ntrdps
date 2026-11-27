# NTRDPS

一个用于 Windows NT 3.1、3.5、3.51 和 4.0 的远程桌面（RDP）服务器，适用于 TSE Hydra 之前和 WinFrame 之前的时代。RDP 客户端连接到 `WinSta0` 或本地控制台。不使用 WTS，不使用 MultiWin。

它也可运行在 Windows 2000 for Alpha 上：包括 32 位 RC2，以及带有独立安装程序 `ntrdpset-axp64.exe` 的 64 位 build 2210。

它也可运行在 PINT（Windows NT on Raspberry Pi）上：PINT 3.5 使用 `ntrdpset-pint35.exe`，PINT 4.0 使用 `ntrdpset-pint40.exe`。

是的，你可以通过远程桌面连接到 NT 3.1……

![Windows NT 3.1 on Alpha over RDP](screenshot.png)

## 安装

从 [Releases](https://github.com/tenox7/ntrdps/releases) 下载适用于你的 CPU 的安装程序，运行并点击 Install。

![ntrdps Setup](setup.png)

**要求身份验证：** RDP 客户端必须使用用户名和密码登录。

- NT 3.51 和 4.0：使用 Administrators 组中的 Windows 账户。
- NT 3.1 和 3.5：它们无法检查 Windows 密码，因此安装程序只要求为 ntrdps 设置用户名和密码。

如果关闭身份验证，任何能访问 TCP 端口 3389 的人都会获得控制台。

**性能：** NTRDPS 可以使用显示驱动或 GDI Polling 来检测屏幕上的变化。显示驱动（NT 4.0 上的 Mirror Driver，NT 3.x 上的 Layered Driver）速度较快。

从脚本运行，不显示窗口：

    ntrdpset /install /q
    ntrdpset /remove /q
    ntrdpset /status /q

`/install` 的选项：`/poll` 使用 GDI Polling，`/nologin` 不启用身份验证，`/user:NAME /password:PW` 用于设置 NT 3.1 / 3.5 的用户名和密码。

## 历史与先前技术

虽然 Windows NT 上的服务和后台进程一直可以假定角色 / 以特定用户身份运行，但 Windows 桌面是单用户的，并且仅限本地控制台。

Symantec pcANYWHERE、Dameware Mini Remote Control 或 VNC 等应用程序允许通过使用镜像驱动或 GDI polling 来远程控制本地桌面。

Citrix 授权获得 Windows 源代码以开发 MultiWin 功能。1995 年 9 月，他们发布了名为 WinFrame 的产品，引入了 WinStation 概念以及使用 ICA 协议的远程连接。

大约在 1998 年，Microsoft 发布了 Windows NT 4.0 Terminal Server Edition（TSE），又名 “Hydra”。它附带了一个名为 Remote Desktop Protocol（RDP）的新协议，与 ICA 不同，RDP 归 Microsoft 所有。Citrix 继续将 ICA 协议作为 TSE 的附加组件发布，称为 Metaframe。后来 TSE 更名为 WTS（Windows Terminal Services）。

有趣的是，RDP 协议是 Microsoft 从 [NetMeeting](https://en.wikipedia.org/wiki/NetMeeting) 和 ITU T.120/128 发展而来的。有 [a video](https://www.youtube.com/watch?v=qhay6VryyvE) 讲述了这段历史。

NTRDPS 基于来自 NetMeeting 的同一协议，但像 pcANYWHERE/DMRC/VNC 一样只连接到控制台。

## 常见问题

问：支持哪个 RDP 版本  
答：4.0 / 5.x

问：如何关闭压缩  
答：将 `HKLM\SOFTWARE\ntrdps\Compression`（REG_DWORD）设置为 0，然后执行 `net stop ntrdps` 和 `net start ntrdps`。这会节省 NT 机器上的 CPU，但会通过网络发送更多数据。

问：它是否处理多用户/多会话/multi-win  
答：不处理，仅限本地控制台

## 许可证

MIT
