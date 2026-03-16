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
    Source/Common/ServerRpcRuntime.cpp
    Source/Common/ReflectionExample.cpp
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

# MHeaderTool - 反射代码生成工具
add_executable(MHeaderTool
    Source/Tools/MHeaderTool.cpp
)
target_link_libraries(MHeaderTool
    PRIVATE
        mession_core
)
configure_mession_compile_options(MHeaderTool)

set(MESSION_GENERATED_DIR "${CMAKE_SOURCE_DIR}/Build/Generated")
set(MESSION_HEADERTOOL_MANIFEST "${MESSION_GENERATED_DIR}/MHeaderToolTargets.cmake")
set(MESSION_HEADERTOOL_CMD
    "${CMAKE_SOURCE_DIR}/Bin/MHeaderTool"
    "--source-root=${CMAKE_SOURCE_DIR}/Source"
    "--output-dir=${MESSION_GENERATED_DIR}"
    "--cmake-manifest=${MESSION_HEADERTOOL_MANIFEST}"
)

if(EXISTS "${CMAKE_SOURCE_DIR}/Bin/MHeaderTool")
    execute_process(
        COMMAND ${MESSION_HEADERTOOL_CMD}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE MESSION_HEADERTOOL_RESULT
    )
    if(NOT MESSION_HEADERTOOL_RESULT EQUAL 0)
        message(WARNING "MHeaderTool configure-time generation failed with code ${MESSION_HEADERTOOL_RESULT}")
    endif()
endif()

set(MESSION_GENERATED_GROUPS)
include("${MESSION_HEADERTOOL_MANIFEST}" OPTIONAL)

if(NOT MESSION_GENERATED_GROUPS)
    set(MESSION_GENERATED_GROUPS shared)
endif()

function(mession_get_generated_group_target GroupName OutVar)
    string(TOLOWER "${GroupName}" GroupNameLower)
    string(REGEX REPLACE "[^a-z0-9_]" "_" GroupNameSafe "${GroupNameLower}")
    set(${OutVar} "mession_generated_${GroupNameSafe}" PARENT_SCOPE)
endfunction()

function(mession_define_generated_group GroupName)
    mession_get_generated_group_target("${GroupName}" TargetName)
    string(TOUPPER "${GroupName}" GroupNameUpper)
    string(REGEX REPLACE "[^A-Z0-9_]" "_" GroupVarSuffix "${GroupNameUpper}")
    set(GroupSourcesVar "MESSION_GENERATED_${GroupVarSuffix}_SOURCES")

    add_library(${TargetName} OBJECT
        Source/Tools/MGeneratedStub.cpp
        ${${GroupSourcesVar}}
    )
    target_include_directories(${TargetName}
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/Source
    )
    configure_mession_compile_options(${TargetName})
endfunction()

foreach(GroupName IN LISTS MESSION_GENERATED_GROUPS)
    mession_define_generated_group("${GroupName}")
endforeach()

add_custom_target(mession_reflection_codegen
    COMMAND ${MESSION_HEADERTOOL_CMD}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS MHeaderTool
    COMMENT "Generating reflection glue with MHeaderTool"
)

add_dependencies(mession_common mession_reflection_codegen)

foreach(GroupName IN LISTS MESSION_GENERATED_GROUPS)
    mession_get_generated_group_target("${GroupName}" GroupTarget)
    if(TARGET ${GroupTarget})
        add_dependencies(${GroupTarget} mession_reflection_codegen)
    endif()
endforeach()

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

function(mession_attach_generated_groups TargetName)
    foreach(GroupName ${ARGN})
        mession_get_generated_group_target("${GroupName}" GroupTarget)
        if(TARGET ${GroupTarget})
            target_sources(${TargetName}
                PRIVATE
                    $<TARGET_OBJECTS:${GroupTarget}>
            )
        endif()
    endforeach()
endfunction()

# 1. Gateway Server - 网关服务器
add_executable(GatewayServer 
    Source/Servers/Gateway/GatewayServer.h
    Source/Servers/Gateway/GatewayServer.cpp
    Source/Servers/Gateway/GatewayServerMain.cpp
)
target_link_libraries(GatewayServer
    PRIVATE
        mession_netdriver
)
configure_mession_target(GatewayServer)
mession_attach_generated_groups(GatewayServer shared gateway)

# 2. Login Server - 登录服务器
add_executable(LoginServer 
    Source/Servers/Login/LoginServer.h
    Source/Servers/Login/LoginServer.cpp
    Source/Servers/Login/LoginServerMain.cpp
)
target_link_libraries(LoginServer
    PRIVATE
        mession_netdriver
)
configure_mession_target(LoginServer)
mession_attach_generated_groups(LoginServer shared login)

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
mession_attach_generated_groups(WorldServer shared world)

# 4. Scene Server - 场景服务器
add_executable(SceneServer 
    Source/Servers/Scene/SceneServer.h
    Source/Servers/Scene/SceneServer.cpp
    Source/Servers/Scene/SceneServerMain.cpp
)
configure_mession_target(SceneServer)
mession_attach_generated_groups(SceneServer scene)

# 5. Router Server - 控制面路由服务器
add_executable(RouterServer
    Source/Servers/Router/RouterServer.h
    Source/Servers/Router/RouterServer.cpp
    Source/Servers/Router/RouterServerMain.cpp
)
target_link_libraries(RouterServer
    PRIVATE
        mession_netdriver
)
configure_mession_target(RouterServer)
mession_attach_generated_groups(RouterServer shared router)

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
mession_attach_generated_groups(MonoServer shared mono)

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
