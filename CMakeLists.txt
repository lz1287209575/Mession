cmake_minimum_required(VERSION 3.15)
project(Mession VERSION 1.0.0 LANGUAGES CXX)

# C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 统一二进制/库输出目录（与构建目录 Build 分离）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Bin/Lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Bin/Lib)

# 统一编译选项
function(configure_mession_compile_options TargetName)
    target_compile_definitions(${TargetName}
        PRIVATE
            $<$<CONFIG:Debug>:DEBUG>
    )

    if(MSVC)
        target_compile_options(${TargetName}
            PRIVATE
                /W4
                /EHsc
                $<$<CONFIG:Debug>:/Zi>
                $<$<CONFIG:Debug>:/Od>
                $<$<CONFIG:Release>:/O2>
        )
    else()
        target_compile_options(${TargetName}
            PRIVATE
                -Wall
                -Wextra
                $<$<CONFIG:Debug>:-g>
                $<$<CONFIG:Debug>:-O0>
                $<$<CONFIG:Release>:-O3>
        )
    endif()
endfunction()

# Core 库（基础设施：Core/*）
set(MESSION_CORE_SOURCES
    Source/Core/Net/NetCore.cpp
    Source/Core/Net/SocketPlatform.cpp
    Source/Core/Net/Socket.cpp
    Source/Core/Net/HttpDebugServer.cpp
    Source/Core/Event/EventLoop.cpp
    Source/Core/Event/MEventLoop.cpp
    Source/Core/Event/TaskEventLoop.cpp
    Source/Core/Concurrency/TaskQueue.cpp
    Source/Core/Concurrency/ThreadPool.cpp
)

add_library(mession_core STATIC
    ${MESSION_CORE_SOURCES}
)
target_include_directories(mession_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)
if(NOT WIN32)
    target_link_libraries(mession_core PUBLIC m)
endif()
if(WIN32)
    target_link_libraries(mession_core PUBLIC ws2_32)
endif()
configure_mession_compile_options(mession_core)

# Common 库（公共组件：Common/*）
set(MESSION_COMMON_SOURCES
    Source/Common/Logger.cpp
    Source/Common/NetServerBase.cpp
    Source/Common/ServerConnection.cpp
)

add_library(mession_common STATIC
    ${MESSION_COMMON_SOURCES}
)
target_include_directories(mession_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)
target_link_libraries(mession_common
    PUBLIC
        mession_core
)
configure_mession_compile_options(mession_common)

# NetDriver 库（复制与网络对象：NetDriver/*）
set(MESSION_NETDRIVER_SOURCES
    Source/NetDriver/NetObject.cpp
    Source/NetDriver/ReplicationDriver.cpp
    Source/NetDriver/Reflection.cpp
    Source/NetDriver/ReflectionExample.cpp
)

add_library(mession_netdriver STATIC
    ${MESSION_NETDRIVER_SOURCES}
)
target_include_directories(mession_netdriver
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)
target_link_libraries(mession_netdriver
    PUBLIC
        mession_common
)
configure_mession_compile_options(mession_netdriver)

# 统一配置每个目标的公共依赖
function(configure_mession_target TargetName)
    target_include_directories(${TargetName}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/Source
    )
    target_link_libraries(${TargetName}
        PRIVATE
            mession_common
    )
    configure_mession_compile_options(${TargetName})
endfunction()

# 1. Gateway Server - 网关服务器
add_executable(GatewayServer 
    Source/Servers/Gateway/GatewayServer.h
    Source/Servers/Gateway/GatewayServer.cpp
    Source/Servers/Gateway/GatewayServerMain.cpp
)
configure_mession_target(GatewayServer)

# 2. Login Server - 登录服务器
add_executable(LoginServer 
    Source/Servers/Login/LoginServer.h
    Source/Servers/Login/LoginServer.cpp
    Source/Servers/Login/LoginServerMain.cpp
)
configure_mession_target(LoginServer)

# 3. World Server - 世界服务器
add_executable(WorldServer 
    Source/Servers/World/WorldServer.h
    Source/Servers/World/WorldServer.cpp
    Source/Servers/World/WorldServerMain.cpp
)
target_link_libraries(WorldServer
    PRIVATE
        mession_netdriver
)
configure_mession_target(WorldServer)

# 4. Scene Server - 场景服务器
add_executable(SceneServer 
    Source/Servers/Scene/SceneServer.h
    Source/Servers/Scene/SceneServer.cpp
    Source/Servers/Scene/SceneServerMain.cpp
)
configure_mession_target(SceneServer)

# 5. Router Server - 控制面路由服务器
add_executable(RouterServer
    Source/Servers/Router/RouterServer.h
    Source/Servers/Router/RouterServer.cpp
    Source/Servers/Router/RouterServerMain.cpp
)
configure_mession_target(RouterServer)

# NetBench - 简单网络压测工具
add_executable(NetBench
    Source/Tools/NetBench.cpp
)
target_link_libraries(NetBench
    PRIVATE
        mession_core
)
configure_mession_compile_options(NetBench)

# 6. Mono Server - 单机测试服务器（用于反射等功能验证）
add_executable(MonoServer
    Source/Servers/Mono/MonoServer.h
    Source/Servers/Mono/MonoServer.cpp
    Source/Servers/Mono/MonoServerMain.cpp
)
target_link_libraries(MonoServer
    PRIVATE
        mession_netdriver
)
configure_mession_target(MonoServer)

# 安装目标
install(TARGETS 
    GatewayServer 
    LoginServer 
    WorldServer 
    SceneServer 
    RouterServer
    MonoServer
    DESTINATION bin
)

message(STATUS "=====================================")
message(STATUS "  Mession - Distributed MMO Server")
message(STATUS "=====================================")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Servers: Gateway, Login, World, Scene, Router")
message(STATUS "=====================================")
