# 统一自动识别与电量解析（v0.2.0）

用户直接启动 `bin/BatteryIndicator.exe`。程序自动枚举设备、匹配内置读取器、发出已知的电量查询，将结果转换成统一数据并显示在托盘。无需配置文件、上传驱动、安装 Python 或连接网络。

## 内置支持范围

| 读取器 | 范围 | 验证状态 |
| --- | --- | --- |
| GANSS | GS3104T-PRO，05AC:024F + 型号名 + FF60:0061 | 本机真实设备已验证 |
| HID++ | 罗技 HID++ 2.0 电量特征 1004 / 1000 | 本机 PRO X Wireless 已验证；其他型号未逐一验证 |
| Razer | 下表中的鼠标 USB ID，Feature 报文长度 91 | 已实现与测试解析，未在本机验证这些硬件 |
| HyperX | Cloud II Wireless，03F0:0696 / 018B，FF90:0303 | 已实现与测试解析，未在本机验证这些硬件 |
| Nova7 | 下表中的 SteelSeries Nova 7 系列，1038，FFC0:0001 | 已实现与测试解析，未在本机验证这些硬件 |
| StandardHID | HID 描述中声明的 Battery Strength Feature，0006:0020 | 保留标准字段读取；未在本机验证相应硬件 |

### 雷蛇鼠标 USB ID（VID 1532）

| 型号 | PID |
| --- | --- |
| Viper Ultimate | 007A、007B |
| DeathAdder V2 Pro | 007C、007D |
| Basilisk Ultimate | 0086、0088 |
| Orochi V2 | 0094 |
| Pro Click Mini | 009A |
| Viper V2 Pro | 00A5、00A6 |
| Basilisk V3 Pro | 00AA、00AB |
| DeathAdder V3 Pro | 00B6、00B7 |
| Viper V3 Pro | 00C0、00C1 |

这些 ID 可能包括有线和接收器模式。程序仅在列出的 ID 且 Feature 报文长度匹配时查询，不对其他雷蛇型号试发命令。

### SteelSeries Nova 7 系列 USB ID（VID 1038）

- 百分比布局：22A1、227E、2258、229E、22AD、22A5、22A9、22A7、2298。
- 0～4 等级布局：2202、2206、22A4、223A、227A、220A；显示为约 0/25/50/75/100%。

## 内部结构

`src/battery.c` 负责枚举、现有设备协议、线程、托盘、设置和绘图；新增的 `src/protocols.h` 是与主程序一起编译的内置协议模块，不是给用户填写的配置。

```text
scan()
  → DeviceInfo（VID、PID、名称、collection、报文长度、设备键）
  → dispatchBattery()
  → providers[] 自动匹配
  → read() 查询
  → Reading / Snapshot
  → 原有托盘显示
```

`providers[]` 为每个读取器注册名称、说明、匹配函数和读取函数。顺序是已知厂商协议优先、标准字段作为补充。底层协议各不相同，但上层统一使用设备名、百分比、充电状态、是否近似与协议名称。

简单的报文协议通过 `PacketRule` 描述请求、响应前缀、最小响应长度、电量位置、在线状态和等级范围。`queryPacket()` 统一处理输入输出与超时，`parsePacket()` 统一检查响应并换算读数。HyperX 和 Nova 7 复用这套引擎。

罗技需要动态查找特征和多步通信，继续使用原来的 HID++ 处理器；高斯继续使用原来的带加法校验和的处理器。雷蛇通过 Feature 报文查询，并校验事务 ID、命令、状态与 XOR 校验。统一接口允许这些协议共存，无需强行把不同报文格式当成同一种格式。

## 自动匹配的边界

- 不会对所有 HID 接口遍历发送各品牌命令。只有设备标识和接口匹配的读取器才会执行。
- 新增厂商设备通过 Windows Container ID 区分物理设备、合并复合 collection；无法取得 Container ID 时回退到接口路径。
- 高斯和罗技保留原有设备键策略；相同型号的多把高斯键盘仍会合并。
- 匹配到读取器但无法取得有效电量时，显示休眠、离线或暂时无法读取。
- 名称含 2.4G / GANSS 的未适配设备可显示 `?`，悬停提示协议暂未支持。程序无法从所有普通 USB 名称判断设备是否为无线接收器，其他未知设备可能不显示。
- 未知私有协议仍需要开发者适配，无法仅凭一串字节自动理解其含义。新增读取器会随程序版本提供，普通用户无需收集配置。

## 保留的使用行为

正常电量亮粉色 `#FF2D95`、低电量红色、已知充电绿色、未知灰色；三位数自动缩放、4 倍采样、七种字体和字体记忆；托盘菜单、120 秒刷新、手动刷新、登录自启、单实例与命令行操作均保留。

新增 `--supported` 参数，将内置范围写到 `bin/supported.txt`。`--status` 和 `--probe` 显示使用的协议名称，方便区分“尚未适配”和“已适配但本次没有读数”。

## 协议参考

本次读取的主要参考（2026-11-04）：

- [HaloBattery Razer provider](https://github.com/HeyOkay/HaloBattery/blob/main/providers/razer.py)：已知鼠标 ID、事务 ID、90 字节载荷、Feature 查询和电量换算。
- [HaloBattery HyperX provider](https://github.com/HeyOkay/HaloBattery/blob/main/providers/hyperx.py)：Cloud II Wireless 型号与 06 FF BB 查询、响应字段。
- [HaloBattery SteelSeries provider](https://github.com/HeyOkay/HaloBattery/blob/main/providers/steelseries.py)：Nova 7 型号列表、B0 查询、在线和电量布局。

第三方 MIT 说明保留在 `THIRD_PARTY_NOTICES.md`。新增硬件没有出现在本机，因此“内置支持”表示已有匹配、查询和解析实现，不能代替对应实机测试。

## 验证

`test.cmd` 保留原高斯报文、去重和 168 种数字显示测试，增加共用报文解析、错误响应头、短报文、超范围、离线旧值、离散等级、雷蛇事务和 CRC、精确设备匹配，以及未知接收器不执行厂商查询的测试。

硬件回归已取得：罗技 39%，高斯按 Shift 唤醒后 100%。两台可能分别休眠，实时 `--status` 不保证一直同时有有效百分比。当前字体仍为用户保存的 Times New Roman（序号 6），自启路径保持原值。
