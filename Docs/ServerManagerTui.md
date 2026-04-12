# Server Manager TUI

`Scripts/server_manager_tui.py` 是面向本地终端的服务器统筹管理界面。

它直接复用当前仓库已有的脚本、Agent API 和注册表逻辑，不要求额外安装第三方 Python 包。

## 启动

在仓库根目录执行：

```bash
python3 Scripts/server_manager_tui.py
```

可选参数：

```bash
python3 Scripts/server_manager_tui.py --build-dir Build --refresh-interval 1.0
```

多机场景可传集群配置：

```bash
python3 Scripts/server_manager_tui.py --cluster-config Config/server_cluster.example.json
```

## 界面内容

当前提供 5 个视图：

- `Overview`
  服务列表、选中服务详情、节点注册表 / 心跳 / 拓扑问题
- `Logs`
  当前选中服务的实时日志尾部
- `Tasks`
  任务列表与最近输出
- `Fleet`
  注册表驱动的节点分组视图、搜索过滤、分组折叠与批量操作面板
- `Help`
  键位与视图说明

## 主要键位

- `Tab / h / l / 1-5`
  切换视图
- `, / .`
  切换节点
- `q`
  退出
- `r`
  立即刷新
- `Up / Down / j / k`
  切换当前选中服务
- `[` / `]`
  切换当前选中任务
- `PgUp / PgDn`
  在 `Logs` 或 `Tasks` 视图里滚动内容
- `Home / End`
  跳到更旧或最新内容
- `f`
  在 `Logs` 或 `Tasks` 视图里切换 follow 模式
- `a`
  启动全部服务
- `X`
  停止全部服务
- `s`
  启动当前选中服务
- `x`
  停止当前选中服务
- `R`
  重启当前选中服务
- `b`
  构建
- `v`
  执行 `validate --no-build`
- `V`
  执行 `validate`（带构建）
- `p`
  向当前节点下发拓扑
- `P`
  向全部节点下发拓扑
- `g`
  在 `Fleet` 视图里切换分组模式
- `/`
  在 `Fleet` 视图里进入搜索过滤输入
- `z`
  在 `Fleet` 视图里清空搜索、筛选和折叠状态
- `o`
  在 `Fleet` 视图里只看已标记节点
- `i`
  在 `Fleet` 视图里只看异常节点
- `{ / }`
  在 `Fleet` 视图里切换当前分组
- `c / C / e`
  在 `Fleet` 视图里折叠当前分组 / 折叠全部可见分组 / 展开全部分组
- `m`
  在 `Fleet` 视图里标记当前节点
- `M`
  在 `Fleet` 视图里标记当前分组全部节点
- `u`
  在 `Fleet` 视图里清空标记

## 适合当前仓库的原因

当前工程已经有：

- 固定服务端口
- PID 文件约定
- 起停服脚本
- 验证脚本
- Agent API
- 中央注册表
- 日志目录

所以用一个零依赖 TUI 把这些入口聚起来，可以很快形成可用的本地和多机控制面。

## 多机模式

TUI 当前支持三种节点：

- `local`
  直接在本机调用脚本和读取日志
- `agent`
  通过常驻 Agent 的 HTTP API 拉状态、看日志、触发动作
- `ssh`
  通过 SSH 到远端机器执行 `servers.py` / `validate.py`

推荐优先使用 `agent`，因为它更适合长期在线的多机控制面。

## 注册表与自动发现

当前控制台会维护一份节点注册表视图，记录：

- 首次见到时间
- 最近一次心跳时间
- 心跳状态
- Agent 名称 / Agent Id
- Agent 已应用的拓扑版本
- 当前节点拓扑问题

如果集群配置里提供了 `registry.url`，TUI 会优先从中央注册表读取：

- `online / stale / never_seen`
- 中央侧首次接入时间
- Agent 身份与拓扑版本
- Agent 自报分组信息

如果同时启用了 `registry.auto_discover`，TUI 会把注册表里出现但未写进静态配置的 Agent 自动加入节点列表。

这类自动发现节点在具备可直连控制信息时会作为可管理节点出现；否则会以只读节点出现，仍然可以用于健康度和拓扑巡检。

同时仍会直接访问各节点 Agent 来获取：

- `/api/status`
- `/api/tasks`
- `/api/logs/<server>`
- `/api/topology`

这样节点心跳由中心统一汇总，具体控制动作仍由节点自身 API 提供。

## 集群配置

示例配置见：

- [`Config/server_cluster.example.json`](/root/Mession/Config/server_cluster.example.json)

如果你准备完全走注册表自动发现，也可以把 `nodes` 留空，只保留 `registry` 配置。

## 远端节点前提

建议每台远端机器满足这些前提：

- 仓库路径存在且一致可配置
- `python3` 可用
- `cmake` 可用
- `servers.py` / `validate.py` 可在远端直接执行

如果使用 `agent`，建议在每台机器上常驻运行：

```bash
python3 Scripts/server_control_api.py \
  --host 0.0.0.0 \
  --port 18080 \
  --agent-name <node-name> \
  --auth-token <token> \
  --advertise-host <reachable-host> \
  --group world
```

如果 Agent 监听在 `0.0.0.0`，建议额外传 `--advertise-host` 或 `--advertise-url`，否则注册表无法为自动发现节点生成可直连地址。

如果使用 `ssh`，还需要：

- SSH key 已配置完成

当前 `ssh` 模式会使用：

- `BatchMode=yes`
- `ConnectTimeout=3`

所以如果远端 Agent 或 SSH 节点不可达，TUI 会较快把节点显示为 `Unknown`，而不会长时间卡死。

## 当前限制

- 日志视图是“实时 tail + 有限回看”，不是完整历史检索
- 任务输出是最近输出视图，不是完整任务归档
- 中央注册表当前只负责心跳与节点清单，不代理实际控制指令
- 自动发现节点能否直接控制，仍依赖 Agent 上报的可达地址和控制端掌握的 token
