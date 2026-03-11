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

# 公共源文件
set(COMMON_SOURCES
    Core/NetCore.cpp
    Core/Socket.cpp
    Common/ServerConnection.cpp
)

# 公共基础库
add_library(mession_common STATIC
    ${COMMON_SOURCES}
)
target_include_directories(mession_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(mession_common PUBLIC m)

# 统一配置每个目标的公共依赖
function(configure_mession_target TargetName)
    target_include_directories(${TargetName}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(${TargetName}
        PRIVATE
            mession_common
    )
endfunction()

# 1. Gateway Server - 网关服务器
add_executable(GatewayServer 
    Servers/Gateway/GatewayServer.h
    Servers/Gateway/GatewayServer.cpp
    Servers/Gateway/GatewayServerMain.cpp
)
configure_mession_target(GatewayServer)

# 2. Login Server - 登录服务器
add_executable(LoginServer 
    Servers/Login/LoginServer.h
    Servers/Login/LoginServer.cpp
    Servers/Login/LoginServerMain.cpp
)
configure_mession_target(LoginServer)

# 3. World Server - 世界服务器
add_executable(WorldServer 
    NetDriver/NetObject.cpp
    NetDriver/NetObject.h
    NetDriver/Replicate.h
    NetDriver/ReplicationDriver.cpp
    NetDriver/ReplicationDriver.h
    Servers/World/WorldServer.h
    Servers/World/WorldServer.cpp
    Servers/World/WorldServerMain.cpp
)
configure_mession_target(WorldServer)

# 4. Scene Server - 场景服务器
add_executable(SceneServer 
    Servers/Scene/SceneServer.h
    Servers/Scene/SceneServer.cpp
    Servers/Scene/SceneServerMain.cpp
)
configure_mession_target(SceneServer)

# 5. Router Server - 控制面路由服务器
add_executable(RouterServer
    Servers/Router/RouterServer.h
    Servers/Router/RouterServer.cpp
    Servers/Router/RouterServerMain.cpp
)
configure_mession_target(RouterServer)

# 安装目标
install(TARGETS 
    GatewayServer 
    LoginServer 
    WorldServer 
    SceneServer 
    RouterServer
    DESTINATION bin
)

message(STATUS "=====================================")
message(STATUS "  Mession - Distributed MMO Server")
message(STATUS "=====================================")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Servers: Gateway, Login, World, Scene, Router")
message(STATUS "=====================================")
