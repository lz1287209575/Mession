# Server Registry

`Scripts/server_registry.py` 是多机控制场景下的中心注册服务。

它负责接收各 Agent 的心跳，并维护一份中心节点表。

## 启动

在控制机上执行：

```bash
python3 Scripts/server_registry.py --host 0.0.0.0 --port 19080 --auth-token <token>
```

可选参数：

```bash
python3 Scripts/server_registry.py --host 127.0.0.1 --port 19080 --stale-seconds 15
```

## Agent 侧接入

每台业务机的 Agent 可这样接入：

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

也支持环境变量：

```bash
MESSION_REGISTRY_TOKEN=<registry_token> python3 Scripts/server_control_api.py --registry-url http://registry-host:19080
```

Registry 自身也支持环境变量：

- `MESSION_REGISTRY_HOST`
- `MESSION_REGISTRY_PORT`
- `MESSION_REGISTRY_TOKEN`
- `MESSION_REGISTRY_STALE_SECONDS`
- `MESSION_REGISTRY_STATE_FILE`

## 当前能力

- 接收 Agent 心跳
- 记录 Agent 身份信息
- 记录首次接入时间
- 记录最近一次心跳时间
- 判断节点 `online / stale`
- 接收 Agent 自报的控制地址与分组信息
- 持久化节点表到本地文件
- 给控制台提供自动发现基础数据

## 与 TUI / 集群配置配合

如果集群配置里包含：

```json
{
  "registry": {
    "url": "http://registry-host:19080",
    "auth_token": "...",
    "auto_discover": true,
    "default_agent_auth_token": "..."
  }
}
```

那么 `server_manager_tui.py` 会在保留直连 Agent 控制能力的同时，优先使用中央注册表来显示：

- 节点心跳状态
- Agent 身份信息
- Agent 自报分组
- Agent 已应用拓扑版本

如果 Agent 还上报了 `control_api.base_url`，并且控制端知道相应 token，那么这些节点可以被 TUI 自动发现并直接纳入批量操作面板。

## 路由

### `GET /healthz`

无鉴权 liveness 探针。

### `GET /readyz`

无鉴权 readiness 探针。

当前会检查状态文件目录是否可写。

### `GET /api/registry/nodes`

返回当前全部节点表与汇总状态。

### `GET /api/registry/nodes/<agent_name>`

返回单个节点详情。

### `POST /api/registry/heartbeat`

供 Agent 周期上报：

- `agent_name`
- `agent_id`
- `agent_started_at`
- `project_root`
- `build_dir`
- `heartbeat_at`
- `groups`
- `control_api`
- `topology`
- `status`

## 心跳状态

注册中心按 `received_at` 与 `--stale-seconds` 判断：

- `online`
  最近心跳未超时
- `stale`
  最近心跳已超时，但中心仍保留记录

## 状态文件

默认写入：

- `Build/.mession_registry_state.json`

它当前是注册中心自己的持久化缓存，而不是完整 CMDB。

## 认证

如果配置了 token，请求需要带任一请求头：

- `Authorization: Bearer <token>`
- `X-Mession-Token: <token>`

## 容器化

仓库已提供：

- [`Docker/registry.Dockerfile`](/root/Mession/Docker/registry.Dockerfile)

示例构建：

```bash
docker build -f Docker/registry.Dockerfile -t mession-registry .
```

示例运行：

```bash
docker run --rm -p 19080:19080 \
  -e MESSION_REGISTRY_HOST=0.0.0.0 \
  -e MESSION_REGISTRY_PORT=19080 \
  -e MESSION_REGISTRY_TOKEN=registry-token \
  -e MESSION_REGISTRY_STATE_FILE=/data/.mession_registry_state.json \
  -v "$(pwd)/Build:/data" \
  mession-registry
```

## 当前限制

- 当前只负责节点清单和心跳，不代理实际控制指令
- 状态模型偏轻量，更适合作为控制台数据源，而不是完整资产系统
- 自动发现能否直接控制节点，仍依赖 Agent 上报的可达地址和控制端掌握的 token
