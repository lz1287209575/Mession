# 主链路验证说明

本仓库通过 **脚本验证** 保证登录、复制与断线清理主链路可用，不依赖 ctest。

## 入口

- **本地**：在仓库根目录执行  
  `python3 scripts/validate.py [--build-dir build] [--timeout 50] [--no-build]`
- **CI**：`main` 分支 push/PR 时，在 Linux GCC 构建后自动执行上述脚本（见 `.github/workflows/cmake-multi-platform.yml`）。

## 前置条件

- 端口 **8001、8002、8003、8004、8005** 未被占用。若本地有残留进程，可先结束再跑，例如：
  - `fuser -k 8001/tcp 8002/tcp 8003/tcp 8004/tcp 8005/tcp`
- Python 3，无需额外依赖。

## 覆盖内容（登录、进世界、断线清理）

| 测试 | 场景 | 说明 |
|------|------|------|
| Test 1 | 登录 / 进世界 | 多玩家登录，校验 SessionKey/PlayerId 与登录响应 |
| Test 2 | 进世界 | 复制链路：登录后从客户端收包，断言至少收到一条 `MT_ActorCreate` |
| Test 3 | 断线清理 | 一端断线后重连同一 PlayerId，校验 Gateway/World 已回收状态 |

上述三项共同构成主链路集成测试，CI 在 Linux GCC 构建后自动执行。

## 可选参数

- `--build-dir`：构建目录，默认 `build`；CI 中传入绝对路径。
- `--timeout`：等待服务就绪超时（秒），默认 30。
- `--no-build`：跳过编译，仅运行验证。
- `--debug`：将各服 stdout/stderr 写入 `build/validate_logs/<Server>.log`，结束时根据 Gateway 日志提示复制是否到达。

## 与 ctest 的关系

当前 **不引入 ctest**：所有主链路验证由 `scripts/validate.py` 完成，CI 中直接调用该脚本。
