# UE Client Downlink Function Call

这份文档记录 UE 下行正式路径的当前结论。

## 当前状态

这条链路已经完成：

- UE 侧统一下行接收端已完成
- UE 与当前服务端联调已完成
- 正式下行路径已确认为 `MT_FunctionCall -> FunctionID -> decode -> invoke`

## 当前约束

从现在开始：

- `MT_LoginResponse / MT_Actor*` 旧入口不再视为新的正式默认路径
- 下行新增能力应优先走统一函数调用
- UE 不应维护人工 `FunctionID` 映射表

## 这条路径适合承载什么

适合承载：

- 登录响应
- Actor create / update / destroy 包裹后的统一下行
- 一次性业务事件
- UI 提示和交互结果

## 不适合继续做什么

- 继续扩张 UE 侧 `MessageType -> switch`
- 为历史兼容再扩一套并列下行入口

## 回归口径

这条路径当前应通过：

- UE 联调验证
- `Scripts/validate.py`

来持续回归。
