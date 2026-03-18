# Validation

Mession 当前默认通过脚本验证主链路，而不是依赖 ctest。

## 默认入口

在仓库根目录执行：

```bash
python3 Scripts/validate.py --timeout 60
```

如果只想跳过编译：

```bash
python3 Scripts/validate.py --timeout 60 --no-build
```

协议相关验证：

```bash
python3 Scripts/verify_protocol.py
```

## 当前验证覆盖

`validate.py` 当前覆盖的重点包括：

- Gateway 本地 Handshake
- 多玩家登录
- Actor create / update / destroy 下行
- `RouterResolved` 路由缓存
- 断线清理与重连
- Chat 路径
- Heartbeat 本地处理
- 统一函数调用负向场景
- 登录后立刻断开
- 双端同时断线
- 同一 `PlayerId` 快速重连
- 并发登录

## 当前协议口径

客户端正式入口与下行都应以统一函数调用为准：

- `Client -> Gateway`：`MT_FunctionCall`
- `World -> UE`：`MT_FunctionCall`

历史 `MT_LoginResponse / MT_Actor*` 这类路径不应再被当作新的正式设计目标。

## 什么时候应该更新验证

出现下面任一情况，都应该同步更新脚本：

- 客户端入口协议变化
- World 下行 snapshot 结构变化
- Gateway 路由策略变化
- 复制 / 清理 / 重连行为变化
- 负向错误码或失败分类变化

## 建议

开发节奏建议保持：

1. 先改代码
2. 再补文档
3. 最后跑 `validate.py`

对主链路改动，不建议只凭手工联调通过就结束。
