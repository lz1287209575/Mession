# Component Injection 系统实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 Component 注入系统，简化玩家对象模型，支持声明式验证和服务注入

**Architecture:** 扁平 Component 模式替代树形 Player 结构，通过 MHeaderTool 代码生成实现自动注入

**Tech Stack:** C++17, MHeaderTool (代码生成), MReflect (反射系统)

---

## 第一阶段：Async/Await 验证

### Task 1: 验证 MFunction(Async) 代码生成

MHeaderTool 已有 Async 代码生成逻辑 (MHeaderTool.cpp:3024-3193)，需要验证功能完整性。

**Files:**
- Test: `Source/Servers/World/Player/PlayerService.h` (现有的 PlayerLogout 函数)
- Check: `Build/Generated/MPlayerService.mgenerated.cpp`

- [ ] **Step 1: 检查 PlayerLogout 函数签名**

确认 `MFUNCTION(Async)` 和 `MFUTURE` 宏使用正确。

```cpp
// Source/Servers/World/Player/PlayerService.h 应该包含：
MFUNCTION(Async, ServerCall)
MFUTURE(FPlayerLogoutResponse) PlayerLogout(const FPlayerLogoutRequest& Request)
{
    // 函数体包含 AWAIT 和 co_return
    // 注意：不再使用 ParaMeta，所有验证通过 MPROPERTY(ValidateMeta=...) 在属性上声明
}
```

- [ ] **Step 2: 检查生成的代码**

运行 MHeaderTool 生成代码，检查是否生成了状态机。

Run: `Source/Tools/MHeaderTool` (或查看 `Build/Generated/MPlayerService.mgenerated.cpp`)
Expected: 应包含类似 `struct SState` 和 `_run` 方法的续体链代码

- [ ] **Step 3: 验证 AWAIT 解析正确性**

检查生成的 `.mgenerated.cpp` 中：
1. `AWAIT` 调用被解析为 `.Then()` 链
2. `co_return` 被替换为 `_onComplete(TResult::Ok(...))` 或 `_onComplete(TResult::Err(...))`
3. 错误自动短路传播

- [ ] **Step 4: 测试编译**

Run: `cmake --build Build -j4 2>&1 | grep -E "error|warning.*Async|PlayerService"`
Expected: 无编译错误

- [ ] **Step 5: 如果有问题，修复 MHeaderTool.cpp 中的解析逻辑**

需要编译才能知道具体问题，单独编译 MHeaderTool:
Run: `cd Build && cmake --build . --target MHeaderTool -j4`

---

## 第二阶段：Component 注入基础设施

### Task 2: 定义 IComponent 接口

**Files:**
- Create: `Source/Common/Runtime/Component/IComponent.h`

- [ ] **Step 1: 创建 IComponent.h**

```cpp
#pragma once
#include "Common/Runtime/Object/MObject.h"

class IComponent
{
public:
    virtual ~IComponent() = default;

    // 组件附加到 Owner 时调用
    virtual void OnAttach(MObject* Owner) {}

    // 组件从 Owner 分离时调用
    virtual void OnDetach() {}

    // 获取 Owner 引用
    MObject* GetOwner() const { return Owner; }

protected:
    MObject* Owner = nullptr;
};
```

- [ ] **Step 2: 更新 MLib.h 导出**

在 `Source/Common/Runtime/MLib.h` 中添加:
```cpp
#include "Common/Runtime/Component/IComponent.h"
```

- [ ] **Step 3: 提交**

```bash
git add Source/Common/Runtime/Component/IComponent.h Source/Common/Runtime/MLib.h
git commit -m "feat: add IComponent interface for component system"
```

---

### Task 3: 定义 IService 接口

**Files:**
- Create: `Source/Common/Runtime/Service/IService.h`

- [ ] **Step 1: 创建 IService.h**

```cpp
#pragma once

class IService
{
public:
    virtual ~IService() = default;

    // 服务名称，用于日志和调试
    virtual const char* GetServiceName() const = 0;

    // 服务是否可用
    virtual bool IsAvailable() const { return true; }
};
```

- [ ] **Step 2: 提交**

```bash
git add Source/Common/Runtime/Service/IService.h
git commit -m "feat: add IService interface for service injection


```

---

### Task 4: MHeaderTool 支持 InjectionClass 标签

**Files:**
- Modify: `Source/Tools/MHeaderTool.cpp` (约 1800-1900 行处理宏)
- Modify: `Source/Tools/MHeaderTool.h` (SParsedClass 结构)

- [ ] **Step 1: 添加 InjectionClass 到解析结构**

在 `SParsedClass` 结构中添加:
```cpp
struct SParsedClass {
    // ... 现有字段 ...
    std::string InjectionClass;  // InjectionClass=X 中的 X
    std::vector<SParsedFunction> InjectionFunctions;  // MFUNCTION(Injection) 函数
    std::vector<SParsedProperty> InjectionProperties;  // MPROPERTY(Injection) 属性
};
```

- [ ] **Step 2: 解析 MCLASS(InjectionClass=X) 标签**

在宏解析逻辑中添加 (约 1797 行附近):
```cpp
if (MacroName == "MCLASS") {
    // 解析 InjectionClass=X
    if (auto It = Attrs.find("InjectionClass"); It != Attrs.end()) {
        Parsed.InjectionClass = It->second;
    }
}
```

- [ ] **Step 3: 收集 Injection 标记的成员**

解析 `MFUNCTION(Injection)` 和 `MPROPERTY(Injection)`:
```cpp
if (Function.HasAttr("Injection")) {
    ParsedClass.InjectionFunctions.push_back(Function);
}
if (Property.HasAttr("Injection")) {
    ParsedClass.InjectionProperties.push_back(Property);
}
```

- [ ] **Step 4: 生成注入代码到目标类**

在 `WriteGeneratedSource` 中添加 (MPlayerService.mgenerated.cpp 类似的逻辑):
```cpp
// 为目标类生成注入的函数和属性
if (!Parsed.InjectionClass.empty()) {
    // 找到目标类
    // 生成函数转发代码
    // 生成属性访问代码
}
```

- [ ] **Step 5: 编译验证**

需要单独编译 MHeaderTool 验证解析逻辑:
Run: `cd Build && cmake --build . --target MHeaderTool -j4`

- [ ] **Step 6: 提交**

```bash
git add Source/Tools/MHeaderTool.cpp Source/Tools/MHeaderTool.h
git commit -m "feat: MHeaderTool support for InjectionClass tag

Parse MCLASS(InjectionClass=X) and generate injection code for
MFUNCTION(Injection) and MPROPERTY(Injection) members.


```

---

### Task 5: 服务注入生成

**Files:**
- Modify: `Source/Tools/MHeaderTool.cpp`

- [ ] **Step 1: 识别 IService* 成员**

在 `WriteGeneratedSource` 中添加:
```cpp
// 检测 IService* 或 IXXXService* 类型的成员
if (Property.Type.find("Service") != std::string::npos) {
    // 生成 SetXxxService() 方法
}
```

- [ ] **Step 2: 生成 Setter 方法**

```cpp
// 生成代码:
// void SetDbService(IDbService* InDb) { Db = InDb; }
Out << "    void Set" << ServiceName << "(" << Property.Type << "* InService) {\n";
Out << "        " << Property.Name << " = InService;\n";
Out << "    }\n";
```

- [ ] **Step 3: 提交**

```bash
git add Source/Tools/MHeaderTool.cpp
git commit -m "feat: generate service setter methods for IService* members

Auto-generate SetXxxService() methods for components with service dependencies.


```

---

## 第三阶段：验证框架

### Task 6: 实现 ValidateMeta 验证

**Files:**
- Create: `Source/Common/Runtime/Validation/PropertyValidation.h`
- Modify: `Source/Tools/MHeaderTool.cpp` (函数参数验证生成)

- [ ] **Step 1: 定义验证宏**

```cpp
// Source/Common/Runtime/Validation/PropertyValidation.h
#pragma once

#include "Common/Runtime/Object/Result.h"

// 验证结果
using FValidationResult = TResult<void, FAppError>;

// 基础验证函数
FValidationResult ValidateNotZero(uint64 Value, const char* PropertyName);
FValidationResult ValidateNotEmpty(const MString& Value, const char* PropertyName);
FValidationResult ValidateRange(int32 Value, int32 Min, int32 Max, const char* PropertyName);
```

- [ ] **Step 2: 实现验证函数**

```cpp
inline FValidationResult ValidateNotZero(uint64 Value, const char* PropertyName) {
    if (Value == 0) {
        return FValidationResult::Err(FAppError{
            "validation.not_zero",
            MString(PropertyName) + " must not be zero"
        });
    }
    return FValidationResult::Ok();
}
```

- [ ] **Step 3: MHeaderTool 生成验证调用**

在 `WriteGeneratedSource` 中，对于有 `ValidateMeta` 的参数:
```cpp
// 生成验证代码:
// auto _v = ValidateNotZero(Request.PlayerId, "PlayerId");
// if (_v.IsErr()) return MServerCallAsyncSupport::MakeErrorFuture<_ResponseType>(_v.GetError());
```

- [ ] **Step 4: 提交**

```bash
git add Source/Common/Runtime/Validation/PropertyValidation.h
git commit -m "feat: add property validation framework with ValidateMeta

Implement ValidateNotZero, ValidateNotEmpty, ValidateRange validators.
MHeaderTool generates validation calls for decorated properties.


```

---

## 第四阶段：示例 Component

### Task 7: 创建示例 PlayerInfoComponent

**Files:**
- Create: `Source/Protocol/Messages/Player/PlayerInfoMessages.h`
- Create: `Source/Servers/World/Player/Components/MPlayerInfoComponent.h`

- [ ] **Step 1: 创建 Request/Response 定义**

```cpp
// Source/Protocol/Messages/Player/PlayerInfoMessages.h
#pragma once
#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct SGetPlayerInfoRequest
{
    GENERATED_BODY()

    MPROPERTY(ValidateMeta=NotZero)
    uint64 PlayerId;
}

MSTRUCT()
struct SGetPlayerInfoResponse
{
    GENERATED_BODY()

    MPROPERTY()
    MString PlayerName;

    MPROPERTY()
    int32 Level;

    MPROPERTY()
    int32 ExperiencePoints;
}
```

- [ ] **Step 2: 创建 Component 实现**

```cpp
// Source/Servers/World/Player/Components/MPlayerInfoComponent.h
#pragma once
#include "Common/Runtime/Component/IComponent.h"
#include "Protocol/Messages/Player/PlayerInfoMessages.h"

MCLASS(InjectionClass=MPlayer)
class MPlayerInfoComponent : public IComponent
{
public:
    void OnAttach(MObject* Owner) override;

    MFUNCTION(Async, Injection)
    SFutureResult<SGetPlayerInfoResponse> GetPlayerInfo(const SGetPlayerInfoRequest& Request);

    // 服务注入
    void SetDbService(IService* InDb) { DbService = InDb; }

private:
    IService* DbService = nullptr;
};
```

- [ ] **Step 3: 创建实现文件**

```cpp
// Source/Servers/World/Player/Components/MPlayerInfoComponent.cpp
#include "MPlayerInfoComponent.h"

void MPlayerInfoComponent::OnAttach(MObject* Owner)
{
    Owner_ = Owner;
}

SFutureResult<SGetPlayerInfoResponse> MPlayerInfoComponent::GetPlayerInfo(
    const SGetPlayerInfoRequest& Request)
{
    auto Profile = AWAIT(DbService->QueryProfile(Request.PlayerId));
    SGetPlayerInfoResponse Response;
    Response.PlayerName = Profile->Name;
    Response.Level = Profile->Level;
    Response.ExperiencePoints = Profile->Exp;
    co_return Response;
}
```

- [ ] **Step 4: 提交**

```bash
git add Source/Protocol/Messages/Player/PlayerInfoMessages.h
git add Source/Servers/World/Player/Components/MPlayerInfoComponent.h
git add Source/Servers/World/Player/Components/MPlayerInfoComponent.cpp
git commit -m "feat: add MPlayerInfoComponent as example component

Demonstrates Component injection pattern with:
- InjectionClass=MPlayer for auto-injection
- MFUNCTION(Async, Injection) for RPC
- ValidateMeta=NotZero for validation
- IService injection via setter


```

---

## 第五阶段：清理与迁移

### Task 8: 移除 PlayerService.h 中的 coroutine include

**Files:**
- Modify: `Source/Servers/World/Player/PlayerService.h`

- [ ] **Step 1: 删除不必要的 include**

```cpp
// 删除:
// #include <coroutine>
// #include <optional>  // 如果不再需要
```

- [ ] **Step 2: 提交**

```bash
git add Source/Servers/World/Player/PlayerService.h
git commit -m "chore: remove unnecessary #include <coroutine>

Project uses C++17, coroutine header is not needed.


```

---

## 验收标准

1. [ ] `MFUNCTION(Async)` 函数生成正确的续体链代码
2. [ ] `MCLASS(InjectionClass=X)` 正确解析
3. [ ] `MFUNCTION(Injection)` 和 `MPROPERTY(Injection)` 自动注入到目标类
4. [ ] `MPROPERTY(ValidateMeta=NotZero)` 生成验证代码
5. [ ] `IService*` 成员生成 `SetXxxService()` 方法
6. [ ] 示例 Component 可以编译通过
7. [ ] 现有测试套件全部通过

---

## 依赖关系

```
Task 1 (Async 验证)
    ↓
Task 2 (IComponent) → Task 4 (MHeaderTool)
Task 3 (IService)   → Task 5 (Service 生成)
Task 6 (Validation) → Task 4 (MHeaderTool)
    ↓
Task 4 + Task 5 + Task 6
    ↓
Task 7 (示例 Component)
    ↓
Task 8 (清理)
```
