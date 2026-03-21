cmake_minimum_required(VERSION 3.15)
project(Mession VERSION 1.0.0 LANGUAGES CXX)
option(MESSION_USE_MONGOCXX "Enable MongoDB C++ driver integration for MgoServer" OFF)

# C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 统一二进制/库输出目录（与构建目录 Build 分离）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Bin/Lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Bin/Lib)
set(MESSION_GENERATED_DIR "${CMAKE_SOURCE_DIR}/Build/Generated")

# Keep multi-config generators (Visual Studio/Xcode) from adding per-config
# subdirectories so custom tools can be referenced from a stable Bin path.
foreach(Config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${Config}" ConfigUpper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${ConfigUpper} ${CMAKE_SOURCE_DIR}/Bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${ConfigUpper} ${CMAKE_SOURCE_DIR}/Bin/Lib/${Config})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${ConfigUpper} ${CMAKE_SOURCE_DIR}/Bin/Lib/${Config})
endforeach()

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
                /utf-8
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

# Core 库（底层运行时：Common 下的网络/并发/事件循环基础能力）
set(MESSION_CORE_SOURCES
    Source/Common/Net/HttpDebugServer.cpp
    Source/Common/IO/Socket/SocketPlatform.cpp
    Source/Common/IO/Socket/Socket.cpp
    Source/Common/Runtime/EventLoop/TaskEventLoop.cpp
    Source/Common/Runtime/EventLoop/NetEventLoop.cpp
    Source/Common/Runtime/Concurrency/TaskQueue.cpp
    Source/Common/Runtime/Concurrency/ThreadPool.cpp
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

# Common 库（公共组件：日志/RPC/连接等中层能力）
set(MESSION_COMMON_SOURCES
    Source/Common/Runtime/Log/Logger.cpp
    Source/Common/Runtime/Log/ConsoleLogSink.cpp
    Source/Common/Runtime/Log/FileLogSink.cpp
    Source/Common/Net/NetServerBase.cpp
    Source/Common/Net/ServerConnection.cpp
    Source/Common/Net/ServerRpcRuntime.cpp
)

add_library(mession_common STATIC
    ${MESSION_COMMON_SOURCES}
)
target_include_directories(mession_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${MESSION_GENERATED_DIR}
)
target_link_libraries(mession_common
    PUBLIC
        mession_core
)
configure_mession_compile_options(mession_common)

# NetDriver 库（游戏侧复制与对象能力，物理文件已迁入 Common/Runtime/*）
set(MESSION_NETDRIVER_SOURCES
    Source/Servers/World/Avatar/AvatarMember.cpp
    Source/Servers/World/Avatar/AttributeMember.cpp
    Source/Servers/World/Avatar/InventoryMember.cpp
    Source/Servers/World/Avatar/InteractionMember.cpp
    Source/Servers/World/Avatar/MovementMember.cpp
    Source/Servers/World/Avatar/PlayerAvatar.cpp
    Source/Common/Runtime/Object/NetObject.cpp
    Source/Common/Runtime/Persistence/MongoPersistenceSink.cpp
    Source/Common/Runtime/Replication/ReplicationDriver.cpp
    Source/Common/Runtime/Reflect/Class.cpp
    Source/Common/Runtime/Reflect/Property.cpp
    Source/Common/Runtime/Reflect/Reflection.cpp
)

add_library(mession_netdriver STATIC
    ${MESSION_NETDRIVER_SOURCES}
)
target_include_directories(mession_netdriver
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${MESSION_GENERATED_DIR}
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

set(MESSION_HEADERTOOL_MANIFEST "${MESSION_GENERATED_DIR}/MHeaderToolTargets.cmake")
file(MAKE_DIRECTORY "${MESSION_GENERATED_DIR}")

set(MESSION_HEADERTOOL_EXE "${CMAKE_SOURCE_DIR}/Bin/MHeaderTool")
set(MESSION_HEADERTOOL_BOOTSTRAP_EXE "${CMAKE_BINARY_DIR}/MHeaderTool.bootstrap")

# Fresh checkout bootstrap:
# If Bin/MHeaderTool does not exist yet, compile a temporary host tool at configure-time
# so generated reflection sources and manifest are available in the first build.
if(NOT EXISTS "${MESSION_HEADERTOOL_EXE}")
    execute_process(
        COMMAND
            "${CMAKE_CXX_COMPILER}"
            -std=c++20
            -O2
            "${CMAKE_SOURCE_DIR}/Source/Tools/MHeaderTool.cpp"
            -o
            "${MESSION_HEADERTOOL_BOOTSTRAP_EXE}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE MESSION_HEADERTOOL_BOOTSTRAP_RESULT
        ERROR_VARIABLE MESSION_HEADERTOOL_BOOTSTRAP_ERROR
    )
    if(NOT MESSION_HEADERTOOL_BOOTSTRAP_RESULT EQUAL 0)
        message(WARNING
            "Failed to compile configure-time MHeaderTool bootstrap (code ${MESSION_HEADERTOOL_BOOTSTRAP_RESULT}). "
            "First build may require reconfigure.\n${MESSION_HEADERTOOL_BOOTSTRAP_ERROR}")
    endif()
endif()

set(MESSION_HEADERTOOL_RUNNER "${MESSION_HEADERTOOL_EXE}")
if(NOT EXISTS "${MESSION_HEADERTOOL_RUNNER}" AND EXISTS "${MESSION_HEADERTOOL_BOOTSTRAP_EXE}")
    set(MESSION_HEADERTOOL_RUNNER "${MESSION_HEADERTOOL_BOOTSTRAP_EXE}")
endif()

set(MESSION_HEADERTOOL_CMD
    "${MESSION_HEADERTOOL_RUNNER}"
    "--source-root=${CMAKE_SOURCE_DIR}/Source"
    "--output-dir=${MESSION_GENERATED_DIR}"
    "--cmake-manifest=${MESSION_HEADERTOOL_MANIFEST}"
)

if(EXISTS "${MESSION_HEADERTOOL_RUNNER}")
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
    set(GroupSources ${${GroupSourcesVar}})

    # Use a stub only when this group currently has no generated sources.
    # This avoids brittle duplicate stub object dependencies across groups.
    if(GroupSources)
        add_library(${TargetName} OBJECT
            ${GroupSources}
        )
    else()
        add_library(${TargetName} OBJECT
            Source/Tools/MGeneratedStub.cpp
        )
    endif()
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
            ${MESSION_GENERATED_DIR}
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
    Source/Servers/World/PlayerSession.cpp
    Source/Servers/World/WorldServerGameplay.cpp
    Source/Servers/World/WorldServerClientSync.cpp
    Source/Servers/World/PlayerManager.cpp
    Source/Servers/World/WorldServerBackend.cpp
    Source/Servers/World/WorldServerPersistence.cpp
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
target_link_libraries(SceneServer
    PRIVATE
        mession_netdriver
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

# 6. Mgo Server - 持久化接入层服务器
add_executable(MgoServer
    Source/Servers/Mgo/MgoServer.h
    Source/Servers/Mgo/MgoServer.cpp
    Source/Servers/Mgo/MgoServerMain.cpp
)
target_link_libraries(MgoServer
    PRIVATE
        mession_netdriver
)
if(MESSION_USE_MONGOCXX)
    find_package(mongocxx CONFIG QUIET)
    find_package(bsoncxx CONFIG QUIET)
    if(mongocxx_FOUND AND bsoncxx_FOUND)
        target_link_libraries(MgoServer PRIVATE mongo::mongocxx_shared mongo::bsoncxx_shared)
        target_compile_definitions(MgoServer PRIVATE MESSION_USE_MONGOCXX=1)
        message(STATUS "MgoServer MongoDB integration: enabled (mongocxx/bsoncxx found)")
    else()
        message(WARNING "MESSION_USE_MONGOCXX=ON but mongocxx/bsoncxx not found; building MgoServer without MongoDB driver")
    endif()
endif()
configure_mession_target(MgoServer)
mession_attach_generated_groups(MgoServer shared mgo)

# NetBench - 简单网络压测工具
add_executable(NetBench
    Source/Tools/NetBench.cpp
)
target_link_libraries(NetBench
    PRIVATE
        mession_core
)
configure_mession_compile_options(NetBench)

# 安装目标
install(TARGETS 
    GatewayServer 
    LoginServer 
    WorldServer 
    SceneServer 
    RouterServer
    MgoServer
    DESTINATION bin
)

message(STATUS "=====================================")
message(STATUS "  Mession - Distributed MMO Server")
message(STATUS "=====================================")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Servers: Gateway, Login, World, Scene, Router, Mgo")
message(STATUS "=====================================")
