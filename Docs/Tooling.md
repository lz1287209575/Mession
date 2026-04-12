# 工具链

## 总览

当前仓库的工具链可以分成四类：

- 代码生成：`MHeaderTool`
- 本地验证：`validate.py`、`test_client.py`、`debug_replication.py`
- 本地起停服：`servers.py`
- 控制面与多机工具：`server_control_api.py`、`server_registry.py`、`server_manager_tui.py`

## `MHeaderTool`

源码位于：

- [MHeaderTool.cpp](/root/Mession/Source/Tools/MHeaderTool.cpp)

它负责扫描 `Source/` 下的反射宏使用情况，提取：

- 类
- 结构体
- 枚举
- 属性
- 函数
- RPC 元数据

并生成：

- 反射注册代码
- `Build/Generated/` 下的 glue 文件
- `Build/Generated/MHeaderToolTargets.cmake`

通常不需要手动执行。CMake 配置和编译时会自动触发。

## `NetBench`

`NetBench` 是一个轻量测试工具，适合做：

- 报文格式快速实验
- 端口联通检查
- Gateway 侧网络实验

它不是正式压测系统，更适合开发期快速验证。

## `Scripts/validate.py`

这是当前仓库最重要的自动验证脚本。

它负责：

- 可选编译
- 启动完整服务链
- 通过 `MT_FunctionCall` 做客户端级回归
- 验证玩家查询、写操作、场景同步、最小战斗链路
- 验证错误链路和重登恢复

如果你只打算执行一个脚本，优先执行它。

## `Scripts/servers.py`

开发期便捷起停服脚本。

适用场景：

- 本地调试
- 长时间观察单服日志
- 手工连接客户端或测试脚本

它面向“手工开发观察”，不是回归验证工具。

## `Scripts/verify_protocol.py`

协议一致性检查脚本，适合在这些场景下运行：

- 改消息结构
- 改 `MFUNCTION` 元数据
- 改客户端稳定 API 名
- 改函数 ID 相关逻辑

## `Scripts/test_client.py`

轻量测试客户端，适合：

- 直连 Gateway 做协议实验
- 验证某个 `Client_*` 调用
- 调试 validate 没覆盖到的定向请求

## `Scripts/debug_replication.py`

复制链路调试脚本，适合：

- 观察对象更新下发
- 排查脏标记消费
- 对照 World 日志检查状态同步

## 控制面脚本

### `Scripts/server_control_api.py`

轻量 Agent，给本地或多机控制工具提供统一 API。

适合：

- 查询服务状态
- 触发 `build / start / stop / restart / validate`
- 拉取任务状态和日志
- 挂到多机控制台或 Web 管理端

对应文档：

- [ServerControlApi.md](/root/Mession/Docs/ServerControlApi.md)

### `Scripts/server_registry.py`

多机控制场景下的中心注册服务，负责：

- 接收 Agent 心跳
- 维护节点表
- 提供 `online / stale` 状态
- 为控制台提供自动发现基础

对应文档：

- [ServerRegistry.md](/root/Mession/Docs/ServerRegistry.md)

### `Scripts/server_manager_tui.py`

零依赖终端控制台，聚合：

- 服务列表
- 实时日志
- 任务输出
- 节点和分组视图
- 本地与多机控制动作

对应文档：

- [ServerManagerTui.md](/root/Mession/Docs/ServerManagerTui.md)

## 当前推荐使用顺序

1. 改完代码先编译
2. 先跑 `validate.py`
3. 需要长期盯服务时再用 `servers.py start`
4. 改协议时补跑 `verify_protocol.py`
5. 改复制、持久化、客户端链路时再用专项脚本定点排查

## 当前工具链的重点约束

- 改反射宏或协议结构后，优先确认 `Build/Generated/` 是否正常更新
- 不要把“手工起服能跑”当成“已经通过回归”
- 新增主链路能力后，应优先把它补进 `validate.py` 和对应文档
