# P5 Async 实现状态 — 最终形态与决策记录（2026-08-13）

> 本文档记录 P5（"业务像 C# async 一样写"）从 spec 到实现的**最终形态**、
> 与原始 spec 的**决策偏离**、**验证范围**与**已知边界**。
> 原始 spec：`.superpowers/sdd/2026-07-30-async-p5/`（设计讨论）；本文为落地后状态。

## 1. 最终业务形态

```cpp
// 业务头（方案 B：纯声明，无 #ifdef）
MFUNCTION(Async)
SFutureResult<int> AwaitDemoCompute(int Seed);

// 业务 .cpp（#ifdef 体 = codegen 输入：正常业务逻辑 + await 表达式）
#ifdef MESSION_AWAIT_CODEGEN_SOURCE
MFUNCTION(Async)
SFutureResult<int> AwaitDemoCompute(int Seed)
{
    int Mid = Seed * 3;                                        // 业务逻辑 1
    int R = TAwaitable<AwaitDemoHelper>(Mid);                  // await 点（最简签名）
    return SFutureResult<int>(TResult<int, FAppError>::Ok(R + 1));  // 业务逻辑 2
}
#endif
```

- **await 表达式**：`TAwaitable<F>(args...)`——F 是目标函数名（`auto` 非类型模板参数），
  R 从函数指针返回类型（`SFutureResult<R>::InnerType`）推导——**不写 R/Args/decltype**。
- **控制流**：任意层 if/else/else-if 链/for 循环/嵌套/组合内 await——**通用递归生成器**。

## 2. 与原始 spec 的决策偏离

| 决策点 | 原始 spec | 最终实现 | 原因 |
|---|---|---|---|
| TAwaitable 签名 | `TAwaitable<F, R, Args...>(args)` | **`TAwaitable<F>(args)`**（auto F + FnTraits 推导 R） | 用户反馈 decltype+类型参数复杂；CTAD 对 auto+包不推导 |
| 业务体位置 | 头内 `#ifdef` 双视图 | **方案 B：头纯声明，体在 .cpp**（`#ifdef` 收 .cpp） | 业务头保持普通声明 |
| codegen 生成器 | 串行/循环/分支特化 | **通用递归控制流生成器**（AsyncBody → 控制流树 → 递归生成状态机） | 逐层特化不可持续（N 层嵌套） |
| Frame 生命周期 | 无明确 | **`TEnableSharedFromThis` + `SelfGuard`**（挂起自持有，Finish 释放） | 并发下悬垂指针（数据串/卡死） |
| `MESSION_AWAIT_CODEGEN_SOURCE` | 必要（双视图） | **仍必要**（C++ 无 await 关键字——codegen 输入标记），收在 .cpp | 与 spec 一致，位置优化 |

## 3. 验证范围（全部端到端 + 测试锁底）

- 单 await / 多 await 串行 / 循环累加 / 真异步 suspend-恢复（含耗时断言）/
  并发并行（50ms vs 100ms）/ 类成员 async
- if + early return / else await / if 内多 await / else-if 链 / 嵌套 if /
  3 层嵌套 / 分支内循环 / 循环内 if / fall-through（if 无 return 继续外层）
- 非 int 返回类型（MString）
- **回归入口**：`ctest --test-dir Build -R AwaitCodegen`（31 断言）
- 独立演示：`~/AwaitDemo`（桩运行时，独立构建运行）

## 4. 已知边界（未验证/未实现）

- **真实网络 await**：EchoAwait（ServerCall+Async）生成路径已验证，但
  Registry + Echo×2 + Gateway 拓扑的真实 RPC future 链路未跑
- 字符串字面量注释保护已实现；fall-through 已支持（后续边界较少）
- 特化生成器已删除（通用唯一路径——直线串行仍保留）

## 5. 结构

```
Source/Common/Runtime/Async/    SFutureResult/SAwaitable 类型层 + TAwaitable
Source/Tools/MHeaderTool/       codegen：AST 收集 + 通用递归控制流生成器
Source/Examples/AwaitDemo/      Demo（全形态）+ Tests/AwaitCodegenTest（回归）
~/AwaitDemo/                    独立可跑演示（桩运行时）
```
