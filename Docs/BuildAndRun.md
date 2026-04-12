# 构建与运行

## 环境要求

- CMake `>= 3.15`
- 支持 C++20 的编译器
- Python 3
- 可选 MongoDB C++ Driver

当前 `CMakeLists.txt` 已固定：

- `CMAKE_CXX_STANDARD=20`
- 构建目录默认使用 `Build/`
- 可执行文件统一输出到 `Bin/`
- 反射生成输出到 `Build/Generated/`

## 基本构建

### 通用 CMake 方式

配置：

```bash
cmake -S . -B Build -DCMAKE_BUILD_TYPE=Release
```

编译：

```bash
cmake --build Build -j4
```

### Windows 便捷脚本

仓库提供了一个面向 Visual Studio 2022 的便捷脚本：

```bat
Scripts\Build.bat Release
```

它会：

1. 配置 `Visual Studio 17 2022 x64`
2. 生成 `Build/`
3. 编译 `Release`

## 反射代码生成

首次配置时，CMake 会尝试引导生成 `MHeaderTool`，用于反射代码生成。正常情况下不需要手动先跑一次工具。

反射相关输出位于：

- `Build/Generated/`
- `Build/Generated/MHeaderToolTargets.cmake`

如果你改了反射宏、`MFUNCTION`、`MSTRUCT` 或 `MPROPERTY`，优先确认这里是否有更新。

## 可执行文件

编译完成后，主要二进制位于 `Bin/`：

- `GatewayServer`
- `LoginServer`
- `WorldServer`
- `SceneServer`
- `RouterServer`
- `MgoServer`
- `MHeaderTool`
- `NetBench`

## 本地起服

```bash
python3 Scripts/servers.py start --build-dir Build
```

默认启动顺序：

1. `RouterServer`
2. `LoginServer`
3. `WorldServer`
4. `SceneServer`
5. `GatewayServer`

说明：

- `servers.py` 面向本地开发起停服
- 当前它不负责 `MgoServer`
- 适合长时间盯日志、手工连客户端、配合 `test_client.py` 调试

## 停服

```bash
python3 Scripts/servers.py stop --build-dir Build
```

Linux 下如果 PID 文件缺失，脚本会尝试按端口清理占用进程。

## 推荐验证命令

如果已经编译完成，优先跑：

```bash
python3 Scripts/validate.py --build-dir Build --no-build
```

如果希望脚本内部也带编译：

```bash
python3 Scripts/validate.py --build-dir Build
```

`validate.py` 当前会执行：

1. 可选编译
2. 启动 `Router -> Mgo -> Login -> World -> Scene -> Gateway`
3. 连接 `Gateway`
4. 通过统一 `MT_FunctionCall` 验证：
   `Client_Login`
   `Client_FindPlayer`
   `Client_SwitchScene`
   `Client_Move`
   `Client_QueryPawn`
   `Client_ChangeGold`
   `Client_EquipItem`
   `Client_GrantExperience`
   `Client_ModifyHealth`
   `Client_QueryProfile`
   `Client_QueryInventory`
   `Client_QueryProgression`
   双玩家场景下行同步
   `Client_CastSkill`
   登出后重登恢复
   forwarded `ClientCall` 的参数错误 / 业务错误 / 后端不可用错误
5. 清理进程

## 端口约定

- `GatewayServer` `8001`
- `LoginServer` `8002`
- `WorldServer` `8003`
- `SceneServer` `8004`
- `RouterServer` `8005`
- `MgoServer` `8006`

## 日志位置

- `servers.py` 启动日志：`Logs/servers/`
- `validate.py` 启动日志：`Build/validate_logs/`

建议开发时至少盯以下日志：

- `GatewayServer.log`
- `WorldServer.log`
- `SceneServer.log`

## Mongo 可选构建

如需启用 MongoDB C++ Driver 集成：

```bash
cmake -S . -B Build -DCMAKE_BUILD_TYPE=Release -DMESSION_USE_MONGOCXX=ON
```

如果不开该选项，`MgoServer` 仍可在无 Mongo 驱动的情况下跑通内存态流程。

`validate.py` 还支持：

- `--no-mgo`
- `--mongo-db`
- `--mongo-collection`

适合把验证数据隔离到单独 sandbox 库里。
