# TODO

这份文档记录当前这一轮 `Player / ObjectProxy / ClientCall` 重构后的后续收口项。

## 当前已稳定

- `Player` 已拆成 `Session / Controller / Pawn / Profile / Inventory / Progression`
- `WorldPlayerServiceEndpoint` 已收敛为按 `Player` 维度绑定并分发的服务入口
- `ObjectProxyCall / PlayerProxyCall` 已支持对象级本地 / 跨进程调用
- `WorldClientServiceEndpoint` 的非登录入口已统一 trait 化到通用 helper
- 客户端已经打通：
- `Client_FindPlayer`
- `Client_Logout`
- `Client_SwitchScene`
- `Client_ChangeGold`
- `Client_EquipItem`
- `Client_GrantExperience`
- `Client_ModifyHealth`
- `Scripts/validate.py --build-dir Build --no-build` 当前通过

## 下一步优先级

### 1. 补齐客户端查询入口

- 把 `Profile / Inventory / Progression` 的查询也补成客户端可直接调用的 `ClientCall`
- 让客户端对玩家状态的读写都走统一的 Player RPC 绑定层
- 尽量避免继续在 Gateway / World client endpoint 写胶水映射

### 2. 继续下沉 World 入口胶水

- 进一步减少 `WorldPlayerServiceEndpoint` 里的请求适配代码
- 能下沉到 `Player` 对象自身消费的，就不要再停留在 endpoint 里做手写编排
- 目标是“定义在 Player 系列对象上的 RPC，天然按 Player 实例消费”

### 3. 收敛 Player 运行时状态归属

- 继续明确 `Profile` 和 `Pawn` 的职责边界
- 当前 `Health` 仍有 runtime / persistence 的桥接逻辑，需要后续彻底收口
- 长期目标：
- `Profile` 负责持久化画像
- `Pawn` 负责场景驻留运行时状态
- 登录 / 登出 / 切图时不再需要兼容性同步

### 4. 把对象路径绑定变成更强的框架能力

- 继续强化 `PlayerProxyCall` 的“节点 -> 对象”绑定
- 减少显式字符串函数名和手写路径的暴露范围
- 后续可考虑让更多对象根类型复用同一套 `ObjectProxyCall` 绑定模式

### 5. 扩展写操作业务面

- 继续补 `Inventory / Progression` 之外的玩家写操作
- 优先补那些能验证完整生命周期的最小闭环能力
- 例如：
- 背包增删
- 装备切换
- 经验 / 等级规则扩展
- 场景内运行时属性修改

### 6. 开始整理 Actor 风格对象模型

- 按现在的方向继续往 `Actor -> Component` 形式演进
- 但保持游戏服务器语义，不必强行复刻 UE 命名
- 优先围绕玩家完整生命周期设计：
- 玩家实体
- 控制器
- 场景驻留体
- 持久化画像
- 可复制组件

## 近期建议执行顺序

1. 先补客户端查询入口
2. 再继续下沉 `WorldPlayerServiceEndpoint` 胶水
3. 再收 `Profile / Pawn` 的状态归属
4. 然后扩充更多玩家写操作
5. 最后正式整理 Actor 风格对象模型
