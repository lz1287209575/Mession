# UE Server Project Manager 需求说明

## 目标

本文档用于交付给 UE 编辑器工具开发同学，实现一个面向 `Mession` 服务端工程的 UE Editor 插件。

插件目标不是复用服务端运行时、协议反射或对象系统，而是把 UE 作为“服务端工程管理前端”，提供：

- 服务器工程浏览
- 服务器进程管理
- 构建与验证入口
- 日志与状态查看

当前优先做开发工具，不做游戏 Runtime 功能。

## 背景

`Mession` 当前是一个独立的 C++20 服务端工程，仓库内已经提供：

- 服务端代码目录：`Source/Servers`
- 协议目录：`Source/Protocol/Messages`
- 通用运行时目录：`Source/Common`
- 构建系统：`CMakeLists.txt`
- 起停服脚本：`Scripts/servers.py`
- 完整链路验证脚本：`Scripts/validate.py`
- 文档目录：`Docs`

当前已经有明确的服务划分：

- `GatewayServer` 端口 `8001`
- `LoginServer` 端口 `8002`
- `WorldServer` 端口 `8003`
- `SceneServer` 端口 `8004`
- `RouterServer` 端口 `8005`
- `MgoServer` 端口 `8006`

说明：

- `Scripts/servers.py` 当前负责 `Router/Login/World/Scene/Gateway`
- `Scripts/validate.py` 会拉起 `Router/Mgo/Login/World/Scene/Gateway`

## 插件定位

### 插件类型

- 形态：`UE Editor Plugin`
- 优先平台：`Windows`
- 使用阶段：编辑器开发联调阶段

### 非目标

本期明确不做以下内容：

- 不把服务端 `MSTRUCT/MPROPERTY/MFUNCTION` 直接映射到 UE
- 不把服务端编进 UE 运行时
- 不在 UE 中实现客户端网络协议栈
- 不把服务端文件转换成 UE Asset 真源
- 不支持在插件内深度编辑和重构 C++ 服务端代码结构

## 总体功能范围

插件建议命名为 `MessionServerEditor`，并提供一个总入口面板：`Server Project Manager`。

面板至少包含以下三个页签：

- `Server Content View`
- `Server Process`
- `Server Actions`

## 一、Server Content View

### 目标

在 UE 编辑器中提供一个“服务器工程视图”，用于浏览服务端仓库结构和逻辑模块，而不是简单复刻系统文件浏览器。

### 展示方式

优先做独立的树状面板，不要求第一期深度接入 UE 原生 `Content Browser`。

推荐根节点：

- `Servers`
- `Protocols`
- `Runtime`
- `Scripts`
- `Docs`
- `Generated`
- `Config`
- `Bin`

### 目录映射规则

#### `Servers`

来源目录：`Source/Servers`

建议按服务划分，再展开二级结构：

- `Server.h/.cpp`
- `Rpc`
- `Services`
- `Domain`
- 其他实现文件

当前重点服务：

- `Gateway`
- `Login`
- `World`
- `Scene`
- `Router`
- `Mgo`

#### `Protocols`

来源目录：`Source/Protocol/Messages`

按业务域展示：

- `Auth`
- `Common`
- `Gateway`
- `Mgo`
- `Router`
- `Scene`
- `World`

#### `Runtime`

来源目录：`Source/Common`

优先展示逻辑子域，而不是完全平铺：

- `Net`
- `Runtime/Reflect`
- `Runtime/Object`
- `Runtime/Replication`
- `Runtime/Persistence`
- `Runtime/Concurrency`

#### `Scripts`

来源目录：`Scripts`

重点突出可执行脚本：

- `servers.py`
- `validate.py`
- `verify_protocol.py`
- 其他辅助脚本

#### `Docs`

来源目录：`Docs`

支持快速打开和预览 markdown。

#### `Generated`

来源目录：`Build/Generated`

要求：

- 只读显示
- 明确标记“生成产物，请勿直接编辑”

重点可显示：

- `MClientManifest.generated.h`
- `MRpcManifest.generated.h`
- `MReflectionManifest.generated.h`

### 交互要求

每个条目至少支持：

- 单击：显示详情
- 双击：在外部 IDE 或系统默认程序中打开
- 右键菜单：根据条目类型显示对应动作

### 条目详情区

右侧详情面板建议显示：

- 显示名
- 物理路径
- 类型
- 所属模块
- 最近修改时间
- 只读状态
- 对应建议动作

### 搜索与过滤

第一期至少支持：

- 名称搜索
- 类型过滤
- 只看可执行动作条目

推荐类型：

- `Server`
- `Endpoint`
- `Rpc`
- `Message`
- `Script`
- `Doc`
- `Config`
- `Generated`
- `Binary`
- `Folder`

### Server Content View 非目标

第一期不要求支持：

- 拖拽重命名服务端文件
- 在 UE 里直接编辑 C++ 文本
- 自动重构 include 或类名
- 把这些条目注册成真正的 `.uasset`

## 二、Server Process

### 目标

在 UE 中提供服务端进程的构建、启动、停止、重启、状态检查和日志查看。

### 支持动作

#### Build

执行：

```bash
cmake -S . -B Build -DCMAKE_BUILD_TYPE=Release
cmake --build Build -j4
```

说明：

- 工作目录应为服务端仓库根目录
- `Build` 路径后续可配置，不要写死

#### Start Servers

执行：

```bash
python3 Scripts/servers.py start --build-dir Build
```

说明：

- 当前默认会启动 `Router/Login/World/Scene/Gateway`
- 当前脚本不负责 `MgoServer`

#### Stop Servers

执行：

```bash
python3 Scripts/servers.py stop --build-dir Build
```

#### Validate

执行：

```bash
python3 Scripts/validate.py --build-dir Build --no-build
```

说明：

- `validate.py` 会验证完整链路
- 默认会拉起 `MgoServer`
- 这条能力适合作为“一键自检”

#### Validate With Build

执行：

```bash
python3 Scripts/validate.py --build-dir Build
```

### 单服操作

第一期建议支持 UI 占位，但实现方式可分阶段：

- `Restart Gateway`
- `Restart Login`
- `Restart World`
- `Restart Scene`
- `Restart Router`
- `Restart Mgo`

如果第一期难以单独精确控制单服进程，可以先提供：

- `Stop All`
- `Start All`
- `Validate`

然后在第二期补单服控制。

### 运行状态展示

面板中应显示每个服务的：

- 服务名
- 预期端口
- 当前状态
  - `Stopped`
  - `Starting`
  - `Running`
  - `Stopping`
  - `Unknown`
- 最近启动时间
- 最近一次退出码

### 端口约定

用于状态检测的默认端口：

- `GatewayServer` `8001`
- `LoginServer` `8002`
- `WorldServer` `8003`
- `SceneServer` `8004`
- `RouterServer` `8005`
- `MgoServer` `8006`

### 状态检测建议

优先级建议如下：

1. 若进程由插件拉起，优先记录并跟踪 `PID/Handle`
2. 同时做端口探测，作为辅助状态判断
3. 若插件未拉起该进程，也允许通过端口判定为 `Running`

注意：

- 不要求插件完全依赖自身进程句柄判断
- 要兼容“服务由外部终端启动”的情况

## 三、Server Actions

### 目标

提供对常用脚本和工程动作的统一入口，不要求复杂编排。

### 建议动作列表

- `Configure Build`
- `Build`
- `Start Servers`
- `Stop Servers`
- `Validate`
- `Validate With Build`
- `Open Logs Folder`
- `Open Build Folder`
- `Open Generated Folder`
- `Open Server Docs`

### 与条目联动

在 `Server Content View` 中右键不同条目时，推荐联动以下动作：

- 对 `Script`：可直接执行或打开
- 对 `Doc`：可预览或外部打开
- 对 `Server`：可定位日志、执行重启、打开源码目录
- 对 `Generated`：只允许打开，不允许编辑入口动作

## 四、日志能力

### 目标

在 UE 中查看服务启动结果、脚本输出和日志文件内容。

### 日志来源

当前仓库已有日志位置：

- `Logs/servers/`
- `Build/validate_logs/`

### MVP 要求

第一期至少支持：

- 展示最近一次执行的命令行
- 展示 stdout/stderr 输出
- 快速打开日志目录
- 从面板切换查看各服务日志文件

### 建议能力

- 自动刷新日志尾部
- 按服务筛选
- 错误高亮
- 一键复制失败命令

## 五、插件配置项

需要提供一个插件设置页，允许配置以下内容：

- 服务端仓库根目录
- Python 可执行路径
- CMake 可执行路径
- Build 目录名或绝对路径
- 默认构建配置
- 是否开启日志自动刷新
- 是否显示 `Generated`
- 是否启用 `Mgo` 相关动作

建议提供“自动探测”按钮，用于尝试识别：

- 仓库根目录是否有效
- `Scripts/servers.py` 是否存在
- `Scripts/validate.py` 是否存在
- `CMakeLists.txt` 是否存在
- `Bin/`、`Build/` 是否存在

## 六、实现建议

### 推荐模块拆分

建议至少拆成两个模块：

- `MessionServerCore`
- `MessionServerEditor`

#### `MessionServerCore`

职责：

- 仓库扫描
- 条目模型构建
- 命令构建
- 进程调用封装
- 端口探测
- 日志读取

#### `MessionServerEditor`

职责：

- Slate UI
- Tab 注册
- 设置页
- 交互逻辑
- Action 绑定

### 推荐核心类

以下是建议命名，实际可按 UE 团队习惯调整：

- `FMessionServerCoreModule`
- `FMessionServerEditorModule`
- `UMessionServerProjectSettings`
- `FMessionServerProjectScanner`
- `FMessionServerTreeItem`
- `FMessionProcessRunner`
- `FMessionServerStateTracker`
- `SMessionServerProjectManager`
- `SMessionServerContentView`
- `SMessionServerProcessView`
- `SMessionServerActionsView`
- `SMessionLogView`

## 七、数据模型建议

推荐为 `Server Content View` 建立统一条目模型：

```text
Id
DisplayName
Kind
PhysicalPath
LogicalPath
ModuleName
ServerName
Readonly
Children
Tags
```

字段说明：

- `PhysicalPath`：磁盘真实路径
- `LogicalPath`：在插件中的虚拟分类路径
- `Kind`：条目类型
- `Readonly`：对 `Generated` 等条目标记只读

## 八、刷新与同步要求

### 初始加载

插件打开时扫描一次仓库结构。

### 手动刷新

必须提供 `Refresh` 按钮。

### 自动刷新

第一期可以只做轻量刷新：

- 面板获得焦点时刷新
- 执行命令结束后刷新

不要求第一期实现复杂的文件系统监听。

## 九、与外部工具的协作要求

### 打开源码

双击文件时，优先调用系统默认程序或 IDE 打开，不要求在 UE 内置文本编辑器中打开。

### 外部修改兼容

需要接受以下现实情况：

- 用户可能同时开着 VS / Rider / VSCode
- 文件可能在 UE 外被修改
- 插件应以“重新扫描后的磁盘状态”为准

## 十、MVP 范围

UE 团队第一阶段只需完成以下最小版本：

### MVP-1：基础面板

- 注册一个 `Server Project Manager` Tab
- 读取插件设置中的仓库根目录
- 成功扫描并展示 `Servers/Protocols/Scripts/Docs/Generated`

### MVP-2：基础动作

- `Build`
- `Start Servers`
- `Stop Servers`
- `Validate`
- `Open Logs Folder`

### MVP-3：基础状态

- 显示主要服务端口状态
- 显示最近一次执行结果
- 显示日志输出

### MVP-4：基础交互

- 搜索
- 刷新
- 双击打开文件
- 右键菜单动作

## 十一、验收标准

满足以下条件即可认为第一期完成：

### 功能验收

- 能在 UE 中配置并识别正确的服务端仓库根目录
- 能展示服务端工程视图
- 能成功执行 `Build/Start/Stop/Validate`
- 能显示服务运行状态和日志
- 能从 `Server Content View` 打开源文件和脚本

### 使用体验验收

- 常用动作可以在 2 次点击以内到达
- 失败时能看到明确的命令和错误输出
- `Generated` 条目不会被误导为可编辑资源

## 十二、已知限制

- 第一阶段默认可以只支持 `Windows`
- 第一阶段不要求深度接入 UE 原生 `Content Browser`
- 第一阶段不要求单服精确生命周期托管
- 第一阶段不要求做服务端代码模板生成

## 十三、后续可选扩展

以下能力可作为第二阶段候选项：

- 单服重启
- 常用环境变量模板
- `Mgo` 专项控制
- markdown 预览
- 生成文件过期提示
- 服务脚本预设
- 与源代码关系视图联动
- 更深的 Content Browser 风格界面

## 十四、界面草图

以下草图用于帮助 UE 团队快速统一界面方向。这里强调的是信息布局和交互优先级，不要求像素级还原。

### 1. 总面板布局

推荐做成一个主 Tab：`Server Project Manager`

```text
+--------------------------------------------------------------------------------------+
| Toolbar: [Refresh] [Build] [Start] [Stop] [Validate] [Open Logs] [Settings]         |
+--------------------------------------------------------------------------------------+
| Tabs: [Server Content View] [Server Process] [Server Actions]                        |
+--------------------------------------------------------------------------------------+
| Left Pane                          | Main Pane                      | Right Pane      |
|------------------------------------|--------------------------------|-----------------|
| Search: [.................]        | Content / Process / Actions    | Details         |
| Filter: [All v] [Kind v]           | depends on active tab          | or Log Preview  |
|                                    |                                |                 |
| Tree / List                        |                                |                 |
| - Servers                          |                                |                 |
| - Protocols                        |                                |                 |
| - Runtime                          |                                |                 |
| - Scripts                          |                                |                 |
| - Docs                             |                                |                 |
| - Generated                        |                                |                 |
+--------------------------------------------------------------------------------------+
| Bottom Output: command output / validate output / error text                          |
+--------------------------------------------------------------------------------------+
```

### 2. Server Content View 草图

```text
+--------------------------------------------------------------------------------------+
| Search: [Client_Login...........]  Filter: [All] [Readable Only] [Actionable Only]  |
+-------------------------------+--------------------------------+---------------------+
| Tree                          | Item List / Tree Detail        | Details             |
|-------------------------------|--------------------------------|---------------------|
| Servers                       | World                          | Name: World         |
|  - Gateway                    |   - Server.h                   | Kind: Server        |
|  - Login                      |   - Server.cpp                 | Path: ...           |
|  - World   <selected>         |   - Rpc                        | Module: Servers     |
|  - Scene                      |   - Services                   | Updated: ...        |
|  - Router                     |   - Domain                     | Readonly: No        |
|  - Mgo                        |                                | Actions:            |
| Protocols                     |                                | [Open] [Reveal]     |
| Scripts                       |                                | [Restart] [Logs]    |
| Docs                          |                                |                     |
| Generated                     |                                |                     |
+-------------------------------+--------------------------------+---------------------+
```

### 3. Server Process 草图

```text
+--------------------------------------------------------------------------------------+
| [Start All] [Stop All] [Build] [Validate] [Refresh Status]                           |
+--------------------------------------------------------------------------------------+
| Service Table                                                                         |
|--------------------------------------------------------------------------------------|
| Name           Port   Status     PID/Handle   Last Start   Last Exit   Actions       |
| GatewayServer  8001   Running    12345        10:25:10     0           [Restart][Log]|
| LoginServer    8002   Running    12346        10:25:08     0           [Restart][Log]|
| WorldServer    8003   Running    12347        10:25:06     0           [Restart][Log]|
| SceneServer    8004   Running    12348        10:25:04     0           [Restart][Log]|
| RouterServer   8005   Running    12349        10:25:02     0           [Restart][Log]|
| MgoServer      8006   Stopped    -            -            -           [Start][Log]  |
+--------------------------------------------------------------------------------------+
| Log / Output Panel                                                                    |
| [Gateway v] [Auto Scroll] [Clear]                                                     |
|--------------------------------------------------------------------------------------|
| [servers] Starting GatewayServer...                                                   |
| [servers] GatewayServer ready on port 8001                                            |
| ...                                                                                   |
+--------------------------------------------------------------------------------------+
```

### 4. Server Actions 草图

```text
+--------------------------------------------------------------------------------------+
| Common Actions                                                                        |
|--------------------------------------------------------------------------------------|
| [Configure Build]                                                                     |
| [Build Release]                                                                       |
| [Start Servers]                                                                       |
| [Stop Servers]                                                                        |
| [Validate]                                                                            |
| [Validate With Build]                                                                 |
| [Open Logs Folder]                                                                    |
| [Open Build Folder]                                                                   |
| [Open Generated Folder]                                                               |
| [Open Server Docs]                                                                    |
+--------------------------------------------------------------------------------------+
| Preset / Command Preview                                                              |
|--------------------------------------------------------------------------------------|
| WorkingDir: D:\Mession                                                                |
| Command: python3 Scripts/validate.py --build-dir Build --no-build                     |
| Env: MESSION_WORLD_MGO_PERSISTENCE_ENABLE=1                                           |
+--------------------------------------------------------------------------------------+
```

### 5. Settings 草图

```text
+--------------------------------------------------------------------------------------+
| Mession Server Settings                                                               |
|--------------------------------------------------------------------------------------|
| Repo Root:        [D:\Mession...............................................] [Browse]|
| Python Path:      [python.exe...............................................] [Auto]  |
| CMake Path:       [cmake.exe................................................] [Auto]  |
| Build Dir:        [Build....................................................]         |
| Config:           [Release v]                                                         |
| Show Generated:   [x]                                                                 |
| Enable Mgo Tools: [x]                                                                 |
| Auto Refresh Log: [x]                                                                 |
|--------------------------------------------------------------------------------------|
| [Validate Paths] [Save]                                                               |
| Status: OK - found CMakeLists.txt / Scripts / Bin                                     |
+--------------------------------------------------------------------------------------+
```

## 十五、交互流程

以下流程描述第一期必须明确的用户路径。UE 团队实现时请优先保证这些流程顺畅。

### 流程 1：首次配置

目标：用户第一次打开插件后可以完成基本配置并进入可用状态。

步骤：

1. 用户打开 `Server Project Manager`
2. 若未配置仓库根目录，面板显示空态提示
3. 用户点击 `Settings`
4. 配置：
   - `Repo Root`
   - `Python Path`
   - `CMake Path`
   - `Build Dir`
5. 用户点击 `Validate Paths`
6. 插件检查：
   - `CMakeLists.txt` 是否存在
   - `Scripts/servers.py` 是否存在
   - `Scripts/validate.py` 是否存在
   - `Source/Servers` 是否存在
7. 成功后返回主面板并刷新树视图

期望结果：

- 左侧 `Server Content View` 树成功显示
- 工具栏动作变为可用

### 流程 2：浏览服务器工程

目标：用户可以在 UE 内快速查看服务端工程结构并打开外部文件。

步骤：

1. 打开 `Server Content View`
2. 在左侧树选择 `Servers -> World`
3. 中间区域显示该模块下的 `Server/Rpc/Services/Domain`
4. 用户单击 `World`
5. 右侧显示详情：
   - 路径
   - 类型
   - 最近修改时间
   - 可执行动作
6. 用户双击 `WorldServer.cpp`
7. 插件调用外部程序打开对应文件

期望结果：

- 详情区同步更新
- 外部 IDE 或系统默认程序成功打开该文件

### 流程 3：搜索与过滤

目标：用户能够快速定位某个服务端文件或脚本。

步骤：

1. 用户在搜索框输入 `validate`
2. 树和列表联动过滤相关条目
3. 用户切换过滤器为 `Script`
4. 中间区域只显示脚本相关条目
5. 用户选中 `Scripts/validate.py`
6. 右侧显示脚本详情和动作：
   - `Open`
   - `Run Validate`

期望结果：

- 搜索应在 1 秒内响应
- 过滤结果准确，不展示无关大类

### 流程 4：一键构建

目标：用户在 UE 中触发服务端构建并看到结果。

步骤：

1. 用户点击工具栏 `Build`
2. 插件进入执行态：
   - 禁用重复提交按钮
   - 在底部输出区显示命令
3. 插件执行：

```bash
cmake -S . -B Build -DCMAKE_BUILD_TYPE=Release
cmake --build Build -j4
```

4. 输出区实时刷新日志
5. 执行完成后显示：
   - 成功 / 失败
   - 总耗时
   - 失败时保留完整命令和错误输出

期望结果：

- 用户不需要切出 UE 即可知道构建结果
- 构建失败时能直接复制命令去终端复现

### 流程 5：启动服务器

目标：用户在 UE 中启动本地服务端。

步骤：

1. 用户点击 `Start Servers`
2. 输出区显示执行命令：

```bash
python3 Scripts/servers.py start --build-dir Build
```

3. 插件进入 `Starting` 状态
4. 轮询端口状态和进程状态
5. 逐步把各服务状态更新为：
   - `Router`
   - `Login`
   - `World`
   - `Scene`
   - `Gateway`
6. 若成功，服务表显示 `Running`

期望结果：

- 用户能看到每个服务启动进度
- 启动失败时能明确看到失败服务和输出信息

### 流程 6：停止服务器

目标：用户在 UE 中停止已运行服务。

步骤：

1. 用户点击 `Stop Servers`
2. 输出区显示执行命令：

```bash
python3 Scripts/servers.py stop --build-dir Build
```

3. 插件进入 `Stopping` 状态
4. 状态轮询直到各服务端口不再占用
5. 服务表刷新为 `Stopped`

期望结果：

- 即使服务不是由本插件拉起，也尽量能正确反映停止结果

### 流程 7：执行完整验证

目标：用户在 UE 中运行完整链路验证。

步骤：

1. 用户点击 `Validate`
2. 输出区显示命令：

```bash
python3 Scripts/validate.py --build-dir Build --no-build
```

3. 插件持续显示 validate 输出
4. 执行完成后在顶部或底部给出明确结果：
   - `Validate Passed`
   - `Validate Failed`
5. 若失败，输出区自动滚动到最后一段错误附近

期望结果：

- 用户可以直接从 UE 面板判断当前后端链路是否可用

### 流程 8：查看日志

目标：用户快速查看某个服务日志。

步骤：

1. 用户进入 `Server Process`
2. 点击某个服务行的 `Log`
3. 底部日志区切换到对应服务日志
4. 用户可切换：
   - `Auto Scroll`
   - `Refresh`
   - `Open Folder`

期望结果：

- 常用日志查看不依赖离开 UE
- 日志不可读时要有明确提示

### 流程 9：从 Content View 执行动作

目标：用户可从工程视图直接触发相关动作。

步骤：

1. 用户在 `Server Content View` 中右键 `Scripts/validate.py`
2. 弹出菜单：
   - `Open`
   - `Run Validate`
3. 用户选择 `Run Validate`
4. 插件跳转到 `Server Actions` 或底部输出区执行命令

期望结果：

- 内容视图不是纯浏览器，而是动作入口

### 流程 10：空态与异常态

目标：插件在未配置或配置错误时也能给出明确反馈。

未配置场景：

- 显示空态卡片
- 提供 `Go to Settings`
- 提示缺少仓库路径

路径错误场景：

- 明确指出哪个路径缺失
- 例如：
  - `missing CMakeLists.txt`
  - `missing Scripts/servers.py`
  - `missing Source/Servers`

命令失败场景：

- 必须显示：
  - Working Directory
  - Full Command
  - Exit Code
  - Stdout / Stderr

## 十六、对 UE 团队的实现原则

请 UE 团队在实现时遵守以下原则：

- 服务端仓库始终是真实数据源
- 插件是管理层，不是协议层
- `Build/Generated` 一律按只读看待
- 第一阶段优先交付稳定的浏览与操作能力，不优先追求深度资产化

## 十七、参考路径

UE 团队实现时可直接参考以下仓库路径：

- `CMakeLists.txt`
- `Scripts/servers.py`
- `Scripts/validate.py`
- `Source/Servers`
- `Source/Protocol/Messages`
- `Source/Common`
- `Docs/BuildAndRun.md`
- `Docs/Tooling.md`
- `Build/Generated/MClientManifest.generated.h`
