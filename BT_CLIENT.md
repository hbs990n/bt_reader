# 蓝牙朗读客户端开发说明

手机 App（离线朗读）作为 **SPP 服务端**，你的客户端负责连接手机并发送文本，手机离线 TTS 朗读。

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
在本地写代码，不用git,我手动上传到github仓库，用action编译
### Linux（BlueZ）
- 头文件 `<bluetooth/bluetooth.h>`、`<bluetooth/rfcomm.h>`，链接 `-lbluetooth`（装 `libbluetooth-dev`）
- `socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM)`
- `sockaddr_rc` 填手机 MAC + channel=1，`connect` 后 `write`

### Windows（Winsock）
- `<winsock2.h>` `<ws2bth.h>`，链接 `ws2_32`、`Bthprops.lib`
- `socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM)`
- `SOCKADDR_BTH` 填手机 MAC，端口用 0（SDP 服务发现 SPP UUID 自动解析）；建议先用 Windows 系统蓝牙配对手机

### 命令行接口（建议）
```
bt_reader <手机MAC> <文本>     # 连接、发送一行、断开
bt_reader <手机MAC>            # 交互模式，stdin 逐行发送
```

## 测试
手机 App 点「蓝牙朗读」→按钮显示「配对中」→客户端连上→手机显示「已配对」→发文本应朗读。

## 注意
- 手机 MAC：设置 → 我的设备 → 全部参数 → 蓝牙地址，形如 `AA:BB:CC:DD:EE:FF`
- 一次发一段文本即可，行末加 `\n`
