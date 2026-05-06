# Component Injection 系统设计

> 日期：2026-05-06
> 目标：简化基础设施，让业务代码不再难受

---

## 一、目标

简化玩家对象模型，移除树形的 Player/Session/Controller/Pawn 分层，改用扁平 Component 注入模式。

---

## 二、核心设计

### 2.1 Component 注入

```cpp
// MPlayerInfoComponent.h
MCLASS(InjectionClass=MPlayer)
class MPlayerInfoComponent : public IComponent  // 待定义
{
public:
    MFUNCTION(Async, Injection)
    SFutureResult<SGetPlayerInfoResponse> GetPlayerInfo(const SGetPlayerInfoRequest& Request);

private:
    IDbService* Db;      // 注入的服务
    ICacheService* Cache;
};
```

**`InjectionClass=MPlayer` 语义：**
- 被标记的 `MFUNCTION(Injection)` 和 `MPROPERTY(Injection)` 自动注入到目标类
- 调用方可以直接在 Player 上调用：`Player->GetPlayerInfo(Request)`
- 不需要手动获取组件引用

### 2.2 Request/Response 位置

```
Source/
├── Protocol/
│   └── Messages/
│       └── Player/
│           ├── PlayerInfoMessages.h   // SGetPlayerInfoRequest, SGetPlayerInfoResponse
│           └── PlayerModifyMessages.h
│
├── Servers/World/Player/
│   └── Components/
│       ├── MPlayerInfoComponent.h    // include Protocol
│       └── MPlayerCombatComponent.h
```

**原则：**
- Protocol 层只有纯数据 struct，无业务逻辑
- Component 只 include Protocol，避免循环引用

### 2.3 验证

```cpp
// Protocol/PlayerInfoMessages.h
MSTRUCT()
struct SGetPlayerInfoRequest
{
    GENERATED_BODY()

    MPROPERTY(ValidateMeta=NotZero)
    uint64 PlayerId;
}
```

**`ValidateMeta` 语义：**
- 验证在函数参数传入时自动执行
- 失败则返回错误，不进入业务逻辑
- 支持的验证类型（待扩展）：`NotZero`, `NotEmpty`, `Range(min,max)`, `Regex(pattern)`

### 2.4 服务调用

```cpp
class MPlayerInfoComponent
{
public:
    void SetDbService(IDbService* InDb) { Db = InDb; }
    void SetCacheService(ICacheService* InCache) { Cache = InCache; }

    SFutureResult<SGetPlayerInfoResponse> GetPlayerInfo(const SGetPlayerInfoRequest& Request)
    {
        auto Profile = AWAIT(Db->QueryProfile(Request.PlayerId));
        auto CacheData = AWAIT(Cache->Get(Request.PlayerId));
        // ...
        co_return Response;
    }

private:
    IDbService* Db = nullptr;
    ICacheService* Cache = nullptr;
};
```

**设计要点：**
- 服务通过注入设置（`SetXxxService`），不由 Component 创建
- 服务内部实现使用命令队列保证线程安全
- Component 内部通过 `AWAIT` 等待服务结果

---

## 三、组件接口

```cpp
// 待定义的基类接口
class IComponent
{
public:
    virtual ~IComponent() = default;
    virtual void OnAttach(MObject* Owner) {}
    virtual void OnDetach() {}
};
```

**职责：**
- `OnAttach`：组件附加到 Owner 时调用，可获取 Owner 引用
- `OnDetach`：组件从 Owner 分离时调用

---

## 四、MHeaderTool 代码生成

### 4.1 Injection 标记处理

MHeaderTool 扫描到 `MCLASS(InjectionClass=X)` 时：
1. 解析目标类 X
2. 收集所有 `MFUNCTION(Injection)` 和 `MPROPERTY(Injection)`
3. 在 X 的生成代码中注入这些成员

### 4.2 服务注入生成

MHeaderTool 为每个 `IService*` 成员生成：
```cpp
void SetDbService(IDbService* InDb) { Db = InDb; }
```

---

## 五、文件结构

```
Source/
├── Common/Runtime/
│   └── Component/
│       ├── IComponent.h          // 组件基类接口
│       └── ComponentInjection.h   // 注入机制
│
├── Protocol/
│   └── Messages/
│       └── Player/
│           ├── PlayerInfoMessages.h
│           └── PlayerModifyMessages.h
│
├── Servers/World/Player/
│   └── Components/
│       ├── MPlayerInfoComponent.h
│       ├── MPlayerInventoryComponent.h
│       └── MPlayerCombatComponent.h
│
├── Servers/World/
│   ├── MPlayer.h                 // 简化的 Player 类
│   └── MPlayer.inl               // 生成的注入代码
│
└── Tools/
    └── MHeaderTool.cpp           // 新增 InjectionClass 解析
```

---

## 六、迁移计划

### Phase 1: 基础设施
1. 定义 `IComponent` 接口
2. 实现 Component 注入机制
3. 修改 MHeaderTool 支持 `InjectionClass`

### Phase 2: 服务框架
1. 定义 `IService` 接口
2. 实现服务注册和注入
3. 服务内部命令队列机制

### Phase 3: 业务迁移
1. 创建新的 PlayerInfo Component
2. 迁移现有 PlayerService 逻辑到 Components
3. 移除 MPlayerService

---

## 七、风险与限制

1. **循环依赖**：Component 不能互相 include，通过 Player 公开接口通信
2. **服务生命周期**：需要明确服务实例的生命周期管理
3. **调试复杂性**：注入机制增加了调试难度

---

## 八、待确认事项

1. `IService` 接口的具体定义
2. 服务注册机制（静态？动态？）
3. 命令队列的具体实现
4. 验证框架的具体语法（`ValidateMeta` 值）
