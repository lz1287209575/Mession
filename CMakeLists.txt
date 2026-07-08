cmake_minimum_required(VERSION 3.15)
project(Mession VERSION 1.0.0 LANGUAGES CXX)
option(MESSION_USE_MONGOCXX "Enable MongoDB C++ driver integration for MgoServer" OFF)

# C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译性能优化
include(ProcessorCount)
ProcessorCount(NUM_CORES)
if(NUM_CORES GREATER_EQUAL 16)
    set(CMAKE_BUILD_TYPE_INITIAL "${CMAKE_BUILD_TYPE}")
    set(DEFAULT_JOBS 32)
elseif(NUM_CORES GREATER_EQUAL 8)
    set(DEFAULT_JOBS 16)
else()
    set(DEFAULT_JOBS 4)
endif()
if(NOT CMAKE_BUILD_TYPE_INITIAL)
    set(CMAKE_BUILD_TYPE_INITIAL "${DEFAULT_BUILD_TYPE:-Release}")
endif()

# ccache支持
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "ccache found: ${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
else()
    message(STATUS "ccache not found, install it for faster rebuilds: apt install ccache")
endif()

# 预编译头文件
option(MESSION_USE_PCH "Enable precompiled headers" ON)
if(MESSION_USE_PCH AND NOT MSVC)
    set(MESSION_PCH_HEADER "Source/Common/Runtime/MLib.h")
    set(MESSION_PCH_SOURCE "Source/Common/Runtime/MLib.cpp")
    if(EXISTS "${CMAKE_SOURCE_DIR}/${MESSION_PCH_HEADER}")
        message(STATUS "PCH enabled: ${MESSION_PCH_HEADER}")
    endif()
endif()

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

    # 预编译头文件支持
    if(MESSION_USE_PCH AND EXISTS "${CMAKE_SOURCE_DIR}/${MESSION_PCH_HEADER}")
        target_precompile_headers(${TargetName}
            PRIVATE
                <${MESSION_PCH_HEADER}>
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
    Source/Common/Asset/UAssetPackageReader.cpp
    Source/Common/Runtime/Concurrency/FiberAwait.cpp
    Source/Common/Runtime/Concurrency/FiberScheduler.cpp
    Source/Common/Runtime/Log/Logger.cpp
    Source/Common/Runtime/Log/ConsoleLogSink.cpp
    Source/Common/Runtime/Log/FileLogSink.cpp
    Source/Common/Net/NetServerBase.cpp
    Source/Common/Net/Routing/ActorRouter.cpp
    Source/Common/Net/ServerConnection.cpp
    Source/Common/Net/Rpc/RpcClientCall.cpp
    Source/Common/Net/Rpc/RpcDispatch.cpp
    Source/Common/Net/Rpc/RpcManifest.cpp
    Source/Common/Net/Rpc/RpcServerCall.cpp
    Source/Common/Net/Rpc/RpcTransport.cpp
    Source/Servers/App/ServiceContainer.cpp
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
    Source/Common/Runtime/Asset/MObjectAssetJson.cpp
    Source/Common/Runtime/Asset/MObjectAssetBinary.cpp
    Source/Common/Runtime/Asset/MObjectAssetCompiler.cpp
    Source/Common/Runtime/Asset/MObjectAssetLoader.cpp
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

# MHeaderTool - 反射代码生成工具 (模块化版本)
add_subdirectory(Source/Tools/MHeaderTool)

set(MESSION_HEADERTOOL_MANIFEST "${MESSION_GENERATED_DIR}/MHeaderToolTargets.cmake")
file(MAKE_DIRECTORY "${MESSION_GENERATED_DIR}")

set(MESSION_HEADERTOOL_EXE "${CMAKE_SOURCE_DIR}/Bin/MHeaderTool${CMAKE_EXECUTABLE_SUFFIX}")
set(MESSION_HEADERTOOL_BOOTSTRAP_EXE "${CMAKE_BINARY_DIR}/MHeaderTool.bootstrap${CMAKE_EXECUTABLE_SUFFIX}")

set(MESSION_HEADERTOOL_BOOTSTRAP_COMMAND
    "${CMAKE_CXX_COMPILER}"
)
if(MSVC)
    list(APPEND MESSION_HEADERTOOL_BOOTSTRAP_COMMAND
        /std:c++20
        /EHsc
        /utf-8
        "${CMAKE_SOURCE_DIR}/Source/Tools/MHeaderTool/MHeaderTool.cpp"
        "/Fe:${MESSION_HEADERTOOL_BOOTSTRAP_EXE}"
    )
else()
    list(APPEND MESSION_HEADERTOOL_BOOTSTRAP_COMMAND
        -std=c++20
        -O2
        "${CMAKE_SOURCE_DIR}/Source/Tools/MHeaderTool/MHeaderTool.cpp"
        -I "${CMAKE_SOURCE_DIR}/Source"
        -I "${CMAKE_SOURCE_DIR}/Source/Tools/MHeaderTool"
        -o
        "${MESSION_HEADERTOOL_BOOTSTRAP_EXE}"
    )
endif()

# Fresh checkout bootstrap:
# If Bin/MHeaderTool does not exist yet, compile a temporary host tool at configure-time
# so generated reflection sources and manifest are available in the first build.
if(NOT EXISTS "${MESSION_HEADERTOOL_EXE}")
    execute_process(
        COMMAND ${MESSION_HEADERTOOL_BOOTSTRAP_COMMAND}
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
message(STATUS "Including manifest: ${MESSION_HEADERTOOL_MANIFEST}")
include("${MESSION_HEADERTOOL_MANIFEST}" OPTIONAL)
message(STATUS "MESSION_GENERATED_GROUPS = ${MESSION_GENERATED_GROUPS}")

if(NOT MESSION_GENERATED_GROUPS)
    message(WARNING "No generated groups found, using stub")
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

# 暂禁 MObjectAssetSmokeTool：依赖已删除的 Source/Servers/Scene/Combat/。
# Task 6 后续按需恢复（独立排期）。
if(FALSE)
add_executable(MObjectAssetSmokeTool
    Source/Tools/MObjectAssetSmokeTool.cpp
)
target_link_libraries(MObjectAssetSmokeTool
    PRIVATE
        mession_netdriver
)
configure_mession_target(MObjectAssetSmokeTool)
target_sources(MObjectAssetSmokeTool
    PRIVATE
        ${MESSION_GENERATED_DIR}/FCombatUnitRef.mgenerated.cpp
        ${MESSION_GENERATED_DIR}/FSceneCombatAvatarSnapshot.mgenerated.cpp
        ${MESSION_GENERATED_DIR}/FSceneCombatMonsterSpawnParams.mgenerated.cpp
        ${MESSION_GENERATED_DIR}/MMonster.mgenerated.cpp
        ${MESSION_GENERATED_DIR}/MMonsterCombatProfile.mgenerated.cpp
        ${MESSION_GENERATED_DIR}/MMonsterCombatState.mgenerated.cpp
        ${MESSION_GENERATED_DIR}/MMonsterConfig.mgenerated.cpp
        ${MESSION_GENERATED_DIR}/MMonsterFactory.mgenerated.cpp
        ${MESSION_GENERATED_DIR}/MMonsterManager.mgenerated.cpp
)
add_dependencies(MObjectAssetSmokeTool mession_reflection_codegen)
endif()

# 暂禁 MObjectEditorService：依赖已删除的 Source/Servers/Scene/Combat/MonsterConfig.h。
# Task 6 后续按需恢复（独立排期）。
if(FALSE)
add_executable(MObjectEditorService
    Source/Tools/MObjectEditorService/App/MObjectEditorServiceApp.cpp
    Source/Tools/MObjectEditorService/Core/EditorPaths.cpp
    Source/Tools/MObjectEditorService/Documents/MonsterConfigDocument.cpp
    Source/Tools/MObjectEditorService/Export/AssetExportService.cpp
    Source/Tools/MObjectEditorService/Http/EditorApiController.cpp
    Source/Tools/MObjectEditorService/Http/HttpServer.cpp
    Source/Tools/MObjectEditorService/Models/MonsterConfigEditorModel.cpp
    Source/Tools/MObjectEditorService/Validation/MonsterConfigValidator.cpp
    Source/Tools/MObjectEditorService/MObjectEditorServiceMain.cpp
)
target_link_libraries(MObjectEditorService
    PRIVATE
        mession_netdriver
)
configure_mession_target(MObjectEditorService)
target_sources(MObjectEditorService
    PRIVATE
        ${MESSION_GENERATED_DIR}/FSceneCombatMonsterSpawnParams.mgenerated.cpp
        ${MESSION_GENERATED_DIR}/MMonsterConfig.mgenerated.cpp
)
add_dependencies(MObjectEditorService mession_reflection_codegen)
endif()

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

# 2. Login Server - 已由 EchoService（同质多进程）替代
# 详见 Docs/RefactorArchitectureAndRpc.md §5

# 3. World Server - 已由 EchoService（同质多进程）替代
# 详见 Docs/RefactorArchitectureAndRpc.md §5

# 4. Scene Server - 已由 EchoService（同质多进程）替代
# 详见 Docs/RefactorArchitectureAndRpc.md §5

# 5. Router Server - 已由 EchoService（同质多进程）替代
# 详见 Docs/RefactorArchitectureAndRpc.md §5

# 6. Mgo Server - 已由 EchoService（同质多进程）替代
# 详见 Docs/RefactorArchitectureAndRpc.md §5

# 7. EchoService - 同质多进程 Service（PoC 替代 Login/Scene/Router/Mgo 4 服）
add_executable(EchoService
    Source/Servers/EchoService/EchoService.h
    Source/Servers/EchoService/EchoService.cpp
    Source/Servers/EchoService/EchoServiceMain.cpp
    Source/Servers/App/ServiceContainer.h
    Source/Servers/App/ServiceContainer.cpp
)
target_link_libraries(EchoService
    PRIVATE
        mession_netdriver
)
configure_mession_target(EchoService)
mession_attach_generated_groups(EchoService shared sample)
target_sources(EchoService
    PRIVATE
        ${MESSION_GENERATED_DIR}/MEchoService.mgenerated.cpp
        ${MESSION_GENERATED_DIR}/FSampleEchoRequest.mgenerated.cpp
        ${MESSION_GENERATED_DIR}/FSampleEchoResponse.mgenerated.cpp
)
add_dependencies(EchoService mession_reflection_codegen)

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
    DESTINATION bin
)

message(STATUS "=====================================")
message(STATUS "  Mession - Distributed MMO Server")
message(STATUS "=====================================")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Servers: Gateway, Login, World, Scene, Router, Mgo")
message(STATUS "=====================================")
