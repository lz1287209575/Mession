# C++ 代码风格规范 — 设计 Spec

> 起草:2026-07-14
> 状态:v1(已批准)
> 关联:`/root/Mession/CLAUDE.md`(项目总纲,本 spec 实施后只保留快查表 + 跳转)

---

## 0. 一句话

把分散在 `CLAUDE.md`、源码注释、个人习惯里的代码约定合并为 `Docs/CodingStyle.md`,新增 `.clang-format` + `pre-commit` + `Scripts/check-style.sh` 做机器强制,一次性把 43 个 `.cpp` + 107 个 `.h` 走完规范——只动格式与文件头注释,不动运行时行为,不动反射字段 ABI。

## 1. 目标

1. **集中可索引**:把分散约定合并到 `Docs/CodingStyle.md`。`CLAUDE.md` 只保留快查表 + 跳转到该文档。
2. **机器可强制**:`.clang-format` 强制格式化;`pre-commit` 阻断违规提交;`Scripts/check-style.sh` 在 CI 跑同一检查。
3. **全量对齐**:43 个 `.cpp` + 107 个 `.h` 一次性走完规范,代码与文档一一对应。
4. **关键规约**(完整内容见 § 5):
   - 命名:`M*` 类、`S*` 结构、`E*` 枚举、`I*` 接口、`bXxx` bool、PascalCase 函数/变量、`InXxx/OutXxx` 参数。
   - **静态成员:不加 `st_`/`sXxx` 前缀,与普通成员同命名**。
   - **所有标识符禁止下划线**(常量可豁免,允许 `MAX_PACKET_SIZE` 大写蛇形)。
   - include 顺序:本项目头/第三方/标准库四组,同组字典序,组间空行。
   - 错误处理:`MResult`/`TResult`/异常三选一明确;禁止裸 `int`/`bool` 错误码。
   - 日志:`LOG_DEBUG/INFO/WARN/ERROR/FATAL` 五档;FATAL 后必须退出。
   - 文件头注释:每个 `.h`/`.cpp` 顶部 `@file + @brief`,公共 API 加 `@param/@return`。

## 2. 非目标

1. 不动 CMake 编译选项、PCH、第三方依赖选型。
2. 不动 `Build/Generated/` 自动生成代码的格式(MHeaderTool 的责任)。
3. 不动运行时行为、RPC 协议、数据库 schema。
4. 不引入 clang-tidy(只引入 clang-format,避免误报)。
5. 不重写 `MLib.h` 容器实现,只保证使用规范统一。
6. **不动反射字段名(`MPROPERTY` 注册的字段)**:反射字段名是公开 ABI,只允许格式化与文件头注释,不允许改名/重排。

## 3. 现状基线

| 类别 | 状态 |
|------|------|
| **CLAUDE.md** | 已规定命名(`S*`/`M*`/`E*`/`bXxx`/`InXxx`)、STL 别名(`TVector`/`TMap`/`TSharedPtr`)、`MakeShared<T>`、控制流必须花括号、`MSTRUCT + MPROPERTY` 协议结构 |
| **格式工具** | 没有任何 `.clang-format` / `.editorconfig` / `pre-commit` |
| **bool 命名** | 大部分用 `bXxx`,但有反例:`Ready`、`FutureRetrieved`、`bFirst`、`BoolValue`、`bSuccess`、`bFound`、`bAccepted` 混用 |
| **include 顺序** | 杂乱,无统一规则;部分文件字典序,部分不字典序 |
| **错误处理** | `MResult`/`TResult` 已存在(`Common/Runtime/Object/Result.h`),但部分代码仍用 `bool` 返回值 + out-param |
| **日志** | 五档固定已存在,部分高频 `Tick` 用 `LOG_INFO` 而非 `LOG_DEBUG` |
| **文件头注释** | 散乱,部分文件有 Doxygen 风格 `@file`,部分没有 |
| **Build/Generated/** | MHeaderTool 输出,不参与本规范 |

## 4. 目标架构

### 4.1 文件交付物

```
/
├── .clang-format                    # 新增 — 格式化规则
├── .pre-commit-config.yaml          # 新增 — git hook 配置
├── Docs/
│   ├── CodingStyle.md               # 新增 — 完整代码风格文档
│   └── superpowers/specs/2026-07-14-coding-style/design.md  # 本 spec
├── Scripts/
│   ├── check-style.sh               # 新增 — CI 用风格检查脚本
│   └── install-hooks.sh             # 新增 — 本地 pre-commit 安装
└── CLAUDE.md                        # 修改 — 顶部加快查表 + 跳转到 CodingStyle.md
```

### 4.2 文档分层

- `Docs/CodingStyle.md`:完整规范,所有规则详细描述 + 示例 + 反例。
- `CLAUDE.md`:项目总纲,顶部"代码风格"章节缩减为 5-10 行快查表 + 跳转链接。
- `Docs/superpowers/specs/2026-07-14-coding-style/design.md`:本 spec(设计文档)。

## 5. 规范明细

### 5.1 命名规约

| 类别 | 规则 | 示例 |
|------|------|------|
| 类 | `M*` 前缀 | `MPlayerService` |
| 结构 | `S*` 前缀 | `SPlayerConfig` |
| 枚举 | `E*` 前缀 | `EServerType` |
| 接口(纯虚) | `I*` 前缀 | `INetConnection` |
| bool 字段/局部变量/参数 | `bXxx` | `bRunning`、`bHealthy`(误例:`bIsConnected` 应为 `bConnected`) |
| 静态成员 | **不加 `st_`/`sXxx` 前缀,与普通成员同命名** | `static int32 MaxRetries = 3;` |
| 函数/方法 | PascalCase,动词或动词短语 | `Tick`、`HandleClientPacket`、`EncodeEndpoint` |
| 局部变量/参数 | PascalCase | `DeltaTime`、`ConnId` |
| 模板参数 | `T`/`TValue`/`TKey`/`TIterator` | `template<typename TValue>` |
| 入参 | `InXxx`;出参 `OutXxx` | `void Encode(const T& In, T& Out)` |
| 全局指针/变量 | `G` 前缀 + PascalCase | `GGlobalGateway` |
| **常量** | **大写蛇形,允许下划线**(豁免) | `MAX_PACKET_SIZE`、`DEFAULT_TICK_RATE` |
| **其他标识符** | **禁止下划线** | `bHealthy` 而非 `b_healthy` |
| 命名空间 | 小写 + 单词连写,无下划线 | `mynamespace::submodule` |

### 5.2 include 顺序

按 clang-format `IncludeCategories` 四组,组间空行,同组字典序:

1. 当前文件对应头(`.cpp` 包含其同名 `.h`)
2. 本项目头:`"Common/..."`、`"Servers/..."`、`"Protocol/..."`(路径相对 `Source/`)
3. 第三方库:`<boost/...>`、`<gtest/...>`
4. C++ 标准库:`<vector>`、`<memory>`

clang-format 配置:`SortIncludes: true` + `IncludeBlocks: Preserve` + `IncludeCategories` 映射四组。

### 5.3 格式化细则(`.clang-format`)

```yaml
BasedOnStyle: LLVM
IndentWidth: 4
UseTab: Never
ColumnLimit: 240
BraceWrapping:
  AfterClass: true
  AfterFunction: true
  AfterNamespace: true
  AfterStruct: true
  AfterEnum: true
  AfterIf: true
  AfterFor: true
  AfterWhile: true
  AfterDo: true
  AfterCase: true
  AfterTry: true
  Catch: true
  BeforeElse: true
  SplitEmptyFunction: true
  SplitEmptyRecord: true
  SplitEmptyNamespace: true
AllowShortFunctionsOnASingleLine: None
AllowShortIfStatementsOnASingleLine: Never
AllowShortLoopsOnASingleLine: false
AllowShortBlocksOnASingleLine: Never
AllowShortCaseLabelsOnASingleLine: false
PointerAlignment: Left
AccessModifierOffset: 0
AlignConsecutiveAssignments: true
AlignConsecutiveDeclarations: true
AlignConsecutiveMacros: true
SortIncludes: true
IncludeBlocks: Preserve
NamespaceIndentation: All
KeepEmptyLinesAtTheStartOfBlocks: false
MaxEmptyLinesToKeep: 2
Cpp20:
  BracedListInit: true
```

**Allman 风格全开**:所有 `if`/`for`/`while`/函数/类/结构/枚举/命名空间的花括号都换行。这与 `CLAUDE.md` 现有"Always use braces for `if`/`for`/`while`, even single statements"一致。

### 5.4 错误处理

- 业务层**必须**用 `MResult` / `TResult<T, E>`(参考 `Common/Runtime/Object/Result.h`),不要混用。
- 协议层可允许抛异常(`MFuture` 失败传播走 `FAppError` 路径)。
- 不允许返回 `int -1` / `bool false` + 隐式 out-param 模拟错误。
- `LOG_FATAL` 之后代码必须 `return` / `std::abort` / 抛异常;不允许静默继续。

### 5.5 日志

- 五档固定:`LOG_DEBUG/INFO/WARN/ERROR/FATAL`,`FATAL` 约定退出。
- 格式:`LOG_INFO("format %s %d", value, n)`,**禁用**流式 `<<`(避免临时字符串)。
- 服务入口(`Init`/`OnRunStarted`)/退出(`Shutdown`)/错误路径(返回 false 前)必须有日志。
- 高频路径(`Tick` 内)用 `LOG_DEBUG` 而非 `LOG_INFO`。

### 5.6 注释与文件头

- 每个 `.h`/`.cpp` 顶部加 Doxygen 块:
  ```
  /**
   * @file Foo.h
   * @brief <一句话职责>
   */
  ```
- 公共方法用 `@param`/`@return`/`@throws`(无 `MResult` 错误域时)。
- 类/结构在头文件加 `@brief`,复杂类加 `@detail`。
- 命名空间内放 `namespace { /* anonymous */ }` 写文件内辅助函数时,在上面加 `// file-local helpers`。

### 5.7 pre-commit 与 CI 集成

- `.pre-commit-config.yaml` 注册 `clang-format` hook,对暂存文件检查。
- `Scripts/check-style.sh` 对 `Source/` 全部 `.h`/`.cpp` 跑 `clang-format --dry-run --Werror`,失败非零退出。
- CI 工作流(若存在)新增 step:`bash Scripts/check-style.sh`。

## 6. 全量重构执行

### 6.1 commit 拆分

| # | commit | 内容 | 验证 |
|---|--------|------|------|
| 1 | `chore: add Docs/CodingStyle.md, .clang-format, .pre-commit-config.yaml` | 工具与文档,无代码改动 | `pre-commit install` 不报错 |
| 2 | `style(common): apply clang-format + file headers in Source/Common` | 格式化 Common 目录 | `cmake --build` 无新 warning;`validate.py` 通过 |
| 3 | `style(servers): apply clang-format + file headers in Source/Servers` | 格式化 Servers 目录 | 同上 |
| 4 | `style(protocol): apply clang-format + add file headers in Protocol/` | 格式化 Protocol 目录 | 同上 |
| 5 | `style(tests): apply clang-format + add file headers in tests/` + `chore: CLAUDE.md quick-reference table` | 收尾 | 全量 `check-style.sh` 通过 |

### 6.2 反射 ABI 保护

- 任何 `MSTRUCT`/`MCLASS` 内部的 `MPROPERTY` 字段**只允许格式化,不允许重命名/重排/删除**。
- 二进制协议字段(参考 `Endpoint.h:11-17`、`EchoService.h:25-47`)绝不重命名。
- 重构 commit 中**禁止**修改 `MSTRUCT` 注册的字段名拼写与顺序。

## 7. 风险与回滚

### 7.1 主要风险

1. **clang-format 大面积 diff**:列宽 240 + 强制花括号换行,预估 `Source/` 全量 diff 行数 5k-15k。
2. **include 顺序重排**:clang-format `SortIncludes` 会改变顺序,与 PCH 顺序可能冲突。**缓解**:`check-style.sh` 仅做格式检查,build 时 PCH 不重编,顺序问题不立刻浮现。
3. **重命名变量破坏 ABI**:`bHealthy` 等 `MPROPERTY` 字段改名会破坏 wire 兼容性。**缓解**:**禁止改 `MPROPERTY` 字段名**;只格式化,不重命名反射字段。
4. **pre-commit 在 CI 与本地行为差异**:本地没装 clang-format 时 pre-commit hook 跳过;CI 必须装好。
5. **Build/Generated/ 自动生成**:clang-format 检查跳过 `Build/Generated/`,需确认 MHeaderTool 不会因格式问题报错。

### 7.2 回滚方案

- 整组 PR 用 5 个独立 commit,任意 commit 可独立 `git revert`。
- `.clang-format` 是单独 commit,先合入;若后续发现问题,删 `.clang-format` + 清缓存即可。
- `Docs/CodingStyle.md` 是 markdown,任何错误就地修改不影响代码。
- pre-commit hook 仅在本地生效,CI 不依赖 hook 本身,只跑 `Scripts/check-style.sh`。

### 7.3 缓解措施

1. `bHealthy` 等 `MPROPERTY` 字段**不重命名**(只格式化,不动标识符)。
2. 全量重构后立即跑 `cmake --build Build -j4` + `Scripts/validate.py --no-build` 三个核心 suite,确认无回归。
3. 格式化 PR 在本地做一次 `git diff --stat` 抽样 5-10 个文件,人工 review 抽样 diff 后再 push。

## 8. 验收标准

### 8.1 功能性(必须通过)

1. `cmake --build Build -j4` 无 warning 增加(新 warning 数 = 旧 warning 数)。
2. `Scripts/validate.py --no-build` 全 suite 通过。
3. 三个核心 suite(player_state / scene_downlink / combat_commit)单独跑全部通过。
4. `git grep -nE "^\s*if\s*\(.*\)\s*[^{]" -- 'Source/' ':!Build/'` 应**返回空**(确认 CLAUDE.md 短语句必须花括号)。

### 8.2 风格合规性(必须通过)

5. `find Source -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror` 退出码 0。
6. `Docs/CodingStyle.md` 已写完且每章都有至少一个示例。
7. `.pre-commit-config.yaml` 实际生效:故意在某个 `.h` 加坏格式,`git commit` 应被拒。
8. `Scripts/check-style.sh` 在 CI 跑通。
9. **反射 ABI 兼容**:反射字段名数量、字段名拼写、字段顺序在 git diff 中只允许新增、不允许删除/改名(只允许重命名非反射字段)。

### 8.3 非功能性

10. `CLAUDE.md` 中"代码风格"章节更新为跳转到 `Docs/CodingStyle.md`。
11. commit 5 次,每个 commit 独立可 revert。
12. PR 描述明确"这是纯格式变更,不改运行时行为;反射字段名不变"。
