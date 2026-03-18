# Scripts

`Scripts/` 目录包含开发和回归时最常用的辅助脚本。

## 常用脚本

### `validate.py`

主链路回归入口。

```bash
python3 Scripts/validate.py --timeout 60
python3 Scripts/validate.py --timeout 60 --no-build
```

当前主要覆盖：

- Handshake
- 登录
- Actor create / update / destroy
- 路由缓存
- Chat
- Heartbeat
- 重连
- 并发登录
- 统一函数调用负向场景

### `servers.py`

本地起服 / 停服入口。

```bash
python3 Scripts/servers.py start
python3 Scripts/servers.py stop
```

### `verify_protocol.py`

协议相关验证脚本。

```bash
python3 Scripts/verify_protocol.py
```

## 当前建议

开发时的默认节奏建议是：

1. 改代码
2. 看 `Docs/`
3. 跑 `validate.py`

如果是协议改动，再补跑 `verify_protocol.py`。
