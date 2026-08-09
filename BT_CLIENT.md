# 蓝牙朗读客户端开发说明

手机 App（离线朗读）作为 **SPP 服务端**，你的客户端负责连接手机并发送文本，手机离线 TTS 朗读。

## 模块结构（三个文件夹）

| 目录 | 模块 | 职责 |
|---|---|---|
| `ui/` | 界面 + 配置 | GUI（GTK3 / Win32）、配置读写（记住 MAC、剪切板开关） |
| `bt/` | 蓝牙通信 | 扫描（BlueZ D-Bus）、配对、RFCOMM SPP 连接与数据传输 |
| `clipboard/` | 剪切板 | 监听本机剪切板，文本变化自动交给 bt 模块发送朗读 |

各模块职责单一：`ui` 只负责界面和配置，不碰蓝牙协议；`bt` 只负责蓝牙，不知道界面存在；`clipboard` 只负责监听剪切板，把新文本回调给上层。入口 `main.c` 分发 CLI / GUI。

## 协议

| 项 | 值 |
|---|---|
| 传输 | 经典蓝牙 SPP (RFCOMM) |
| 服务名 | `OfflineReader` |
| UUID | `00001101-0000-1000-8000-00805F9B34FB`（标准 SPP） |
| 编码 | UTF-8，文本以 `\n` 换行分隔，每行一条朗读内容 |
| 行为 | 手机逐行读取，每收到一行非空文本立即朗读 |

## 连接流程

1. 用户先在手机 App 点「蓝牙朗读」，手机进入可被发现 + 监听状态
2. 客户端按**手机蓝牙 MAC 地址**发起 RFCOMM 连接
3. 连上后手机 App 按钮显示「已配对」
4. 发送 UTF-8 文本行即可触发朗读
5. 客户端断开后手机自动回到监听状态，可随时重连

## 实现要点
在本地写代码,不用git,我手动上传到github仓库,用action编译
### Linux（BlueZ）
- 头文件 `<bluetooth/bluetooth.h>`、`<bluetooth/rfcomm.h>`、`<bluetooth/sdp.h>`、`<bluetooth/sdp_lib.h>`，链接 `-lbluetooth`（装 `libbluetooth-dev`）
- `socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM)`、`sockaddr_rc` 填手机 MAC + channel、`connect` 后 `write`
- **通道自动解析**：Android 分配 SPP 的 RFCOMM 通道不固定（本机实测为 4），不可硬编码。`bt/bt_link.c` 已用 `sdp_service_search_attr_req` 按 SPP UUID (`SERIAL_PORT_SVCLASS_ID 0x1101`) 查询真实通道，SDP 失败才回退默认通道 1
- 连接使用非阻塞 + poll，8 秒超时，避免失败时长时间阻塞

### Windows（Winsock）
- `<winsock2.h>` `<ws2bth.h>`，链接 `ws2_32`、`Bthprops.lib`
- `socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM)`
- `SOCKADDR_BTH` 填手机 MAC，端口用 0（SDP 服务发现 SPP UUID 自动解析）；建议先用 Windows 系统蓝牙配对手机

### 命令行接口
```
bt_reader <手机MAC> <文本>     # 连接、发送一行、断开
bt_reader <手机MAC>            # 交互模式，stdin 逐行发送
bt_reader                      # 图形界面
```

### 图形界面（含剪切板朗读）
- 填 MAC（可扫描 / 配对 / 手动输入）→「连接并朗读」发送一段文本
- **「剪切板朗读」开关**：开启后监听本机剪切板，复制/剪切文本自动发送给手机朗读（自动去重，仅内容变化且非空才触发）
- MAC 与开关状态保存到配置文件（Linux `~/.config/bt_reader.conf`，Windows `%APPDATA%\bt_reader.conf`），下次启动自动恢复

## 测试
手机 App 点「蓝牙朗读」→按钮显示「配对中」→客户端连上→手机显示「已配对」→发文本应朗读。
剪切板朗读：勾选「剪切板朗读」→复制一段文字 →手机应朗读。

## 注意
- 手机 MAC：设置 → 我的设备 → 全部参数 → 蓝牙地址，形如 `AA:BB:CC:DD:EE:FF`
- 一次发一段文本即可，行末加 `\n`
