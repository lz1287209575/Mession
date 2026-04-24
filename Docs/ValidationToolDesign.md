# Validation 工具化设计

## 背景

当前仓库的自动验证能力主要集中在 `Scripts/validate.py`。
它已经不再只是“最小登录检查”，而是承担了本地起服、客户端协议收发、主链路回归、suite 分组、日志输出和进程清理等多项职责。

这让它在工程早期很高效，但随着仓库逐步从“单脚本回归”进入“长期维护的服务端骨架”阶段，继续把验证能力维持成一个大脚本，会遇到几个明显问题：

- 环境管理、协议编解码、业务断言、suite 编排全部耦合在单文件里
- 用例更偏顺序执行，天然容易复用同一批状态并产生串扰
- 文本日志适合人读，但不适合控制面、UI、CI、集群工具直接消费
- `server_control_api`、`server_cluster`、未来桌面工具都很难复用验证本体，只能“调用脚本”
- 用例粒度仍然偏粗，失败定位更像“脚本跑到第几段挂了”，而不是“某个 case / step 失败”

因此，`validate` 需要被重新定义为一个独立工具域，而不是继续扩展单个脚本。

## 设计目标

### 总目标

把当前 `validate.py` 演进为一套可复用、可扩展、可结构化输出的验证工具。

### 具体目标

- 把验证能力从单文件脚本升级为独立模块
- 明确分层：CLI、环境、传输、fixture、suite、断言、报告
- 支持本地、agent、cluster 三种运行模式
- 支持 suite / case / step 三级结果模型
- 默认降低用例状态串扰，逐步走向更强隔离
- 保持当前 `python3 Scripts/validate.py ...` 的使用习惯，平滑迁移
- 让 `server_control_api`、`server_cluster` 和未来 UI 可以复用同一套验证能力

## 非目标

本次设计不追求一步到位做成一个“万能测试平台”，以下内容不在第一阶段范围内：

- 不直接把验证工具重写成新的 C++ 可执行程序
- 不强依赖 `pytest` / `unittest` 等第三方测试框架
- 不先做压测、fuzz、长稳 soak test 平台
- 不先做复杂的分布式调度器
- 不先解决所有单元测试缺口

第一阶段重点是把验证能力结构化、工具化，而不是追求所有验证类型统一。

## 当前问题总结

以 `Scripts/validate.py` 当前实现为例，主要有这些结构性问题：

### 1. 单文件职责过多

当前一个脚本同时负责：

- CLI 参数解析
- 构建与起停服
- 连接 Gateway
- 协议发包收包
- Response 解析
- suite 分组与 case 编排
- 断言与日志输出
- 清理进程与日志目录

这会让单点修改范围越来越大，也会让未来复用成本越来越高。

### 2. 用例组织仍偏“顺序脚本”

现有 suite 是在总回归脚本里按测试编号切出来的分组，而不是显式的 case 集合。
这会带来两个后果：

- 一个 suite 内部仍依赖前序状态
- 某个 case 的前置、清理、资源分配不明确

### 3. 状态隔离不足

当前很多验证步骤复用同一个 `player_id` 和同一个长连接上下文。
这对快速覆盖主链路很方便，但一旦某步把角色状态改掉，后续 case 很容易误把“初始态”当成“当前态”。

### 4. 报告不可结构化消费

现在输出主要是终端日志，适合人看，但不适合：

- 控制面 API 直接返回
- UI 展示 suite/case 树
- CI 使用 JUnit / JSON 汇总
- Cluster 模式做跨节点结果聚合

### 5. 与现有工具链集成不自然

仓库已经存在这些基础设施：

- `Scripts/servers.py`
- `Scripts/server_control_api.py`
- `Scripts/server_cluster.py`

但 `validate` 还没有成为一个被这些工具复用的核心能力模块。

## 核心设计思路

### 设计原则

整个设计围绕以下原则展开：

1. `validate` 是工具，不是单脚本
2. suite 是注册对象，不是编号块
3. case 默认应该有明确的隔离语义
4. transport 与业务断言分离
5. 输出必须结构化，文本只是视图之一
6. `server_control_api` / `server_cluster` 应复用验证本体，而不是复制一份逻辑

### 目标形态

最终期望是：

- `Scripts/validate.py` 只是兼容入口
- 真正的能力下沉到 `Scripts/validation/`
- CLI、本地控制面、cluster 工具都调用同一套 runner

## 模块分层

建议在 `Scripts/` 下新增 `validation/` 目录，形成独立工具域。

### 建议目录结构

```text
Scripts/
  validate.py
  validation/
    __init__.py
    cli.py
    runner.py
    config.py
    context.py
    model.py
    registry.py
    report.py
    errors.py
    ids.py
    schema_loader.py

    env/
      __init__.py
      base.py
      local_env.py
      agent_env.py
      cluster_env.py

    transport/
      __init__.py
      gateway_client.py
      packets.py
      reflect.py
      reflection_decoder.py
      compat_parsers.py
      stable_ids.py
      codecs/
        __init__.py
        login.py
        player.py
        social.py
        combat.py
        scene.py

    fixtures/
      __init__.py
      players.py
      social.py
      combat.py
      state_reset.py

    assertions/
      __init__.py
      common.py
      player.py
      social.py
      combat.py

    suites/
      __init__.py
      player_state.py
      runtime_dispatch.py
      scene_downlink.py
      combat_commit.py
      runtime_social.py
      forward_errors.py

    reporters/
      __init__.py
      text_reporter.py
      json_reporter.py
      junit_reporter.py
```

### 各层职责

#### `cli.py`

只负责：

- 解析命令行参数
- 构建 `ValidationConfig`
- 调用 `ValidationRunner`
- 转换 exit code

CLI 不应该承担业务验证逻辑。

#### `runner.py`

负责整个运行生命周期：

- 发现 suite
- 按配置筛选 suite / case
- 初始化 environment
- 执行 suite / case / step
- 汇总结果
- 驱动 report 输出

`runner` 是 validate 工具的核心调度器。

#### `env/`

负责“验证在哪儿跑”。

第一阶段建议先实现 `local_env.py`，后续补 `agent_env.py` 与 `cluster_env.py`。

- `local_env.py`
  - 复用 `Scripts/servers.py`
  - 负责本地起服、停服、ready 检查、日志目录整理
- `agent_env.py`
  - 复用 `Scripts/server_control_api.py`
  - 通过 API 触发环境准备、验证执行、日志采集
- `cluster_env.py`
  - 复用 `Scripts/server_cluster.py`
  - 负责选择 node、转发执行、汇总结果

#### `transport/`

负责“验证如何与服务通信”。

应把当前 `validate.py` 里的这些能力下沉：

- Gateway socket 连接
- `MT_FunctionCall` 打包与解包
- stable function id 计算
- ReflectReader / ReflectWriter
- 各类响应编解码

这样 suite 层只关心业务语义，而不关心包格式细节。

这里的长期方向不是继续积累大量手写 `parse_*_response(...)`，而是：

- 在生成阶段导出协议 Schema
- validate 工具加载 Schema
- 用通用 `reflection_decoder` 解码 payload

也就是说：

- `schema_loader.py` 负责加载协议 Schema
- `transport/reflection_decoder.py` 负责按 Schema 解码
- `transport/compat_parsers.py` 只作为迁移期兼容层存在

对应的协议 Schema 草案见：

- [ValidationProtocolSchema.md](/root/Mession/Docs/ValidationProtocolSchema.md)

#### `fixtures/`

负责测试前置与资源生命周期。

建议提供这些 fixture：

- `PlayerFixture`
  - 登录
  - 获取 `player_id`
  - 确保进入目标场景
  - 查询 baseline
- `DualPlayerFixture`
  - 两玩家同场景
  - 用于 scene/social/combat
- `MonsterFixture`
  - 生成怪物
  - 记录 unit ref
  - 结束时确认或执行回收
- `PartyFixture`
  - 创建队伍
  - 邀请
  - 接收
  - 清理

fixture 层的意义是让 suite 不再自己手写一套重复前置逻辑。

#### `assertions/`

负责统一断言语义，避免 suite 中散落大量重复的字典比较代码。

例如：

- `assert_login_ok(...)`
- `assert_profile_matches(...)`
- `assert_progression_delta(...)`
- `assert_party_member_count(...)`
- `assert_combat_reward_delta(...)`

未来如果业务规则变了，只需要集中修改断言帮助函数，而不是全局搜索脚本文本。

#### `suites/`

每个 suite 拆成独立模块，通过注册机制被 runner 发现。

建议保留当前已有 suite 名称，降低迁移成本：

- `player_state`
- `runtime_dispatch`
- `scene_downlink`
- `combat_commit`
- `runtime_social`
- `forward_errors`

#### `reporters/`

负责把运行结果输出成不同格式：

- 文本终端输出
- JSON 结果文件
- JUnit XML

这层不参与断言与执行，只负责结果呈现。

## 核心模型

验证工具需要显式的领域模型，而不是“打印日志 + return False”。

### 运行模型

```python
class ValidationRun:
    run_id: str
    mode: str
    started_at: str
    config: dict
```

### suite 模型

```python
class ValidationSuite:
    name: str
    description: str
    tags: list[str]
    cases: list["ValidationCase"]
```

### case 模型

```python
class ValidationCase:
    id: str
    name: str
    tags: list[str]
    timeout_seconds: float
    isolation: str
    execute: Callable[[ValidationContext], "ValidationCaseResult"]
```

### step 结果模型

```python
class ValidationStepResult:
    name: str
    status: str
    started_at: str
    finished_at: str
    duration_ms: int
    details: dict
    error_code: str | None
    message: str | None
```

### case / suite 结果模型

```python
class ValidationCaseResult:
    case_id: str
    status: str
    step_results: list[ValidationStepResult]

class ValidationSuiteResult:
    suite_name: str
    status: str
    case_results: list[ValidationCaseResult]
```

这套模型的作用是把当前“第几个测试挂了”升级成“哪一个 suite / case / step 失败了”。

## Suite 与 Case 设计

### 从编号测试迁移到注册式 suite

当前 `validate.py` 更接近“大流程 + test id 开关”。
工具化后，建议改成“suite 显式注册 + case 列表”。

例如 `combat_commit` 不再是“测试 16、17、30、38、39、40、41、42、43 的集合”，而是以下显式 case：

- `login_and_enter_scene`
- `cast_skill_on_player`
- `spawn_monster`
- `cast_skill_on_monster`
- `kill_monster_reward_commit`
- `dead_monster_retarget_rejected`

### Case 的隔离级别

每个 case 应显式声明隔离语义：

- `shared`
  - 允许复用同一批状态
  - 适合主链路连续验证
- `isolated_player`
  - 每个 case 分配独立测试玩家
  - 建议作为大多数业务 case 默认选项
- `isolated_env`
  - 每个 case 或 suite 独立环境
  - 适合高风险回归或后端故障类场景
- `readonly_probe`
  - 仅检查状态，不引入写操作

建议默认选择 `isolated_player`，只有明确需要串联的场景才使用 `shared`。

## 环境抽象

### 统一环境接口

建议定义统一抽象：

```python
class ValidationEnvironment:
    def prepare(self) -> None: ...
    def start(self) -> None: ...
    def stop(self) -> None: ...
    def status(self) -> dict: ...
    def logs(self) -> dict[str, str]: ...
    def endpoint(self, service_name: str) -> str: ...
```

### Local 模式

本地模式应作为第一阶段 MVP，负责：

- 可选构建
- 清理残留进程
- 启动服务
- 端口 ready 检查
- 组织日志目录
- 失败后导出关键信息

建议直接复用 `Scripts/servers.py`，不要再复制一份起停服逻辑。

### Agent 模式

后续通过 `Scripts/server_control_api.py` 实现：

- 远程准备环境
- 触发验证运行
- 查询 run 状态
- 拉取 JSON 报告与日志片段

### Cluster 模式

后续通过 `Scripts/server_cluster.py` 实现：

- 选择目标 node
- 下发验证任务
- 汇总结果
- 聚合错误与日志

## 传输与编解码设计

### 目标

把当前 `validate.py` 中与包格式相关的部分全部下沉，suite 层只看到语义化接口。

### 低层能力

建议拆成：

- `gateway_client.py`
  - connect
  - disconnect
  - call
  - recv_downlink
- `packets.py`
  - 包头构建
  - call id 管理
- `reflect.py`
  - 低层 reader/writer
- `stable_ids.py`
  - `MClientApi` / `MClientDownlink` 稳定 id

### codec 层

将当前 `parse_xxx_response(payload) -> dict` 逐步演进为“Schema 驱动的通用 decode”，必要时再在 decode 结果之上封装 dataclass / typed object。

推荐方向：

- `decode_struct("FPlayerQueryProfileResponse", payload)`
- `decode_struct("FWorldCastSkillResponse", payload)`

例如：

```python
@dataclass
class QueryProfileResponse:
    success: bool
    player_id: int
    current_scene_id: int
    gold: int
    equipped_item: str
    level: int
    experience: int
    health: int
    error: str
```

这样可以减少 suite 层对字符串 key 的依赖，并提升可维护性。

### Schema 驱动优先，兼容 parser 兜底

短期内可以保留少量手写 parser 作为迁移兼容，但它们应被视为过渡产物，而不是最终结构。

原则上：

1. 新协议解析优先补 Schema
2. 再接入通用 decoder
3. 手写 parser 只用于迁移期兜底，不再持续扩张

## Fixture 与资源分配

### 统一资源分配器

当前很多问题来自固定 `player_id` 复用。

建议引入统一资源分配器：

```python
class ResourceAllocator:
    def allocate_player_id(self, suite: str, case: str, slot: int = 0) -> int: ...
    def allocate_run_tag(self) -> str: ...
```

### 分配策略

建议：

- 不再手写固定玩家 id
- 使用 `run_id + suite + case + slot` 计算稳定 id
- 支持调试模式下显式指定 id base

### baseline / delta 模式

对于写操作与奖励提交类 case，建议用“baseline + delta”断言，而不是写死绝对值。

例如怪物击杀奖励：

- 先记录 `QueryProfile / QueryInventory / QueryProgression`
- 再执行击杀
- 最后断言：
  - `gold = baseline.gold + reward.gold`
  - `experience = baseline.experience + reward.exp`
  - `equipped_item` 不变
  - `health` 是否应变，由当前业务规则决定

这样能大幅降低状态串扰导致的误判。

## 断言层设计

### 设计目标

把常见的业务断言统一收口，避免 suite 中重复出现大量结构判断。

### 示例

```python
assert_login_ok(response, expected_player_id=player.id)
assert_profile_matches(response, current_scene_id=2, health=75)
assert_inventory_delta(response, baseline=baseline_inventory, gold_delta=25)
assert_progression_after_reward(response, baseline=baseline_progression, exp_delta=120)
```

### 价值

- suite 可读性更高
- 业务规则集中管理
- 错误输出更统一
- 修改协议字段名或计算规则时影响面更小

## 报告与结果输出

### 必须支持的输出格式

第一阶段建议至少支持：

- `text`
  - 给本地终端看
- `json`
  - 给控制面、UI、CI 消费

第二阶段再补：

- `junit`
  - 给 CI 系统直接解析

### JSON 结果结构

建议结果至少包含：

- run metadata
- suite / case / step 结果树
- 使用的配置
- 环境模式
- 关键日志目录
- 失败详情

示例：

```json
{
  "run_id": "2026-04-24T03:10:11Z-local-abc123",
  "status": "failed",
  "mode": "local",
  "suites": [
    {
      "name": "combat_commit",
      "status": "failed",
      "cases": [
        {
          "id": "kill_monster_reward_commit",
          "status": "failed",
          "steps": [
            {
              "name": "query_profile_after_kill",
              "status": "failed",
              "message": "reward delta mismatch"
            }
          ]
        }
      ]
    }
  ],
  "artifacts": {
    "logs_dir": "Build/validate_logs"
  }
}
```

### 文本输出要求

终端输出仍然重要，但它应该来自 report 层，而不是由 suite 直接 `print`。

## CLI 设计

### 兼容目标

保留现有入口：

```bash
python3 Scripts/validate.py --build-dir Build --no-build --suite runtime_dispatch
```

同时逐步演进为更清晰的子命令形式：

```bash
python3 Scripts/validate.py list
python3 Scripts/validate.py run --suite runtime_dispatch
python3 Scripts/validate.py run --suite combat_commit --case kill_monster_reward_commit
python3 Scripts/validate.py run --mode agent --agent-url http://127.0.0.1:18080
python3 Scripts/validate.py run --mode cluster --cluster-config Config/server_cluster.example.json
```

### 推荐命令

- `list`
  - 列出 suite / case
- `describe`
  - 查看 suite 描述、标签、依赖
- `run`
  - 执行 suite / case
- `doctor`
  - 检查构建产物、端口、环境 readiness
- `replay`
  - 按 run id / seed 重放失败 case

第一阶段只需要稳定好 `list` 与 `run`。

## 配置模型

建议把 CLI 参数统一收口到配置对象。

```python
@dataclass
class ValidationConfig:
    mode: str = "local"
    build_dir: Path = Path("Build")
    timeout_seconds: float = 30.0
    build_before_run: bool = False
    enable_mgo: bool = True
    mongo_db: str = "mession_validate_sandbox"
    mongo_collection: str = "world_snapshots"
    suites: list[str] = field(default_factory=lambda: ["all"])
    cases: list[str] = field(default_factory=list)
    tags: list[str] = field(default_factory=list)
    output_format: str = "text"
    json_out: Path | None = None
    junit_out: Path | None = None
    fail_fast: bool = False
    keep_env_on_failure: bool = False
```

后续可再支持 JSON / YAML 配置文件，但第一阶段可以先用 CLI 参数构建该对象。

## 与现有工具链的集成

### 与 `servers.py` 的关系

- `servers.py` 继续作为本地环境起停服的底层工具
- validate 不应复制一份新的起停服逻辑

### 与 `server_control_api.py` 的关系

有两步集成方式：

1. 过渡期：control api 仍通过 subprocess 调 `Scripts/validate.py`
2. 稳定后：control api 直接 import `validation.runner`

最终应支持：

- 发起验证任务
- 查询运行状态
- 拉取结构化结果

### 与 `server_cluster.py` 的关系

cluster 模式应复用它的 node/controller 体系，不再自行实现一套新的远程调度逻辑。

## 迁移计划

建议分 4 个阶段推进。

### Phase 1：从大文件拆成工具模块

目标：

- 保持现有 CLI 兼容
- 把 `validate.py` 中的通用能力拆入 `Scripts/validation/`

范围：

- 抽出 config
- 抽出 runner
- 抽出 transport
- 抽出 suites 基础注册机制
- 文本结果仍可先保持与当前接近

此阶段不追求所有 suite 彻底重写。

### Phase 2：suite / case 注册化

目标：

- 每个 suite 迁移为独立模块
- 从 test id 分组演进为显式 case 集合

范围：

- `player_state`
- `runtime_dispatch`
- `scene_downlink`
- `combat_commit`
- `runtime_social`
- `forward_errors`

### Phase 3：fixture 与隔离机制落地

目标：

- 引入 player allocator
- 引入 baseline / delta 断言
- 为写操作类 case 减少状态串扰

范围：

- 玩家 fixture
- 双玩家 fixture
- 怪物 fixture
- baseline 采样与统一断言

### Phase 4：控制面与 cluster 集成

目标：

- 让 `server_control_api` 和 `server_cluster` 直接复用验证工具本体
- 支持 JSON / JUnit 输出

范围：

- API 集成
- 远程结果回传
- 结构化报告

## 第一阶段 MVP 建议

为了控制风险，建议第一阶段只交付以下内容：

- 保留 `Scripts/validate.py` 兼容入口
- 新增 `Scripts/validation/runner.py`
- 新增 suite 注册机制
- 先把当前 6 个 suite 拆到独立模块
- 支持 `text + json` 两种输出
- 先只支持 `local` 模式

这样可以在不大改现有调用方式的前提下，让架构方向先走正。

## 风险与兼容策略

### 风险 1：迁移过程中破坏当前回归入口

策略：

- 保持旧 CLI 参数兼容
- 先做薄封装重定向，再逐步下沉逻辑

### 风险 2：suite 重写期间语义漂移

策略：

- 第一阶段不改协议与业务语义
- 先迁移结构，再逐步优化断言与隔离

### 风险 3：工具化范围膨胀

策略：

- 先只做 local mode
- 先只支持 text/json
- 先不引入新的测试框架依赖

## 结论

`validate` 应该被视为仓库的“验证平台入口”，而不是继续增长的单脚本。

第一步最合理的方向不是立刻重写所有验证逻辑，而是：

1. 先把 `Scripts/validate.py` 拆成独立工具模块
2. 再把 suite 注册化、case 化
3. 然后引入 fixture、隔离和结构化报告
4. 最后接入 `server_control_api` 与 `server_cluster`

这样可以在不打断当前开发节奏的前提下，把验证能力逐步从“脚本思维”演进到“工具思维”。
