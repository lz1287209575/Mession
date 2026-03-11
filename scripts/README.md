# 脚本

## validate.py - 脚本验证

启动所有 Mession 服务器并验证登录流程是否正常。

### 用法

```bash
# 完整流程（编译 + 验证）
python3 scripts/validate.py

# 跳过编译，仅验证
python3 scripts/validate.py --no-build

# 指定构建目录
python3 scripts/validate.py --build-dir build --timeout 45
```

### 参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--build-dir` | `build` | 构建输出目录 |
| `--no-build` | - | 跳过编译步骤 |
| `--timeout` | 30 | 服务器启动超时（秒） |

### 验证流程

1. 按顺序启动 Router → Login → World → Scene → Gateway
2. **多玩家登录**：3 个客户端依次连接并登录
3. **断线重连**：首个客户端断开后重连并再次登录
4. **可选 Zone**：`--zone 1` 时设置 MESSION_ZONE_ID 验证区服路由
5. 清理并退出

### test_client.py - 简易测试客户端

交互式连接 Gateway，支持登录和移动命令。

```bash
# 交互模式
python3 scripts/test_client.py --host 127.0.0.1 --port 8001

# 非交互：登录 + 发送一次移动后退出
python3 scripts/test_client.py --no-interactive
```

命令：`login`、`move x y z`、`quit`

### CI

CI 在 Linux GCC 构建成功后会自动运行 validate.py。
