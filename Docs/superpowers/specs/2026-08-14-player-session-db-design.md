# 玩家会话与数据库接入设计(2026-08-14, 定稿)

> 状态:设计定稿(历轮决策:DBService 双形态 → ORM → 存储 → Player → ActorMember 框架,见 §8)。
> 前置:`Docs/superpowers/specs/2026-07-24-cpp17-async-await.md`(async 模型)、
> `Docs/superpowers/specs/2026-07-13-service-registry-design.md`(服务发现)。

## 文档索引(阅读顺序)

| 文档 | 内容 | 读者 |
|---|---|---|
| **本文** | 决策主档:服务拆分 / DBService / ORM / 存储 / 玩家模块决策摘要 | 所有人(入口) |
| `Docs/superpowers/specs/2026-08-14-player-design.md` | Player 可实施规格:分层 / 服务 / 成员 / 时序 / 布局 / 顺序 | 实现 Player 者 |
| `Docs/superpowers/specs/2026-08-14-actor-member-framework.md` | F0 框架设计:Actor/ActorMember 类型、生成器、运行时、寻址 | 实现框架者 |

顺序:本文(决策)→ player-design(Player 规格)→ actor-member-framework(F0 框架)。

## 1. 背景与目标

当前 PoC 只有 Echo 回声业务。本文设计第一个真实业务模块:**玩家会话与持久化**,
同时点亮两个基建缺口:

- `MClientTargetResolver` + `CallClient` 下行推送链路(CLAUDE.md Active gaps)
- 首个真实数据库接入:**独立 DBService 进程**(行业常规做法:业务不直连 DB)

目标形态:业务层同质 worker 池(玩家 Actor 按 PlayerId 平铺),数据经独立 DBService
落 Mongo/MySQL,登录/存取全 async。

## 2. 总体形态与服务拆分

### 2.1 总体形态

```
客户端(UE / Lua)
   │ MT_FunctionCall(LoginRequest)      │ 下行 push(Notify/Profile 同步)
   ▼                                    ▲
Gateway(会话网关:connId↔playerId 绑定、下行出口)
   │ server RPC(CallRemote / CallToActor)
   ▼
LoginService(认证/会话进程——信任边界,与 PlayerService 同构:零业务协议 + MLoginAuth 认证成员)
   │
业务 worker 池(验证:EchoService ×N —— 验证管道;正式:MPlayerService ×N —— Player 在线业务)
   │ 业务只发 Schema RPC(async,不碰 DB)
   ▼
DBService(独立进程:可选注册 Registry 服务发现,也可独立剥离配置直连——双形态见 §3.6)
   │
   ├─ MMongoDatabaseClient (mongocxx,当前环境启用)
   └─ MMysqlDatabaseClient (MySQL C API,条件编译,本轮写代码)
```

**职责边界**:
- 业务进程:零 DB 代码,只定义 Schema(反射 MSTRUCT)并 RPC 存取
- DBService:统一数据访问(连接池、重试、双 backend 适配),Schema 无关(集合+Key+JSON)
- 业务侧"无 DB 线程"成立:阻塞发生在 DBService 进程内
- **DBService 可独立编译/独立部署(可剥离)**:不依赖 mession 服务发现与服务器框架,
  其他服务器经 SDK 直连(见 §3.6)

### 2.2 服务拆分准则(力度把控)

**服务(进程/部署单元)是成本,不是美德**——每多一个服务 = 部署单元 + 配置 + 日志/监控 +
发布节奏 + 链路追踪。拆分力度按下述信号判断,**聚合优先、按需拆分**。

**拆的信号(满足其一才值得拆)**:
1. **独立扩展单元**:有独立扩/缩容需求(如无状态 worker 池、DB 代理、网关)。
2. **技术栈隔离**:引入独立依赖/驱动/阻塞 IO(DB 驱动 → DBService;AI 引擎、物理等)。
3. **失败域/阻塞隔离**:阻塞或崩溃不应拖垮其他服务(DB 线程阻塞 → DBService 进程内)。
4. **信任边界**:对外暴露(客户端入口 → Gateway;第三方接入 → 独立网关)。
5. **独立部署形态**:需要脱离 mession 生态独立交付(DBService 形态 B)。

**不拆的信号(放业务进程内,由 actor/组件承载)**:
- 仅 1~2 个调用方、无独立扩展需求——纯业务模块(登录流程、背包、好友、聊天)
  → 进业务 worker 池的 actor/handler,不拆服务。
- "职责清晰"不是拆服务的理由(actor 模型已提供模块边界)。
- 无独立技术栈、无独立安全/失败域需求。

**默认规则**:拿不准时先放业务进程(actor 承载),出现上述拆的信号再拆——服务化容易,
合并难;业务逻辑永远进 actor/handler,不进薄 server 连接层(见 CLAUDE.md)。

**当前服务清单与判定**:

| 服务 | 判定 | 依据 |
|---|---|---|
| MServiceRegistry | ✅ 独立 | 基础设施,发现/心跳,独立扩缩 |
| GatewayServer | ✅ 独立 | 信任边界 + 客户端唯一入口 |
| LoginService | ✅ 独立 | **认证信任边界**:密码/token/会话票据,鉴权演进独立;认证/建号/选实例/踢旧裁决(MLoginAuth 成员) |
| EchoService(×N) | ✅ 保留(验证桩) | **PoC 验证管道**(Registry→Echo→Gateway),非正式业务,不承载 Player |
| MPlayerService(×N) | ✅ 正式业务池 | 扩展单元;**仅在线业务**(不含 Login);PlayerActor 按 PlayerId 分布;背包/好友等模块在其内做 actor,**不**再拆服务 |
| DBService | ✅ 独立 | 技术栈隔离 + 失败域 + 可剥离形态 |

### 3.0 ORM 定位与能力边界

DBService = **轻量持久化 ORM**:业务定义 Schema(反射 MSTRUCT),ORM 管建表/映射/CRUD,
backend 各用所长。基于反射体系已有能力(`MClass::GetProperties()` + `MProperty::{Name,Type}`
+ 反射↔JSON 桥)组装,不引入外部 ORM 依赖。

**做**:
- Schema 声明式定义 → 建表预热(§3.4:SDK 初始化阶段 SyncTables,幂等 + 版本化)
- 标量字段类型映射(EPropertyType → SQL 列);复杂字段(嵌套 Struct/Vector)→ JSON 子列
- 按 Key CRUD:`FindOne` / `UpsertOne` / `DeleteOne`(反射序列化,对象级操作)
- 第一版写入为**整行 Upsert**(`INSERT ... ON DUPLICATE KEY UPDATE` / Mongo `replaceOne`),字段级增量后置

**不做**(避免复杂度爆炸):
- 关联导航/外键 JOIN(游戏数据为聚合根,不需要)
- **LINQ-to-SQL 式数据库条件查询**(表达式树 → SQL 翻译):第一版只 Key 点查;后置为
  "索引字段白名单"有限查询(白名单字段建索引/生成列,支持简单等值 `WHERE`;简化链式 API
  `Filter(字段, 值)`,不做完整 LINQ 语法)——覆盖低频运营/匹配查询
- **LINQ-to-Objects(内存集合操作)不在"做/不做"范围**:C++ 标准库等价物
  (`find_if`/`transform`/lambda + TVector)已具备,成员/actor 内内存数据处理直接使用
- 完整迁移工具链(第一版只建缺失表,不迁移既有结构;字段变更后置)
(轻量持久化 ORM 层)

### 3.1 协议(集合 + Key + JSON,backend 无关)

> 协议里的 `Json` 是**传输格式**(Schema 反射序列化);backend 收到后各自解析并**原生落库**
> (Mongo→BSON 文档 / MySQL→拆列写入),不是把 JSON 整包塞进 JSON 列。

**协议定义支持两种载体(与 §3.6 双形态对应),线格式统一为 JSON 语义:**

| 载体 | 定义方式 | 生成/实现 | 用于形态 |
|---|---|---|---|
| **A. 反射载体** | `MSTRUCT` + `MFUNCTION`(§3.1 消息,项目现状) | MHeaderTool 生成,`MDatabaseClient` 反射序列化 | mession 内 |
| **B. 独立 IDL 载体** | protobuf(建议)等 IDL 定义**等价消息** | 独立生成 server/client 骨架,不依赖反射工具链 | 独立 DBProxy / 其他语言 |

- 两种载体描述**同一协议语义**(FindOne / UpsertOne / DeleteOne + Collection/Key/JSON 值),
  线上表示都是 JSON 值 → 两种载体的实现可互操作、可互替换。
- 第一版实现载体 A(现状);载体 B 为剥离路径,不阻塞第一版。

```cpp
// Source/Protocol/Messages/DBService/FDbServiceMessages.h
MSTRUCT()
struct FDbFindOneRequest
{
    MPROPERTY()
    MString Collection;

    MPROPERTY()
    MString Key;
};

MSTRUCT()
struct FDbFindOneResponse
{
    MPROPERTY()
    bool bFound = false;

    MPROPERTY()
    MString Json;

    MPROPERTY()
    MString Error;
};

MSTRUCT()
struct FDbUpsertRequest
{
    MPROPERTY()
    MString Collection;

    MPROPERTY()
    MString Key;

    MPROPERTY()
    MString Json;
};

MSTRUCT()
struct FDbUpsertResponse
{
    MPROPERTY()
    bool bOk = false;

    MPROPERTY()
    MString Error;
};

MSTRUCT()
struct FDbDeleteRequest
{
    MPROPERTY()
    MString Collection;

    MPROPERTY()
    MString Key;
};

MSTRUCT()
struct FDbDeleteResponse
{
    MPROPERTY()
    bool bOk = false;

    MPROPERTY()
    MString Error;
};

// 建表预热(启动期,SDK 初始化阶段批量发;§3.4)
MSTRUCT()
struct FDbFieldDef
{
    MPROPERTY()
    MString Name;

    MPROPERTY()
    int32 Type = 0;        // EPropertyType 值(标量→列,复杂→JSON 子列)

    MPROPERTY()
    MString Meta;          // 可配:Storage=Binary 等
};

MSTRUCT()
struct FDbSyncTableRequest
{
    MPROPERTY()
    MString Collection;

    MPROPERTY()
    TVector<FDbFieldDef> Fields;
};

MSTRUCT()
struct FDbSyncTableResponse
{
    MPROPERTY()
    bool bOk = false;

    MPROPERTY()
    MString Error;
};
```

**Key 抽象(多主键)**:单键/复合键统一为 `FDbKey`:

```cpp
MSTRUCT()
struct FDbKey
{
    MPROPERTY()
    TVector<MString> Parts;    // 复合键各部分;单键 = 1 个元素
};
```

- 单键:`FDbKey{ Parts = {"player_1001"} }`;复合键(如 `(playerId, mailId)`):
  `FDbKey{ Parts = {"1001", "mail_5"} }`。
- backend:MySQL 复合主键列(SyncTables 的 `bKey` 标记多个主键字段);
  Mongo `_id` = Parts 拼接(如 `"1001#mail_5"`)。

**批量变体**(登录整包加载/登出批量落库,一次 RPC 多操作):

```cpp
MSTRUCT()
struct FDbBatchUpsertRequest
{
    MPROPERTY()
    TVector<FDbUpsertRequest> Items;   // 可跨集合
};
MSTRUCT()
struct FDbBatchUpsertResponse
{
    MPROPERTY()
    TVector<FDbUpsertResponse> Results;   // 与请求一一对应
};
// FindOne / Delete 批量变体同理(FDbBatchFindOne* / FDbBatchDelete*)
```

**SelectPartKey(复合主键部分列查询)**:主键为多字段时,按其中**部分列**(最左前缀)查多条——
非任意字段条件,仍走索引:

```cpp
MSTRUCT()
struct FDbKeyValue
{
    MPROPERTY()
    FDbKey Key;

    MPROPERTY()
    MString Json;
};

MSTRUCT()
struct FDbSelectPartKeyRequest
{
    MPROPERTY()
    MString Collection;

    MPROPERTY()
    FDbKey KeyPrefix;      // 部分列:只填主键的一部分(如主键(playerId,mailId),只给 playerId)

    MPROPERTY()
    int32 Limit = 0;       // 0 = 不限(建议设上限)
};

MSTRUCT()
struct FDbSelectPartKeyResponse
{
    MPROPERTY()
    TVector<FDbKeyValue> Items;

    MPROPERTY()
    MString Error;
};
```

- 语义:**部分列查询**——只给复合主键的前 N 列,查该前缀下的全部记录
  (如 `(playerId, mailId)` 只按 `playerId` 查他全部邮件)。
- backend:MySQL 复合索引**最左前缀**(`WHERE player_id = ?`);Mongo `_id` 为拼接串,
  部分列物理上落成前缀范围(`$gte prefix, $lt prefix+'\uffff'`)——协议语义是"部分列",
  两 backend 都走 Key 索引,非任意字段条件。

**事务(操作列表原子执行)**:扣金币+发道具这类跨集合原子操作:

```cpp
MSTRUCT()
struct FDbTransactionRequest
{
    MPROPERTY()
    TVector<FDbUpsertRequest> Upserts;

    MPROPERTY()
    TVector<FDbDeleteRequest> Deletes;
};
MSTRUCT()
struct FDbTransactionResponse
{
    MPROPERTY()
    bool bOk = false;

    MPROPERTY()
    MString Error;
};
```

- backend:MySQL 原生事务(InnoDB);Mongo 多文档事务需**副本集**(当前 standalone 环境不支持,
  单文档 Upsert 天然原子);standalone 下事务请求返回 unsupported 错误。

**严格新增(Insert)**:主键已存在 → 失败(创建账号/唯一记录不可覆盖,区别于 Upsert):

```cpp
MSTRUCT()
struct FDbInsertRequest
{
    MPROPERTY()
    MString Collection;

    MPROPERTY()
    FDbKey Key;

    MPROPERTY()
    MString Json;
};
MSTRUCT()
struct FDbInsertResponse
{
    MPROPERTY()
    bool bOk = false;

    MPROPERTY()
    bool bConflict = false;    // 主键已存在

    MPROPERTY()
    MString Error;
};
```

**字段自增(Increment)**:金币/计数免"读→改→写"的原子自增:

```cpp
MSTRUCT()
struct FDbIncrementRequest
{
    MPROPERTY()
    MString Collection;

    MPROPERTY()
    FDbKey Key;

    MPROPERTY()
    MString Field;     // 字段路径(如 "Gold" / "Stats.Hp")

    MPROPERTY()
    int64 Delta = 0;
};
MSTRUCT()
struct FDbIncrementResponse
{
    MPROPERTY()
    bool bOk = false;

    MPROPERTY()
    int64 NewValue = 0;

    MPROPERTY()
    MString Error;
};
```

- backend:Mongo `$inc`;MySQL `UPDATE ... SET col = col + delta`(行锁原子)。
- 字段路径:标量字段直接名;嵌套字段用点路径(JSON 子列内部,后置)。

**遍历(Traverse)**:游标式分批拉全集合(运营统计/导出/数据修复):

```cpp
MSTRUCT()
struct FDbTraverseRequest
{
    MPROPERTY()
    MString Collection;

    MPROPERTY()
    int32 Limit = 1000;     // 每批条数

    MPROPERTY()
    MString Cursor;         // 游标:空 = 从头;后续 = 上批返回的续点
};
MSTRUCT()
struct FDbTraverseResponse
{
    MPROPERTY()
    TVector<FDbKeyValue> Items;

    MPROPERTY()
    MString Cursor;         // 非空 = 还有更多;空 = 遍历结束

    MPROPERTY()
    bool bHasMore = false;

    MPROPERTY()
    MString Error;
};
```

- backend 游标 = 上批最后一个 Key(走索引跳页):Mongo `_id` 排序 + `$gt`;
  MySQL `pkey > ? ORDER BY pkey LIMIT n`(无 offset 全扫)。
- SDK 封装成迭代器/回调体验(`Traverse(Collection, [](const FDbKeyValue&){...})`)。

### 3.2 业务侧客户端封装(让业务"写得舒服")

**类型即表名**:`MSTRUCT(Meta=(DbTable))` 的 Schema 类型 = 一张表/集合,类型名即
Collection/Table 名(`TSchema::StaticClass()->GetName()`)——SDK 内部自动填协议
`Collection`,业务零字符串、不会拼错;改名即改表名(配合 §3.5 改名走 Deprecated 铁律)。

```cpp
// Source/Common/DB/MDatabaseClient.h —— 业务侧 SDK(反射↔JSON + RPC + async)
class MDatabaseClient
{
public:
    static MDatabaseClient& Get();

    // ---- 初始化(启动期一次:连接检查 → 建表同步 → 版本校验)----
    SFutureResult<bool> SyncTables();   // 扫 Meta=(DbTable) 类型,建缺失表/加列/版本校验(幂等)

    // ---- 单条 CRUD(模板参数 TSchema = 表类型,Collection 自动)----
    template<typename TSchema>
    SFutureResult<TOptional<TSchema>> FindOne(const FDbKey& Key);

    template<typename TSchema>
    SFutureResult<bool> Insert(const FDbKey& Key, const TSchema& Value);   // 严格新增,bConflict 区分

    template<typename TSchema>
    SFutureResult<bool> UpsertOne(const FDbKey& Key, const TSchema& Value);

    template<typename TSchema>
    SFutureResult<bool> Delete(const FDbKey& Key);

    template<typename TSchema>
    SFutureResult<bool> Increment(const FDbKey& Key, const MString& Field, int64 Delta, int64* OutNewValue = nullptr);

    // ---- 批量 ----
    template<typename TSchema>
    SFutureResult<TVector<TOptional<TSchema>>> SelectMany(const TVector<FDbKey>& Keys);
    template<typename TSchema>
    SFutureResult<bool> UpsertMany(const TVector<TPair<FDbKey, TSchema>>& Items);
    // InsertMany / DeleteMany 同理

    // ---- 部分列查询(复合主键最左前缀)----
    template<typename TSchema>
    SFutureResult<TVector<TPair<FDbKey, TSchema>>> SelectPartKey(const FDbKey& KeyPrefix, int32 Limit = 0);

    // ---- 遍历(游标分批,回调)----
    template<typename TSchema>
    SFutureResult<bool> Traverse(TFunction<void(const FDbKey&, const TSchema&)> OnItem);

    // ---- 事务(收集式)----
    SFutureResult<bool> Transaction(TFunction<void(MDbTransaction&)> InBody);

private:
    template<typename TReq, typename TResp>
    SFutureResult<TResp> CallDb(const MString& Method, const TReq& Req);   // 寻址(Registry/直连)+ RPC
};
```

- Schema 序列化复用反射↔JSON 桥:`ExportStructToJsonValue` / `ImportStructFromJsonValue`
  (ReflectionPropertyTemplates.inl)+ `MJsonWriter`/`MJsonReader`(Json.h),业务零手写;
  值一律以 **JSON 字符串**在业务与 DBService 间传递。
- 业务寻址 DBService:**两种形态**(见 §3.6):
  - mession 内:注册 Registry,业务经 `MEndpointCache::GetOrConnect(EServerType::Db)` 发现;
  - 独立部署:配置 `host:port` 直连。
  SDK(`MDatabaseClient`)对两种形态同一接口,寻址策略由配置决定。
- 返回风格:不存在 = `TOptional` nullopt;错误走 `SFutureResult` 的 `FAppError`(async 模型)。

### 3.3 DBService 服务端(桥梁服务,NetServerBase 体系)

**定位(已决策 DB1)**:DBService 本质是 **`OtherService <-> DBService <-> DB` 的桥梁服务**
——即使剥离 mession 架构,它仍是 `MNetServerBase` 体系的服务:一边 OnAccept 收
OtherService/SDK 连接(MServerConnection,server RPC),一边桥接 backend;协议转换
(`FDb*` 消息 ↔ backend)是全部职责,零业务逻辑。

- 骨架:`MCLASS(Type=Service) class MDBService : public MNetServerBase, public MObject`
  (仿 MEchoService;`EServerType::Db = 9`,形态 A 注册用)。
- **双形态(§3.6)共享同一骨架,差别只有一处**:`--registry=<addr>` 非空 →
  `BindRegistry + RegisterLocal`(形态 A);为空 → 纯监听直连(形态 B)。监听/事件循环/
  连接/协议/backend 全共享——两形态都是"服务"。
- 配置(`SDBServiceConfig`,仿 SEchoServiceConfig):`--listen` / `--registry`(可空=形态 B)/
  `--db-driver=mongo\|mysql` / `--db-conn=<连接串>` / `--db-name=<库名/集合前缀>`。
- 内部:`MMongoDatabaseClient` / `MMysqlDatabaseClient` 适配(3.4)。
- Handler:`MFUNCTION(ServerCall)` `FindOne/UpsertOne/DeleteOne` → backend 调用 → 回包。

**调用模型(已决策:worker 池 + backend 纯同步阻塞)**:
- Handler(事件循环线程)收到请求 → Post 到 worker 池 → worker 执行阻塞 backend 调用
  → 结果回投事件循环 → 回包;事件循环零阻塞。
- backend 实现为**纯同步**(不内部管线程);worker 池集中在 Handler 层一处管理。

**分桶与顺序(已决策:属主 ID 统一分桶)**:
- **workerId = hash(属主 ID) % N**——同一属主永远同一 worker → 同属主操作串行有序。
- 属主 ID = `Meta=(ShardKey)` 显式标记的字段(可不在主键首列;跨集合同属主同桶,
  玩家 Profile/背包/邮件 全数据串行一致)。
- 跨集合 B 依赖 A 的结果:同属主 → 同桶顺序保证;**强原子(要么全成要么全败)→ 事务**
  (不靠分桶)。
- 边界:同属主高频写受单桶串行限(游戏单玩家写频率低,可接受);跨玩家天然并行。

**SyncTable 处理(启动期)**:`FDbSyncTable` → backend 建表 + `schema_versions` 版本校验
(§3.5:加列自动 ALTER、删列拒启)。

### 3.4 backend 适配(双 DB,各自用足特性)

**原则:同一 Schema,不同 backend 用自己擅长的方式落库——不是一种格式硬套。**

| backend | 落库方式 | 特性利用 |
|---|---|---|
| Mongo | collection,Schema → **BSON 文档**(字段级映射) | 文档天然、嵌套无阻、字段级查询 |
| MySQL | 表,Schema → **关系化列**(每个字段一列) | SQL 类型/索引/查询/关联/约束 |

**MySQL 关系化映射**(用足关系特性,不用 JSON 列整包):

- 表名 = Schema 名(如 `FPlayerProfile` → `player_profiles`),Key → 主键列 `pkey`。
- 标量字段 → 类型映射列:`EPropertyType` 反射元数据驱动
  (`MProperty::Name/Type`,Property.h):

  | EPropertyType | MySQL 列 |
  |---|---|
  | Int8/16/32、UInt8/16/32 | `INT` / `BIGINT`(按宽度) |
  | Int64 / UInt64 | `BIGINT` / `BIGINT UNSIGNED` |
  | Float / Double | `FLOAT` / `DOUBLE` |
  | Bool | `BOOLEAN` |
  | String / Name | `VARCHAR(255)`(可配 `TEXT`) |

- 复杂字段(嵌套 Struct / Vector / Map)→ **嵌套子列**(承载嵌套,保数据完整;
  Mongo 存嵌套 BSON 文档,两 backend 数据等价)。
  - **落库格式可配**(按字段,`MPROPERTY(Meta=(Storage=Binary))`):
    - 默认 **JSON 子列**——可读可查(`JSON_EXTRACT`)、DBA 可调试;PoC 默认。
    - 可选 **二进制快照**(`MReflectArchive`,紧凑、体积约为 JSON 一半,类似 TcaplusDB
      的 TDR 二进制)——嵌套字段体积/性能敏感时启用(如大背包),内容不可读。
    - 两种格式都保持"整体不透明"(服务端不解析嵌套内部),架构决策(不拆子表、
      整行 upsert)不随格式变化。
- 嵌套字段**不拆子表**:游戏数据是聚合根(按玩家 Key 点查整包),无跨实体 JOIN 需求,
  子表带来级联/一致性成本而收益为零。
- 内部字段高频查询需求出现时 → 该字段配"生成列白名单",`ALTER TABLE ... ADD GENERATED
  COLUMN AS (JSON_EXTRACT(...))` + 索引(后置,不在第一版)。
- 建表:**启动期预热,运行期零 DDL(已决策)**——业务服务启动时,SDK 把反射生成的
  字段名+类型清单批量发 `FDbSyncTable`(collection + 字段数组)给 DBService;
  DBService 幂等 `CREATE TABLE IF NOT EXISTS` + `schema_versions` 校验(§3.5:加字段
  自动 ALTER、删字段拒绝启动);运行期 FindOne/UpsertOne 纯数据路径,不现场建表(避免
  首次访问 DDL 延迟)。Mongo 集合隐式(首次 insert 自动创建),无需建表。
- 第一版写入:整行 `INSERT ... ON DUPLICATE KEY UPDATE`(字段级增量后置)。

**Mongo 映射(已决策:字段级,类型保真)**:Schema 字段 → BSON 字段(`_id` = FDbKey 拼接),嵌套天然。

- 写入:反射遍历 `MClass::GetProperties()` → 按 `EPropertyType` 转 bsoncxx 值
  (`Int64`→`b_int64`、`String`→`b_string`、嵌套 Struct/Vector→子文档),**字段级**,
  非整文档 `from_json`——类型保真(int64/二进制不丢)、字段级查询可用、嵌套天然。
- 读取:BSON 文档 → 按反射字段解析(或转 JSON 回 SDK),与写入对称。
- 反射映射器与 MySQL 拆列同思路(一个反射驱动,两 backend 各自落法)。

> 否决记录:曾考虑 MySQL `BLOB`(黑盒,不可查/不可读/不可迁移)与 JSON 列整包
> (丢 MySQL 关系特性)——均已否决。正确形态是"Schema 抽象 + 每 backend 原生落库"。

**实现状态**:
- `MMongoDatabaseClient`:`libmongocxx`(环境已装,mongod 运行中),立即实现。
- `MMysqlDatabaseClient`:本轮写代码,`#ifdef MESSION_ENABLE_MYSQL` 守护(当前无驱动默认不编译)。

### 3.5 Schema 版本化与字段生命周期(铁律)

**铁律:字段一经发布,永不删除、永不改名。变更只允许"加字段"。**

| 变更 | 策略 | 机制 |
|---|---|---|
| **加字段** | 允许,自动兼容 | 连接时 SyncTables 对比快照 → 自动 `ALTER TABLE ADD COLUMN`;Mongo 天然;读旧数据缺字段 → 默认值 |
| **删字段** | **禁止** | 字段集变小 → 拒连(版本只增不减的守护);删除会让旧数据反序列化失败 |
| **改名** | 禁止 | 改名 = 删+加,违反铁律;确需改名 → 保留旧字段 + 新增字段 + 一次性迁移脚本拷贝 |
| **改类型** | 不自动做 | 人工迁移;自动 DDL 改类型风险高,禁止 |
| 开发期 | `--db-reset` | DROP 重建表/集合(仅开发/测试) |

**Deprecated(纯业务语义,不进版本机制)**:`MPROPERTY(Meta=(Deprecated))` 标记字段
**不影响表结构、字段集不变、版本不变**——只影响写入端:**写入跳过该字段**(不落新值);
读取端照常(旧数据保留历史值,新数据读默认值)。业务在标记时即完成迁移(不再依赖)。

**版本化机制(第一版即做;SyncTables 在连接时触发)**:
- **SyncTables 时机(已决策)**:SDK 连接到 DBService 时自动触发(连接建立流程内,业务
  零调用)——DBService 校验/建表/版本更新后返回就绪,业务才开始请求;幂等(无变更空转)。
- **DBService 维护持久化版本(已决策,Service 级单版本,单调递增)**——`schema_versions`
  (全局一张):`Version, FieldSnapshot, AppliedAt, ChangeNote`。
  - **整个 DBService 一个 Version**:任一 Schema 字段集变化(只增)→ 全局 +1;
    代码侧不写版本号(DBService 按快照对比自动累计)。
  - FieldSnapshot = **全部 Schema 的字段集**(所有表字段清单 JSON)。
- SyncTables 校验流程(全量快照对比):
  - 无记录 → 建表,Version=1,快照=当前全量字段集;
  - 一致 → 幂等空转;
  - **只增字段 → 自动 ALTER + Version+1 + 快照更新 + ChangeNote**;
  - **当前字段集 ⊄ 快照(字段被删/降级,如旧代码连新库)→ 拒绝连接**。
- 反序列化仍启用 `bIgnoreUnknown` 容错(防御降级/脏数据),但设计上不应出现未知字段。

**Mongo 侧**:文档天然无 schema,加字段零成本;废弃字段同样保留在 Schema 定义中;
`$unset` 迁移脚本仅用于清理 Deprecated 字段的历史值(可选,不影响读取)。

### 3.6 可剥离性:双形态(服务发现 / 独立 DBProxy)

**定位(2026-08 修订:可剥离性的本质 = 提供 SDK)**:DBService 的对外接口是一套 **SDK**——
其他服务(无论 mession 内还是外部系统)接 SDK 即可与 DBService 通信、操作数据库;
寻址/协议/序列化/连接全封装在 SDK 内,DBService 怎么部署、换不换实现,业务侧无感知。

```
业务服务 ──SDK(MDatabaseClient)──► DBService ──► DB
   └─ 寻址:Registry 发现 / 直连地址(配置)   └─ 实现可替换(换语言/换部署),SDK 不变
```

| 形态 | 接入方式 | 适用 |
|---|---|---|
| **A. mession 内** | 注册 Registry,业务经 SDK 内 `MEndpointCache::GetOrConnect(EServerType::Db)` 发现(第一版/默认) | 统一服务发现、多实例、滚动发布 |
| **B. 独立剥离** | 独立 CMake target 编译部署,不注册 Registry;SDK 配置 `host:port` 直连 | 脱离 mession 生态、其他语言/系统接入、DBProxy 化 |

**两种形态共享的接口设计**(这是可剥离性的根本保证):
- **SDK(mession 服务接入)**:`MDatabaseClient`(§3.2 全量接口:`FindOne<T>`/`SelectMany<T>`/
  `SelectPartKey<T>`/`UpsertOne<T>`/`Insert<T>`/`Delete<T>`/`Increment<T>`/`Traverse<T>`/
  `Transaction`/`SyncTables`;类型即表名 + 反射↔JSON + async + 连接管理);**寻址差异在
  SDK 内部**(配置:Registry 发现 or 直连),业务代码同一接口;外部系统按协议自实现客户端
  (§3.1 双载体 IDL)。
- **协议语义**:轻量、Schema 无关——完整操作集见 §3.1(单条 CRUD / 批量 / 部分列查询 /
  遍历 / 事务 / 建表同步;`Collection`+`FDbKey`+JSON 值),不依赖 mession 反射 RPC 链 →
  独立部署时无需 mession 服务栈。
- **协议载体双轨**(§3.1):形态 A 用反射载体(MSTRUCT + MHeaderTool),
  形态 B 用独立 IDL 载体(protobuf 等)生成独立代码——同一语义、同一 JSON 线格式。
- **Schema 感知不下沉**:序列化在 SDK 侧;建表清单(字段名+类型)随 SyncTables 请求下发,
  DBService 执行 DDL——DBService 自身无 Schema 编译期依赖,两种形态皆然。
- **建表清单的两个载体(已决策:IDL 含 Schema 元数据 + 类型映射器)**:
  - 反射载体:MSTRUCT → 反射元数据(`MProperty::Type`)→ `FDbFieldDef`;
  - IDL 载体:.proto → 生成器同时产出 **Schema 元数据**(字段名+protobuf 类型+标记)
    → **IDL→FDbFieldDef 类型映射器**(protobuf→EPropertyType:uint64→Int64、string→String、
    嵌套 message→Struct、repeated→Vector 等)→ `FDbFieldDef`;
  - 两载体汇聚到同一 `FDbFieldDef`,线格式一致,可互操作;转换在 SDK/生成器侧,
    DBService 只收已归一的 `FDbFieldDef`。
- 形态 A 的 Registry/EndpointCache 只是**接入层选择**,不影响协议与 Schema 边界;
  形态 B 剥离时替换接入层,业务侧无感知。
- **传输层抽象(已决策:复用 Socket 库,抽象为基础库)**:
  ```
  MDatabaseClient
    └─ ITransport(抽象:发请求/收响应)
         ├─ 形态 A: RegistryRpcTransport —— MEndpointCache + MServerConnection(server RPC)
         └─ 形态 B: DirectSocketTransport —— Socket 库(连接/收发)+ 自定义帧(长度前缀 + FDb 消息 JSON)
  ```
  - mession `Common/IO/Socket/`(SocketPlatform/Socket)是**可移植 OS socket 封装**,
    抽象为**独立可编译的基础库**(CMake 目标独立于 mession 业务栈)——形态 B 的 SDK
    编译 = SDK 核心(协议/序列化/类型映射)+ Socket 库 + DirectSocketTransport,不拉
    mession_common;剥离不重造 socket。
  - 传输由配置选择(A: Registry 发现 / B: 直连地址),业务代码同一接口,无感知。

## 4. 玩家模块

> 可实施规格(代码骨架 + 时序 + 文件布局):`2026-08-14-player-design.md`。

### 4.1 玩家定位与 ID 规则(2026-08 修订:PlayerId 稳定 + ActorId 分段)

**PlayerId:稳定身份(DB 主键,永不变)**:
- PlayerId = 玩家唯一身份,落 DB(`_id`/pkey),**注册时定、永久稳定**;
  不编码运行时实例(实例变更/崩溃迁移不改变 PlayerId → DB 主键稳定)。
- 生成规则:账号 hash(确定性,同账号同 ID)或注册分配;PoC 用账号 hash 确定性。
- **PlayerId → Service/实例**:靠 **Registry actor 元数据查**(`FServiceEndpoint.ActorIds`,
  §1.1 实例亲和性),不靠 ID 解析。

**ActorId 分段(双布局,各系统独立空间不冲突)**:

| Actor 类型 | ActorId 布局 | 说明 |
|---|---|---|
| **玩家** | `[Kind:16][PlayerId:48]` | 玩家占 48 位大空间(承载 2^48≈281 万亿),无 Service 段 |
| **其他**(房间/怪物/全局) | `[Kind:16][Service:16][Domain:32]` | 显式带 Service 段 + 域内 Domain(32 位够) |

```cpp
enum class EActorKind : uint16 { Player = 1, Global = 2, Room = 3, Monster = 4, ... };
// 玩家: ActorId = (Player<<48) | PlayerId
// 其他: ActorId = (Kind<<48) | (Service<<32) | Domain
```

- 解析:按 Kind 分流——玩家走 PlayerId 布局,其他走 Service+Domain;各 Kind 空间独立,
  PlayerId/全局固定段(9001 等)/房间/怪物互不冲突。
- **玩家路由**:按 PlayerId 查 Registry ActorIds → 实例(§1.1);非玩家 actor 从 ID 解析
  Service 段直接定位。
- 迁移/重连的实例变更:后置(登录态固定实例;重连仍回原实例,见 §4.6)。

### 4.2 消息与表类型(协议/落库分离)

**分离规则**:协议消息(不落 DB)与表类型(落 DB)分文件;表类型 = `Meta=(DbTable)` +
`DbKey`(主键)/ `ShardKey`(分桶列)标记(§3.2/§3.3),表名即类型名。

```
Source/Protocol/Messages/Player/
  FPlayerMessages.h   ← 协议消息(Login/Logout/业务请求响应,不落 DB)
  FPlayerEntity.h     ← 表类型(落 DB:Profile + 成员数据,全部 Meta=(DbTable))
```

```cpp
// FPlayerMessages.h —— 协议消息(普通 MSTRUCT,不落 DB)
// 登录消息抽象:支持多鉴权方式(密码 / 动态 Token / 二维码),不绑死单一密码
MSTRUCT()
enum class EAuthMethod : uint8
{
    Password = 0,   // 账号+密码
    Token    = 1,   // 动态会话票据
    QrCode   = 2,   // 二维码扫码确认
};

MSTRUCT()
struct FLoginCredential
{
    MPROPERTY()
    MString Password;    // Method=Password

    MPROPERTY()
    MString Token;       // Method=Token

    MPROPERTY()
    MString QrCodeId;    // Method=QrCode
};

MSTRUCT()
struct FLoginRequest
{
    MPROPERTY()
    EAuthMethod Method = EAuthMethod::Password;

    MPROPERTY()
    MString AccountName;

    MPROPERTY()
    FLoginCredential Credential;
};

MSTRUCT()
struct FLoginResponse
{
    MPROPERTY()
    uint64 PlayerActorId;  // 即 PlayerId(§4.1 稳定 ID)

    MPROPERTY()
    FPlayerProfile Profile;

    MPROPERTY()
    MString SessionToken;  // 登录成功签发;后续请求携带(动态/可过期)

    MPROPERTY()
    MString ErrorMessage;  // 空=成功
};
```

```cpp
// FPlayerEntity.h —— 表类型(落 DB,类型即表名)
MSTRUCT(Meta = (DbTable))
struct FPlayerProfile
{
    MPROPERTY(Meta = (DbKey, ShardKey))   // 主键 + 分桶列 = PlayerId(属主=自己)
    uint64 PlayerId;

    MPROPERTY()
    MString AccountName;

    MPROPERTY()
    MString PasswordHash;

    MPROPERTY()
    MString DisplayName;

    MPROPERTY()
    int64 Level = 0;

    MPROPERTY()
    int64 Exp = 0;

    MPROPERTY()
    int64 CreatedAtMs = 0;

    MPROPERTY()
    int64 UpdatedAtMs = 0;
};

MSTRUCT(Meta = (DbTable))
struct FPlayerView       // 客户端可见状态(Replicated 域)
{
    MPROPERTY(Meta = (DbKey))
    uint64 PlayerId;

    MPROPERTY()
    MString DisplayName;

    MPROPERTY()
    int64 Level = 0;

    MPROPERTY()
    float PosX = 0.f;

    MPROPERTY()
    float PosY = 0.f;
};
```

- 落库走 SDK:登录 `FindOne<FPlayerProfile>(FDbKey{{PlayerId}})` / 建号 `UpsertOne<T>`;
  `SyncTables()` 自动建表(连接时,§3.5)。
- 成员数据 Schema(背包/任务等)同为 `Meta=(DbTable)` 表类型,`ShardKey` = 属主 PlayerId
  (§4.3,同玩家跨表同桶串行)。

**鉴权策略(可插拔,不绑死密码)**:
- `MLoginAuth` 按 `FLoginRequest.Method` 分派到独立认证器:`PasswordAuth` / `TokenAuth` /
  `QrAuth`(策略可插拔,新增鉴权方式 = 加一个认证器,不改登录主流程)。
- **会话票据**:登录成功签发 `SessionToken`(动态、可过期),后续请求携带 token 校验——
  密码/二维码只是"首次认证"入口,长期会话走 token(降低密码 hash 的暴露面)。
- **密码存储**:PoC 用 **PBKDF2(随机盐 + 慢哈希)**,正式用 Argon2/BCrypt;
  裸 hash / 加盐 SHA 不够(可高速爆破);明文仅 PoC 占位注释。

### 4.3 玩家 Actor 组织(目标架构:ActorMember,协议下沉到成员)

**三层:LoginService(认证,MLoginAuth 成员) + PlayerService(零业务协议,actor 容器) + ActorMember(业务)**。
代码骨架见 `2026-08-14-player-design.md`(§2/§3),框架支撑见 `2026-08-14-actor-member-framework.md`。

**要点**:
- **协议分散粒度 = ActorMember 类**:每个业务模块一个 `MCLASS(Type=ActorMember)` 类,
  自带 `MFUNCTION(ServerCall)`;Service 类零业务协议(与用户理想一致)。
- **寻址(已决策)**:请求显式带 PlayerId(`FPlayerRequestBase{ PlayerId }`),
  路由 = PlayerId 定位 actor + FunctionId 定位 member 方法;不依赖 Gateway 注入。
- Gateway 经 MClientManifest 路由(`OwnerType=member 类`, `TargetName=所属服务`,
  SEntry.TargetName 已有)→ 机制与现有 Echo 转发一致。
- 跨实例 actor 直寻址(`MRpcChannel::CallToActor(PlayerId)`)依赖 Active gap
  "Prove cross-Echo CallToActor" → 后置(PoC 单实例)。

**成员数据(表类型,对齐 DBService §4.2/§3.2)**:成员数据 = `FPlayerEntity.h` 里的表类型
(`Meta=(DbTable)` + `ShardKey=PlayerId` 属主分桶,§3.3 同玩家跨表同桶串行):

```cpp
// FPlayerEntity.h(与 Profile 同文件,表类型)
MSTRUCT(Meta = (DbTable))
struct FPlayerInventory
{
    MPROPERTY(Meta = (DbKey, ShardKey))   // 主键 + 分桶列 = PlayerId(属主)
    uint64 PlayerId;

    MPROPERTY() TVector<FItem> Items;
    ...
};

MSTRUCT(Meta = (DbTable))
struct FPlayerQuest
{
    MPROPERTY(Meta = (DbKey, ShardKey))   // 主键 + 分桶列 = PlayerId
    uint64 PlayerId;

    MPROPERTY() TMap<uint64, FQuestState> Quests;
    ...
};
```

- **Load/Save 走 SDK**:登录 `FindOne<FPlayerInventory>(FDbKey{{PlayerId}})`、登出
  `UpsertOne<T>`;标脏后落库(§4.6 数据生命周期)。
- 表名即类型名(§3.2),`SyncTables()` 自动建表;成员方法内直接 `TAwaitable<...>` 或
  SDK 调用,不接触 DB 细节。

**文件布局**(详见 player-design.md §7):
```
Source/Servers/Player/
  PlayerService.h/cpp      零业务协议:进程骨架 + actor 集合
  MPlayerActor.h/cpp       每玩家 actor(容器:状态机/数据生命周期/成员挂载)
  Members/
    MPlayerStats.h/cpp           (Type=ActorMember)基础属性成员
    MPlayerItemContainer.h/cpp   (Type=ActorMember)背包成员
    MPlayerQuest.h/cpp           (Type=ActorMember)任务成员
```

**进程内两种 actor 形态(服务不拆,进程内按 actor 形态组织)**:

| 形态 | 实例数 | ActorId | 承载业务 |
|---|---|---|---|
| Per-Player actor | 在线玩家数 | PlayerId(§4.1) | 玩家私有:背包/好友/任务/属性 |
| Global actor(仿 `MRankListActor` 先例) | 每功能 1 个 | **`MakeGlobalActorId(Service, Domain)`**(§4.1 其他 actor 布局:`Kind:16|Service:16|Domain:32`,如排行榜 `Global|Player|1`) | 跨玩家:排行榜/邮件/全服广播 |

- 判断:跨玩家 → Global actor;**不**因此拆服务(§2.2);出现拆的信号才拆。
- **Global actor 定位**:ID 自带 Service 段(`Kind:16|Service:16|Domain:32`,§4.1)→ 路由直接
  从 ID 解析服务进程,不依赖 Registry 查(与玩家 actor 不同);`MRankListActor.RANK_LIST_ACTOR_ID`
  等裸固定 ID 需按 `MakeGlobalActorId` 规则迁移。

**成员间通信(同 actor 同线程,零锁)**——成员是 actor 内组件,actor 单线程模型,
不走事件总线(Event 目录已删,无消费者):

1. **`GetActorMember<T>()` 查找 + 直接调用**:读数据、基础能力,依赖方向避免环
   (如 Login → ItemContainer → Stats)。
2. **`TAwaitable` 表达式 await 成员方法**(`int R = TAwaitable<&Member::Method>(Member, args...)`;
   F0 演进项:TAwaitableFnTraits 需支持成员函数指针);
3. **调用方成员编排**:跨成员业务流由调用方成员协调(如 Login 成员调背包
   LoadAllItem),不经 actor 中转。

**消息流(目标架构)**:
```
登录:客户端 → Gateway(按 FunctionId+Target 路由)→ LoginService 进程(框架分发 → MLoginAuth 成员)→ Gateway 绑定
  → server RPC → MPlayerService.EnterGame(PlayerId) → 创建 actor(成员自动挂载) → 回包/下行
业务:客户端 → Gateway(按 FunctionId+Target 路由)→ MPlayerService 进程
  → 框架 member 分发器 → Find(PlayerId) → actor → member 反射调用 → 回包
下行:成员变更 → actor 判定需同步 → Gateway 绑定表 → CallClient
```

**跨实例(后置)**:PlayerId 稳定(§4.1,不编码实例),登录态固定实例、重连回原实例;
PoC 单实例验证,跨实例 CallToActor 待 gap 解决。

- 上行业务消息经框架 member 分发器到成员;下行经 Gateway(§4.5)。
- 在线状态机见 §4.6。

### 4.4 登录流程(async 全链路,LoginService 跨进程)

```
客户端 ──LoginRequest(Account+Password)──► Gateway(按 FunctionId+Target)──► LoginService 进程
  → 框架 member 分发器 → MLoginAuth 成员(认证,§4.3/player-design §2.1):
      0. **重复登录检查**:本账号已有在线 actor → 踢旧(§4.6):经 ServiceDiscovery
         通知旧 Player 实例 Kick + 落库注销,新登录继续
      1. 选实例(账号 hash 稳定映射;PlayerId 稳定不编码实例,§4.1)
      2. PlayerId = 账号 hash(§4.1);AWAIT SDK.FindOne<FPlayerProfile>(FDbKey{{PlayerId}})
      3. 不存在 → 创建(密码 hash)→ SDK.UpsertOne<FPlayerProfile>(Key, Profile);
         存在 → 校验密码
      4. 回包 LoginResponse{PlayerId, 目标 PlayerService 实例}
Gateway:绑定 connId ↔ playerId + 记录 playerId→实例(仅路由)
  → [会话激活] LoginService 经 ServiceDiscovery → CallRemote(Player 实例, "EnterGame")
  → MPlayerService:创建 MPlayerActor(成员自动挂载)+ Register + OnLogin(查 Profile/成员 Load)
  → ① 响应回包 ② CallRemote(Gateway, "PushClientDownlink", 欢迎+View)   ← 下行闭环(§6.1)
```

- SDK 调用(`MDatabaseClient`):连接时 `SyncTables()` 已建表(§3.5);登录 `FindOne<T>` /
  建号 `UpsertOne<T>` 类型即表名,无 Collection 字符串。
- 登出/断连:Profile 回写 DB(SDK `UpsertOne<T>`)→ 注销 actor(经 LoginService 走 ServiceDiscovery,§1.1)。
- 落库时机:登录加载、关键变更、登出;定期快照(TODO 后置)。

### 4.5 Gateway 会话绑定与下行(收束闭环)

- Gateway 已有未提交代码:`MClientTargetResolver::RegisterConn/UnregisterConn` +
  `MClientTargetContextGuard`(连接按 `GetPlayerId()` 键控 = **PlayerId→conn 持久注册表**)。
- **响应收束闭环(§6.1)**:`return Response` 不是终点——业务侧下行需求经
  `CallRemote(Gateway, "PushClientDownlink", {PlayerId, FunctionId, Payload})`
  (ServerCall,非客户端协议)→ Gateway 经 Resolver **按 PlayerId 查连接** → 发下行;
  响应与下行为两条消息。Guard/当前绑定仅覆盖同步处理期,跨进程/异步下行依赖持久注册表。
- 下行 FunctionId 与 `MGET_STABLE_RPC_FUNCTION_ID("MClientDownlink", ...)` 一致
  (NetBench.cpp 先例)。

### 4.6 在线状态机与生命周期

```
Offline ──登录──► LoggingIn ──成功──► Online ──断线──► Reconnecting(宽限期 60s)
  ▲                                                │
  └──────── 宽限期超时:落库 + 注销 actor ◄──────────┘
```

- **LoggingIn**:登录流程中(查库/建号/校验),重复 Login 请求去重(幂等)。
- **Online**:可收业务消息;PersistentData 字段变更标脏。
- **断线**:Gateway 检测 conn 断开 → 解绑(PlayerId↔ConnId)→ 通知 actor → 进 Reconnecting。
- **Reconnecting(宽限期 60s)**:期内重连**回原实例**(§4.1,登录态固定实例),
  复用内存态、免重载;重连成功 → Online 并换绑新 ConnId。
- **宽限期超时**:落库 → 注销 actor → Offline。
- **重复登录(踢旧)**:同账号新登录 → 旧连接发 Kick 后断开,旧 actor 落库注销,
  新登录继续(登录流程步骤 0)。
- **登出/踢下线**:Profile/成员数据回写 DB(SDK `UpsertOne<T>`)**同步成功后**才注销
  actor(防丢档)。

**数据生命周期(登出落库 + 标脏,走 SDK)**:
- 登录:全量加载 Profile + 成员数据(SDK `SelectMany<T>` 或并行 `FindOne<T>`)→ 内存态。
- 运行:PersistentData 字段变更标脏,不即时落库。
- 落库:登出 / 踢下线 / 宽限期超时(**SDK `UpsertOne<T>` 同步成功后才注销**,防丢档);
  定期快照(TODO 后置)。
- **与 DBService 衔接**:同玩家多表(Profile/背包/任务)落库经属主分桶(§3.3)同桶串行,
  顺序一致;标脏粒度 = 表级(整行 Upsert,§3.0),字段级后置。

## 5. 里程碑

> 实现顺序以 `2026-08-14-player-design.md` §8 为准(F0 框架 → F1 验证成员 → P1+ 业务)。
> 此处保留与 DBService 对应的里程碑:

| 阶段 | 内容 | 验证 |
|---|---|---|
| M1 | DBService 进程骨架 + `MMongoDatabaseClient` + Db 协议 Handler | `DBServiceTest`:Schema round-trip(经 DBService RPC) |
| M4 | `MMysqlDatabaseClient`(MESSION_ENABLE_MYSQL 守护) | 环境就绪后冒烟 |

(业务侧登录/下行里程碑见 player-design §8 的 F1/P1/P3;Login 归属 LoginService 而非旧式 `MPlayerActor::Login`——§2.2/§4.3。)

## 6. 尚未讨论完整的板块(TODO 后续轮次)

- [x] ~~玩家生命周期全态:断线检测、重连(回原实例)、踢下线、登出清理~~ → §4.6 已定稿(宽限期 60s、踢旧、登出落库+标脏)
- [x] ~~数据一致性:重复登录(踢下线)、落库时机~~ → §4.6 已定稿;并发写(乐观版本/版本号)仍 TODO
- [x] ~~Player 组织与模块通信~~ → §4.3 目标架构(ActorMember)+ player-design 规格
- [ ] **实例亲和性基建**:`MEndpointCache::FindEndpointByActorId(PlayerId)` + actor 上报链路(MActorSystem Register → RegistryProtocol 更新 `FServiceEndpoint.ActorIds`)= Active gap "Registry actor metadata"(player-design §1.1/1.2)
- [ ] **跨实例 CallToActor**(好友/组队/Global actor 协同,依赖上项)
- [x] ~~下行目标解析:业务实例 → 玩家 connId 的查询/推送链~~ → §6.1 响应收束闭环:业务侧经 Gateway `PushClientDownlink` + Resolver 按 PlayerId 查持久注册表
- [x] ~~鉴权演进:token/会话票据~~ → §4.2 已定:多鉴权方式(Method+Credential)+ SessionToken + 密码 PBKDF2/Argon2;仍 TODO:QrAuth 具体流程、token 有效期/续期细节
- [ ] EServerType::Db/Player/Login 接入 Registry + servers.py 拓扑(Registry→Login→Echo×N→Player×N→Db→Gateway)
- [ ] 定期快照落库(宕机恢复窗口内丢档的兜底)
- [ ] 并发写:乐观版本/版本号

## 7. 前置阻塞

1. **构建恢复绿色**:`MActorHandle.cpp` 编译错误(未提交 multi-reactor WIP)阻塞全量构建。
2. **validate.py 基线**:先跑通现有 3 链路再扩展登录链路。

## 8. 已确认决策

- [x] DB 形态:独立 DBService 进程,可独立编译部署(双形态:Registry 发现 / 配置直连,§3.6)
- [x] ORM:轻量持久化 ORM(§3.0),第一版整行 Upsert
- [x] Schema:业务定义反射 MSTRUCT;值以 JSON 传输,backend 原生落库(Mongo 文档 / MySQL 拆列,§3.4)
- [x] 版本化铁律:字段只增不删、不改名;废弃用 Deprecated;启动校验拒绝删字段(§3.5)
- [x] 嵌套落库:格式可配(JSON 默认 / 二进制按字段,`Meta=(Storage=Binary)`);不拆子表(§3.4)
- [x] 玩家定位:PlayerId 稳定(§4.1 修订,不编码实例),ActorId 分段;实例映射经 Registry 查
- [x] 服务拆分准则:拆的信号/不拆的信号/默认聚合(§2.2)
- [x] 玩家组织:LoginService(认证,信任边界)+ MPlayerService(零业务协议)+ ActorMember(业务,§4.3)
- [x] LoginService 同构:零业务协议 + MLoginAuth 成员挂服务实例(无 actor 容器)
- [x] 生命周期:宽限期 60s、重复登录踢旧、登出落库+标脏(§4.6)
- [x] 协议分散:每协议一个 MFUNCTION(ServerCall);不做统一信封(已否决)
- [x] 成员寻址:请求显式带 PlayerId(`FPlayerRequestBase`)
- [x] 落地路径:先框架后业务(F0 框架 → F1 验证成员 → P1+ 业务)
- [x] 会话编排:走 ServiceDiscovery 层(MEndpointCache + CallRemote),不经 Gateway 中转
- [x] 实例亲和性:按 Registry ActorIds 定位实例(非 round-robin),崩溃时 fallback(§1.1/1.2)
- [x] MySQL 适配:条件编译守护
- [x] 登录鉴权:简单密码校验(PoC 明文)
