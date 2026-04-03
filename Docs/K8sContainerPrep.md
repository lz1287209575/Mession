# K8s / Container Prep

这份文档说明当前控制面脚本如何以更适合容器和 K8s 的方式运行。

当前目标不是直接提供完整 Helm Chart，而是先把以下三件基础设施铺平：

- 环境变量化配置
- 健康检查接口
- 最小可用容器镜像

仓库当前也提供了一套最小可用 manifest：

- [`K8s/kustomization.yaml`](/root/Mession/K8s/kustomization.yaml)

可直接执行：

```bash
kubectl apply -k K8s/
```

## 已提供的容器入口

- [`Docker/control-api.Dockerfile`](/root/Mession/Docker/control-api.Dockerfile)
- [`Docker/registry.Dockerfile`](/root/Mession/Docker/registry.Dockerfile)

## 已提供的 K8s 资源

- [`namespace.yaml`](/root/Mession/K8s/namespace.yaml)
- [`registry-configmap.yaml`](/root/Mession/K8s/registry-configmap.yaml)
- [`registry-secret.yaml`](/root/Mession/K8s/registry-secret.yaml)
- [`registry-pvc.yaml`](/root/Mession/K8s/registry-pvc.yaml)
- [`registry-deployment.yaml`](/root/Mession/K8s/registry-deployment.yaml)
- [`control-api-configmap.yaml`](/root/Mession/K8s/control-api-configmap.yaml)
- [`control-api-secret.yaml`](/root/Mession/K8s/control-api-secret.yaml)
- [`control-api-deployment.yaml`](/root/Mession/K8s/control-api-deployment.yaml)

其中 `control-api` 当前是“单个逻辑节点”的样板 Deployment。
如果你要接多组 Agent，最直接的做法是复制这份 Deployment/Service，并修改：

- `metadata.name`
- `MESSION_AGENT_NAME`
- `MESSION_AGENT_GROUPS`
- `MESSION_CONTROL_API_ADVERTISE_HOST`
- 对应的 `Secret`

这两个镜像都直接以 Python 脚本作为入口：

- `python3 Scripts/server_control_api.py`
- `python3 Scripts/server_registry.py`

## Control API 建议环境变量

- `MESSION_CONTROL_API_HOST=0.0.0.0`
- `MESSION_CONTROL_API_PORT=18080`
- `MESSION_BUILD_DIR=/app/Build`
- `MESSION_AGENT_NAME=node-a`
- `MESSION_AGENT_GROUPS=world,shard-a`
- `MESSION_CONTROL_API_TOKEN=agent-token`
- `MESSION_CONTROL_API_ADVERTISE_HOST=node-a.default.svc.cluster.local`
- `MESSION_CONTROL_API_ADVERTISE_PORT=18080`
- `MESSION_CONTROL_API_ADVERTISE_URL=http://node-a.default.svc.cluster.local:18080`
- `MESSION_REGISTRY_URL=http://registry:19080`
- `MESSION_REGISTRY_TOKEN=registry-token`
- `MESSION_REGISTRY_HEARTBEAT_INTERVAL=5`

## Registry 建议环境变量

- `MESSION_REGISTRY_HOST=0.0.0.0`
- `MESSION_REGISTRY_PORT=19080`
- `MESSION_REGISTRY_TOKEN=registry-token`
- `MESSION_REGISTRY_STALE_SECONDS=15`
- `MESSION_REGISTRY_STATE_FILE=/data/.mession_registry_state.json`

## 健康检查

两个服务都支持：

- `GET /healthz`
- `GET /readyz`

并且默认无鉴权，方便直接挂到探针。

建议映射：

### Control API

- `livenessProbe`
  `GET /healthz`
- `readinessProbe`
  `GET /readyz`

当前 manifest 里把 `MESSION_BUILD_DIR` 指向了 `/data/build`，并挂了 `emptyDir`。
这更适合先验证控制面容器是否能启动。
后续如果你要让它真的管理构建产物或运行产物，建议把这里替换为：

- PVC
- `hostPath`
- 或者你自己的镜像内固定目录

### Registry

- `livenessProbe`
  `GET /healthz`
- `readinessProbe`
  `GET /readyz`

## 当前建议的 K8s 映射思路

- `server_registry.py`
  更像一个中心控制面组件，适合单独 Deployment
- `server_control_api.py`
  更像每个逻辑节点或每个游戏服宿主上的 sidecar / admin container
- 当前 TUI 里的“节点”
  后续可以映射成 Pod、Deployment 或一组逻辑 workload，而不一定是物理机

## 后续接 K8s 时最可能继续做的事情

- 用 K8s Service 名称替代当前的宿主机地址
- 让 Agent 的动作执行器从本地脚本逐步切换为 K8s API
- 给 Registry 或新的控制面加 workload 标签和 namespace 维度
- 视情况把 Registry 状态从本地文件切到外部存储
