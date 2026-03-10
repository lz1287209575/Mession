cmake_minimum_required(VERSION 3.15)
project(Mession VERSION 1.0.0 LANGUAGES CXX)

# C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -Wall -Wextra -DDEBUG")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall -Wextra")
endif()

# 头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# 公共源文件
set(COMMON_SOURCES
    Core/NetCore.cpp
    Core/Socket.cpp
    Common/ServerConnection.cpp
)

# 1. Gateway Server - 网关服务器
add_executable(GatewayServer 
    ${COMMON_SOURCES}
    Servers/Gateway/GatewayServer.h
    Servers/Gateway/GatewayServer.cpp
    Servers/Gateway/GatewayServerMain.cpp
)
target_link_libraries(GatewayServer m)

# 2. Login Server - 登录服务器
add_executable(LoginServer 
    ${COMMON_SOURCES}
    Servers/Login/LoginServer.h
    Servers/Login/LoginServer.cpp
    Servers/Login/LoginServerMain.cpp
)
target_link_libraries(LoginServer m)

# 3. World Server - 世界服务器
add_executable(WorldServer 
    ${COMMON_SOURCES}
    NetDriver/NetObject.cpp
    NetDriver/NetObject.h
    NetDriver/Replicate.h
    NetDriver/ReplicationDriver.cpp
    NetDriver/ReplicationDriver.h
    Servers/World/WorldServer.h
    Servers/World/WorldServer.cpp
    Servers/World/WorldServerMain.cpp
)
target_link_libraries(WorldServer m)

# 4. Scene Server - 场景服务器
add_executable(SceneServer 
    ${COMMON_SOURCES}
    Servers/Scene/SceneServer.h
    Servers/Scene/SceneServer.cpp
    Servers/Scene/SceneServerMain.cpp
)
target_link_libraries(SceneServer m)

# 安装目标
install(TARGETS 
    GatewayServer 
    LoginServer 
    WorldServer 
    SceneServer 
    DESTINATION bin
)

message(STATUS "=====================================")
message(STATUS "  Mession - Distributed MMO Server")
message(STATUS "=====================================")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Servers: Gateway, Login, World, Scene")
message(STATUS "=====================================")
