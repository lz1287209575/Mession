# Server Control API

`Scripts/server_control_api.py` 是面向本地开发工具和多机控制的轻量 Agent 入口。

它的目标不是替代服务端运行时，而是给下面这些前端提供统一后端：

- UE Editor 插件
- 本地桌面管理工具
- Web 管理台
- 集群 TUI 控制台

## 启动

在仓库根目录执行：

```bash
python3 Scripts/server_control_api.py
```

可选参数：

```bash
python3 Scripts/server_control_api.py --host 127.0.0.1 --port 18080 --build-dir Build
```

默认监听：

- `127.0.0.1:18080`

如果要作为远端常驻 Agent 暴露给控制台，建议显式指定：

```bash
python3 Scripts/server_control_api.py --host 0.0.0.0 --port 18080 --agent-name node-a --auth-token <token>
```

也可以通过环境变量提供 token：

```bash
MESSION_CONTROL_API_TOKEN=<token> python3 Scripts/server_control_api.py --host 0.0.0.0
```

常用环境变量：

- `MESSION_CONTROL_API_HOST`
- `MESSION_CONTROL_API_PORT`
- `MESSION_BUILD_DIR`
- `MESSION_AGENT_NAME`
- `MESSION_AGENT_GROUPS`
- `MESSION_CONTROL_API_TOKEN`
- `MESSION_CONTROL_API_ADVERTISE_HOST`
- `MESSION_CONTROL_API_ADVERTISE_PORT`
- `MESSION_CONTROL_API_ADVERTISE_URL`
- `MESSION_REGISTRY_URL`
- `MESSION_REGISTRY_TOKEN`
- `MESSION_REGISTRY_HEARTBEAT_INTERVAL`

如果要主动向中心注册服务上报心跳，可追加：

```bash
python3 Scripts/server_control_api.py \
  --host 0.0.0.0 \
  --port 18080 \
  --agent-name node-a \
  --auth-token <agent_token> \
  --advertise-host 10.0.0.11 \
  --group world \
  --registry-url http://registry-host:19080 \
  --registry-token <registry_token>
```

如果 Agent 对外监听的是 `0.0.0.0`，建议补 `--advertise-host` 或 `--advertise-url`，否则注册表和自动发现控制台无法得到可直连地址。

## 当前能力

- 查询全部服务状态
- 查询后台任务状态
- 触发 `build`
- 触发 `start`
- 触发 `stop`
- 触发单服 `start / stop / restart`
- 触发 `validate`
- 触发 `validate_with_build`
- 读取单个服务日志尾部
- 可作为多机常驻 Agent 被控制台轮询
- 提供 Agent 身份信息
- 提供可选的分组和控制地址自报信息
- 接收拓扑下发并持久化当前拓扑版本
- 可主动向中心注册服务上报心跳

## 路由

### `GET /healthz`

无鉴权 liveness 探针。

### `GET /readyz`

无鉴权 readiness 探针。

当前会检查：

- `build_dir` 是否存在
- 当前 registry 心跳线程状态摘要

### `GET /api/agent/info`

返回 Agent 自身信息，包括：

- `agent_name`
- `agent_id`
- `started_at`
- `build_dir`
- `project_root`
- `auth_enabled`
- 当前已应用拓扑摘要
- 当前中心注册心跳状态

### `GET /api/status`

返回当前服务状态快照，来源于：

- `Build/.mession_servers.pid`
- 端口探测
- `Logs/servers/*.log`

返回内容包含：

- 仓库路径
- Build 路径
- PID 文件位置
- 日志目录
- 各服务状态
- Agent 名称与启动时间
- 已应用拓扑摘要

### `GET /api/topology`

返回当前 Agent 已应用的拓扑摘要。

### `GET /api/tasks`

返回当前 API 进程内维护的后台任务列表。

### `GET /api/tasks/<task_id>`

返回单个任务详情，包括：

- 状态
- 返回码
- 执行命令
- 最近输出

### `GET /api/logs/<server>?lines=200`

读取指定服务日志尾部。

当前支持：

- `RouterServer`
- `LoginServer`
- `WorldServer`
- `SceneServer`
- `GatewayServer`
- `MgoServer`

### `POST /api/actions/build`

后台执行：

```bash
cmake -S <project_root> -B <build_dir> -DCMAKE_BUILD_TYPE=Release
cmake --build <build_dir> -j4
```

### `POST /api/actions/start`

后台执行：

```bash
python3 Scripts/servers.py start --build-dir <build_dir>
```

### `POST /api/actions/stop`

后台执行：

```bash
python3 Scripts/servers.py stop --build-dir <build_dir>
```

### `POST /api/actions/validate`

后台执行：

```bash
python3 Scripts/validate.py --build-dir <build_dir> --no-build
```

### `POST /api/actions/validate_with_build`

后台执行：

```bash
python3 Scripts/validate.py --build-dir <build_dir>
```

### `POST /api/actions/start_server/<server>`

后台执行单服启动。

当前支持：

- `RouterServer`
- `LoginServer`
- `WorldServer`
- `SceneServer`
- `GatewayServer`
- `MgoServer`

### `POST /api/actions/stop_server/<server>`

后台执行单服停止。

### `POST /api/actions/restart_server/<server>`

后台执行单服重启。

### `POST /api/topology/apply`

接收控制面下发的节点拓扑并写入 Agent 本地状态文件。

当前会写入：

- `Build/.mession_agent_topology.json`

## 适合作为管理端 MVP 的原因

当前仓库里已经有：

- 进程控制脚本
- 验证脚本
- 日志目录约定
- 固定端口约定

所以先把这些能力通过一个本地 JSON API 暴露出来，比直接上完整 UI 更稳。

在多机场景里，这个脚本也可以直接作为每台机器上的常驻 Agent。

## 容器化

仓库已提供：

- [`Docker/control-api.Dockerfile`](/root/Mession/Docker/control-api.Dockerfile)

示例构建：

```bash
docker build -f Docker/control-api.Dockerfile -t mession-control-api .
```

示例运行：

```bash
docker run --rm -p 18080:18080 \
  -e MESSION_CONTROL_API_HOST=0.0.0.0 \
  -e MESSION_CONTROL_API_PORT=18080 \
  -e MESSION_AGENT_NAME=node-a \
  -e MESSION_CONTROL_API_TOKEN=agent-token \
  -e MESSION_CONTROL_API_ADVERTISE_HOST=node-a.default.svc.cluster.local \
  -e MESSION_REGISTRY_URL=http://registry:19080 \
  -e MESSION_REGISTRY_TOKEN=registry-token \
  mession-control-api
```

## 认证

如果未配置 token，API 默认为无认证。

如果配置了 token，请求需要带任一请求头：

- `Authorization: Bearer <token>`
- `X-Mession-Token: <token>`

后续如果要继续做“服务器统筹管理 App”，推荐下一步优先补：

1. 任务输出实时流式推送
2. 统一健康信息接口
3. 各服务接入结构化 debug status JSON
4. 更细粒度的多机控制
