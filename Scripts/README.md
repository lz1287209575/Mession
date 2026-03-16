# 脚本总览（`Scripts/`）

`Scripts/` 目录包含若干辅助脚本，用于一键起服、验证主链路、调试与 CI 集成。

## servers.py - 一键起服 / 停服

不跑测试，只启动或停止五个服务器进程，适合本地联调。

```bash
python3 Scripts/servers.py start [--build-dir Build]
python3 Scripts/servers.py stop  [--build-dir Build]
```

- **start**：按序启动 Router → Login → World → Scene → Gateway，等待各端口就绪后写入 PID 到 `Build/.mession_servers.pid`。可选 `--no-wait` 不等待端口就绪。
- **stop**：读取 PID 文件发送 SIGTERM；Linux 下会再对 8001–8005 执行 `fuser -k` 清理残留。

推荐场景：本地快速起一整套服务进行交互或脚本测试。

## validate.py - 主链路验证脚本

启动所有 Mession 服务器并验证登录、复制与清理路径是否正常，适合做冒烟测试与 CI 验证。

### 用法

```bash
# 完整流程（编译 + 验证）
python3 Scripts/validate.py

# 跳过编译，仅验证
python3 Scripts/validate.py --no-build

# 指定构建目录（CMake 构建目录，二进制输出在 Bin/）
python3 Scripts/validate.py --build-dir Build --timeout 45
```

### 常用参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--build-dir` | `Build` | CMake 构建目录（可中间文件），可执行文件统一输出到仓库根下 `Bin/` |
| `--no-build` | - | 跳过编译步骤 |
| `--timeout` | 30 | 服务器启动超时（秒） |
| `--zone` | - | 设置 `MESSION_ZONE_ID`，验证分区/分片路由 |
| `--stress` | 0 | 开启 N 个并发客户端压力测试 |

### 验证流程摘要

1. 按顺序启动 Router → Login → World → Scene → Gateway
2. **Handshake 本地处理**：通过 `MT_Handshake` 验证 Gateway 本地声明式入口和 debug 观测
3. **多玩家登录**：多个客户端依次连接并登录，校验 `SessionKey/PlayerId`
4. **复制链路**：登录后收包，断言至少收到 `MT_ActorCreate`
5. **RouterResolved 路由缓存**：通过客户端移动触发路由缓存建立，并观察 Gateway 路由日志
6. **Chat 路径**：通过 `MT_Chat` 验证 Gateway -> World -> Client 的声明式转发
7. **Heartbeat 本地处理**：通过 `MT_Heartbeat` 验证 Gateway 本地声明式入口和 debug 观测
8. **断线重连**：首个客户端断开后重连，并再次登录
9. **登录即断开**：登录后立刻断开，并验证可再次登录
10. **双端同时断线**：两个已登录玩家同时断开，并验证都可恢复
11. **快速重连边界**：同一 `PlayerId` 短时间内连续重连
12. **并发验证**：多线程同时连接登录，验证服务端稳定性；若首轮出现单次抖动，会自动复核失败客户端后再判失败
13. （可选）压力测试：大量并发连接 + 登录 + 多次收发包
14. 清理并退出

说明：

- 当前客户端上行入口中，`MT_RPC` 仍是唯一保留的 legacy 特例。
- `validate.py` 会额外断言 `legacyClientRpcCount` 增长且 `rejectedClientFallbackCount` 不增长。
- 其余未接入声明式入口的客户端消息默认不会再通过 Gateway 通用 fallback 自动转发。

## test_client.py - 简易测试客户端

交互式连接 Gateway，支持登录和移动命令，方便本地手动测试。

```bash
# 交互模式
python3 Scripts/test_client.py --host 127.0.0.1 --port 8001

# 非交互：登录 + 发送一次移动后退出
python3 Scripts/test_client.py --no-interactive
```

交互命令：

- `login`：执行一次登录
- `move x y z`：发送一次移动
- `quit`：退出客户端

## 其他脚本

- `verify_protocol.py`：协议相关的静态/动态验证工具
- `debug_replication.py`：复制链路调试脚本（可用于观察 `MT_ActorCreate/Update` 等包）

## CI 集成

在 CI（例如 Linux / GCC 构建）中，会在编译成功后自动运行 `validate.py`，保证主链路始终处于可用状态。  
