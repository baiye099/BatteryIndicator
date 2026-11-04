# BatteryIndicator

> **仅限非商业用途，禁止商用。** 本项目原创部分采用 [PolyForm Noncommercial 1.0.0](LICENSE)，详见[中文使用许可说明](LICENSE.zh-CN.md)；第三方材料保留原许可。

Windows 10/11 x64 的轻量外设电量托盘程序，使用 C 和 Windows 自带 API。

当前版本：**v0.2.0**。版本号见 [VERSION](VERSION)，版本变更见 [CHANGELOG.md](CHANGELOG.md)，本版功能、验证范围和更新方式见 [v0.2.0 版本说明](RELEASE_NOTES_v0.2.0.md)。

自动扫描设备并选择内置电量协议，用户无需下载或上传配置文件。实现与支持范围见 [统一自动识别说明](AUTO_DETECTION.md)。

完整开发过程、两种设备协议、低占用设计、图标和字体实现、构建测试及目录清理说明，见 [项目实现全流程](BatteryIndicator_项目实现全流程.md)。

## 使用

程序已编译到 `bin/BatteryIndicator.exe`。双击根目录的 `启动.cmd` 即可运行，无控制台窗口。

- 每台已识别的设备有一个数字托盘图标；悬停显示设备名、电量。
- 数字按实际宽度自动缩放，三位数 `100` 完整显示并留白；使用 4 倍采样平滑边缘。
- 托盘菜单“数字字体”提供 Bahnschrift（默认）、Segoe UI Semibold、DIN Next、MiSans、Consolas、Arial、Times New Roman。选择保存在 `HKCU\Software\BatteryIndicator\Font`。使用本机已安装字体，换到缺少该字体的电脑时由 Windows 替代。
- 点击或右键图标展开设备列表，可立即刷新、切换登录自启、退出。
- 默认每 120 秒查询一次；设备插拔和电脑唤醒时刷新。
- 低于或等于 20% 显示红色；充电状态已确认时显示绿色；普通读数显示亮粉色（#FF2D95）。
- 休眠、未返回有效数据或协议未适配时显示 `?`，不会把上次读数当作实时电量。
- 图标若在右下角 `^` 内，可以手动拖到托盘常显区域。

使用独立系统托盘图标，不修改 Windows 自带的笔记本电池弹出面板。

`开启自启.cmd` 设置当前用户登录 Windows 后启动，并立即启动程序；`关闭自启.cmd` 仅移除自启；`退出程序.cmd` 退出常驻进程。退出本次程序不会取消下次登录自启。

自启项：`HKCU\Software\Microsoft\Windows\CurrentVersion\Run\BatteryIndicator`，内容是带引号的完整 EXE 路径。无需管理员权限。移动 EXE 后请重新开启自启。

## 设备支持与实测

2026-11-04 在本机通过 2.4G 接收器读取：

| 设备 | 通道 | 实测读数 | 状态 |
| --- | --- | --- | --- |
| Logitech PRO X Wireless | 046D:C547，HID++ 2.0 | 40% | 已验证 |
| GANSS GS3104T-PRO 2.4G | 05AC:024F，FF60:0061 | 100% | 已验证 |

读数会随使用变化，以上不是固定值。高斯充电状态尚未验证，当前仅显示其百分比。

自动枚举设备，内置 GANSS、Logitech HID++、部分 Razer 鼠标、HyperX Cloud II Wireless、SteelSeries Nova 7 系列和标准 HID Battery Strength Feature 读取器。新增的雷蛇、HyperX、SteelSeries 及标准 HID 分支已做解析测试，但尚未在本机用对应硬件验证；已实测的仍是上表两台设备。**没有统一电量协议，无法保证所有 2.4G 型号都能读取**。未实现 HID++ 1.0、电压推算、通用蓝牙扫描。未适配设备可能不出现在托盘；名称含 2.4G 或 GANSS 的设备可显示未知电量图标。相同型号的多把高斯键盘目前合并显示。

高斯只在 VID/PID、型号字符串和 HID collection 都匹配时发送已定位的电量查询，不按共享的 Apple VID/PID 盲发命令。不改变按键、灯光、宏、配对或固件。设备可能因休眠或官方软件独占接收器而暂时无法读取。

## 体积与运行方式

单个原生 EXE，不依赖 Python、Electron、.NET，也无需厂商驱动常驻。空闲时线程等待 Windows 事件，没有动画和常驻轮询循环；仅查询响应期间执行代码。读数不变时不重绘图标。程序没有联网、更新或遥测功能。

最终体积与本机实测资源占用见 `VALIDATION.md`。内存会受 Windows、DPI、驱动和系统缓存影响，不能承诺所有电脑相同。

## 编译与验证

需要 Visual Studio C++ 工具和 Windows SDK，运行：

```cmd
build.cmd
test.cmd
```

使用 `/O1 /MT` 编译，静态链接 C 运行库。`test.cmd` 检查高斯真实报文样本、长度、校验和、命令、范围、离线零值和去重。

数字显示测试覆盖 7 款字体 × 4 种图标尺寸（16/20/24/32）× 6 个值，验证非空字形及四周透明留白。`preview-fonts.cmd` 可生成同一绘制函数的字体对比图 `bin/font-preview.bmp`。

调试设备：

```cmd
bin\BatteryIndicator.exe --probe
```

退出后查看 `bin/diagnostics.txt`。报告只列 HID 名称、VID/PID、collection、报文长度和已适配协议的电量响应，不记录键盘按键。平时运行不写日志。

运行中的程序还支持 `--refresh` 请求立即刷新，以及 `--status` 把当前托盘读数和图标注册结果写入 `bin/status.txt`；状态文件里的 `-1%` 表示当前没有有效读数。

`bin\BatteryIndicator.exe --supported` 将内置协议范围输出到 `bin/supported.txt`。没有协议导入、外部插件或联网下载步骤。

## 参考来源

- [HaloBattery](https://github.com/HeyOkay/HaloBattery)：多设备托盘及协议组织方式；[Logitech provider](https://github.com/HeyOkay/HaloBattery/blob/main/providers/logitech.py) 用于核实 HID++ 特征、查询与状态含义。MIT 声明保留在 `THIRD_PARTY_NOTICES.md`。
- [mousebatt](https://github.com/ryanlewis/mousebatt)：参考其精简原生托盘、低频查询的设计思路，没有复制其源代码。
- [GANSS 官方产品页](https://www.ganss.cn/forum/post/334636/) 和[官方网页驱动](https://ganss.driveall.cn/)：确认型号和驱动入口。网页版本的查询在本机无有效响应，最终采用本机已安装的官方 `GS3104T-PRO Driver Beta 1.0.0.3` 电量函数确认的命令。程序不包含厂商可执行文件、图片或固件。

高斯协议依据：原生驱动电量函数 `0x4357A0`、传输函数 `0x44F490`（该版本 ImageBase `0x400000`）。请求为 33 字节 `00 20 01 ... 21`；最后一字节为前 32 字节之和的低 8 位。响应为 `00 20 01 00 <percent> ... <checksum>`。本机捕获的 100% 样本末字节为 `85`。接收时核对长度、报文头、命令、百分比范围和校验和。原厂将百分比零作为无效读数处理，此版本同样显示未知。

## 版本管理

主分支使用 `main`；首个可复现版本使用附注标签 `v0.1.0`。版本发布时更新 `VERSION` 和 `CHANGELOG.md`，执行构建、测试，再提交并创建对应的版本标签。

仓库包含 `bin/BatteryIndicator.exe` 便于直接运行；更新程序时应同时提交匹配的源码及程序，并更新 `VALIDATION.md` 中的大小、哈希和验证结果。IDE 配置、调研下载、中间文件、诊断输出和生成预览不纳入版本管理。
