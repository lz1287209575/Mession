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

### 配置

```bash
cmake -S . -B Build -DCMAKE_BUILD_TYPE=Release
```

### 编译

```bash
cmake --build Build -j4
```

首次配置时，CMake 会尝试引导生成 `MHeaderTool`，用于反射代码生成。正常情况下不需要手动先跑一次工具。

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

## 一键起服

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
- 若要验证完整链路，请使用 `Scripts/validate.py`

## 停服

```bash
python3 Scripts/servers.py stop --build-dir Build
```

Linux 下如果 PID 文件缺失，脚本会尝试按端口清理占用进程。

## 完整链路验证

```bash
python3 Scripts/validate.py --build-dir Build
```

该脚本会执行：

1. 可选编译
2. 启动 `Router -> Mgo -> Login -> World -> Scene -> Gateway`
3. 连接 `Gateway`
4. 通过统一 `MT_FunctionCall` 验证以下请求
   `Client_Login`
   `Client_FindPlayer`
   `Client_SwitchScene`
   `Client_Logout`
5. 清理进程

如果已经编译完成，可跳过构建：

```bash
python3 Scripts/validate.py --build-dir Build --no-build
```

## 端口约定

- `GatewayServer` `8001`
- `LoginServer` `8002`
- `WorldServer` `8003`
- `SceneServer` `8004`
- `RouterServer` `8005`
- `MgoServer` `8006`

## 日志位置

- 一键起服日志：`Logs/servers/`
- `validate.py` 启动的服务日志：同样会落到日志目录

建议开发时至少盯以下日志：

- `Logs/servers/GatewayServer.log`
- `Logs/servers/WorldServer.log`

## Mongo 可选构建

如需启用 MongoDB C++ Driver 集成：

```bash
cmake -S . -B Build -DCMAKE_BUILD_TYPE=Release -DMESSION_USE_MONGOCXX=ON
```

如果不开该选项，`MgoServer` 仍可在无 Mongo 驱动的情况下跑通内存态流程。
