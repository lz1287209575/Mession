# 工具链

## `MHeaderTool`

`MHeaderTool` 是当前反射系统的代码生成工具，源码位于：

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
- CMake manifest
- `Build/Generated/` 下的 glue 文件

### 使用方式

通常不需要手动执行。CMake 配置阶段会自动尝试运行它，并在首次冷启动时做 bootstrap。

## `NetBench`

`NetBench` 是一个轻量测试工具，当前默认针对 Gateway 侧能力做网络基准或连通性验证。

适合做：

- 报文格式快速实验
- 端口联通检查
- Gateway 压测雏形

## `Scripts/validate.py`

这是当前仓库最重要的自动验证脚本，负责跑最小完整链路。

覆盖内容：

- 编译
- 起服
- 统一 `MT_FunctionCall`
- 登录 / 查找玩家 / 切场 / 登出

## `Scripts/servers.py`

开发期便捷起停服脚本。

适用场景：

- 本地调试
- 观察单服日志
- 手动连客户端或压测工具

## `Scripts/verify_protocol.py`

用于协议一致性校验，适合在协议调整后快速检查消息定义和函数 ID 相关问题。

## `Scripts/test_client.py`

轻量测试客户端，可用于直连 Gateway 做协议实验。

## `Scripts/debug_replication.py`

用于复制链路调试，帮助观察对象更新下发过程。

## 工具链使用建议

- 改反射宏或协议结构后，优先确认 `Build/Generated/` 是否正常更新
- 改客户端入口或服间 RPC 后，优先跑 `validate.py`
- 改复制与状态同步后，配合 `debug_replication.py` 看链路
