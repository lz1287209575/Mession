cmake_minimum_required(VERSION 3.15)
project(Mession VERSION 1.0.0 LANGUAGES CXX)
option(MESSION_USE_MONGOCXX "Enable MongoDB C++ driver integration for MgoServer" OFF)

# C++标准
set(CMAKE_CXX_STANDARD 17)
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
    Source/Common/Runtime/Async/AsyncContext.cpp
    Source/Common/Runtime/Concurrency/FiberAwait.cpp
    Source/Common/Runtime/Concurrency/FiberScheduler.cpp
    Source/Common/Runtime/Concurrency/SignalHandler.cpp
    Source/Common/Runtime/Log/ConsoleSink.cpp
    Source/Common/Runtime/Log/RollingFileSink.cpp
    Source/Common/Runtime/Log/UdpSink.cpp
    Source/Common/Runtime/Log/TcpSink.cpp
    Source/Common/Runtime/Log/LogStringTable.cpp
    Source/Common/Runtime/Log/LogContext.cpp
    Source/Common/Runtime/Log/LogRegistry.cpp
    Source/Common/Runtime/Log/LogMetrics.cpp
    Source/Common/Runtime/Log/LogCategories.cpp
    Source/Common/Runtime/Log/LogRouter.cpp
    Source/Common/Runtime/Log/SinkWriter.cpp
    Source/Common/Runtime/Log/Dispatcher.cpp
    Source/Common/Runtime/Log/CoredumpSink.cpp
    Source/Common/Runtime/Log/LogConfig.cpp
    Source/Common/Runtime/Log/Log.cpp
    Source/Common/Net/ClientCall/MClientTargetResolver.cpp
    Source/Common/Net/NetServerBase.cpp
    Source/Common/Net/Routing/ActorRouter.cpp
    Source/Common/Net/ServerConnection.cpp
    Source/Common/Net/Rpc/RpcClientCall.cpp
    Source/Common/Net/Rpc/RpcDispatch.cpp
    Source/Common/Net/Rpc/RpcManifest.cpp
    Source/Common/Net/Rpc/RpcServerCall.cpp
    Source/Common/Net/Rpc/RpcTransport.cpp
    Source/Common/Net/Rpc/MClientManifest.cpp
    Source/Common/Net/Rpc/MRpcChannel.cpp
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

# NetDriver 库（游戏侧对象能力：反射驱动的资产加载/序列化）
set(MESSION_NETDRIVER_SOURCES
    Source/Common/Runtime/Asset/MObjectAssetJson.cpp
    Source/Common/Runtime/Asset/MObjectAssetBinary.cpp
    Source/Common/Runtime/Asset/MObjectAssetCompiler.cpp
    Source/Common/Runtime/Asset/MObjectAssetLoader.cpp
    Source/Common/Runtime/Reflect/Class.cpp
    Source/Common/Runtime/Reflect/Property.cpp
    Source/Common/Runtime/Reflect/Reflection.cpp
    Source/Common/Net/ServiceDiscovery/Endpoint.cpp
    Source/Common/Net/ServiceDiscovery/RegistryProtocol.cpp
    Source/Common/Net/ServiceDiscovery/EndpointCache.cpp
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

# Script Abstract layer (Task 1 of 2026-08-04 plan)
add_subdirectory(Source/Common/Script/Abstract)

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
        -std=c++17
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
    "--client-manifest=${MESSION_GENERATED_DIR}/MClientManifest.mgenerated.cpp"
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
            ${MESSION_GENERATED_DIR}
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
            mession_netdriver
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

# mession_add_service — 注册一个 Service binary
#
# 用法:
#   mession_add_service(<target> <ServiceClass> <ConfigClass> <ServiceHeader>
#       [SOURCES <src>...]
#       [GENERATED_GROUPS <group>...]
#       [GENERATED_SOURCES <path>...])
#
# 行为:
#   - 生成唯一 main.cpp，包含 ServiceHeader 并实例化模板
#   - add_executable(<target> <generated_main> <SOURCES>)
#   - target_link_libraries PRIVATE mession_netdriver
#   - target_sources 追加 GENERATED_SOURCES
#   - mession_attach_generated_groups 链接 GENERATED_GROUPS
#   - configure_mession_target + add_dependencies(mession_reflection_codegen)
function(mession_add_service TargetName ServiceClass ConfigClass ServiceHeader)
    set(MultiValueArgs SOURCES GENERATED_GROUPS GENERATED_SOURCES EXTRA_GROUPS)
    cmake_parse_arguments(MESSION_SVC "" "" "${MultiValueArgs}" ${ARGN})

    set(GeneratedMain "${CMAKE_BINARY_DIR}/Generated/${TargetName}_main.cpp")
    file(RELATIVE_PATH REL_HEADER "${CMAKE_BINARY_DIR}/Generated" "${CMAKE_CURRENT_SOURCE_DIR}/${ServiceHeader}")
    file(WRITE "${GeneratedMain}"
        "// Auto-generated by mession_add_service — DO NOT EDIT\n"
        "#include \"${REL_HEADER}\"\n"
        "#include \"Servers/App/ServiceMain.h\"\n"
        "\n"
        "int main(int argc, char** argv)\n"
        "{\n"
        "    return MServiceMain::Run<${ServiceClass}, ${ConfigClass}>(argc, argv);\n"
        "}\n"
    )

    add_executable(${TargetName}
        ${GeneratedMain}
        ${MESSION_SVC_SOURCES}
    )
    target_link_libraries(${TargetName}
        PRIVATE
            mession_netdriver
    )
    configure_mession_target(${TargetName})

    if(MESSION_SVC_GENERATED_GROUPS)
        mession_attach_generated_groups(${TargetName} ${MESSION_SVC_GENERATED_GROUPS})
    endif()

    if(MESSION_SVC_GENERATED_SOURCES)
        target_sources(${TargetName}
            PRIVATE
                ${MESSION_SVC_GENERATED_SOURCES}
        )
    endif()

    if(MESSION_SVC_EXTRA_GROUPS)
        mession_attach_generated_groups(${TargetName} ${MESSION_SVC_EXTRA_GROUPS})
    endif()

    add_dependencies(${TargetName} mession_reflection_codegen)
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

# 1. Gateway Server - 网关服务器
mession_add_service(GatewayServer MGatewayServer SGatewayConfig Source/Servers/Gateway/GatewayServer.h
    SOURCES
        Source/Servers/Gateway/GatewayServer.cpp
    GENERATED_GROUPS shared gateway
    GENERATED_SOURCES
        ${MESSION_GENERATED_DIR}/MClientManifest.mgenerated.cpp
)

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
mession_add_service(EchoService MEchoService SEchoServiceConfig Source/Servers/EchoService/EchoService.h
    SOURCES
        Source/Servers/EchoService/EchoService.cpp
    GENERATED_GROUPS shared sample
    GENERATED_SOURCES
        ${MESSION_GENERATED_DIR}/FSampleEchoRequest.mgenerated.cpp
        ${MESSION_GENERATED_DIR}/FSampleEchoResponse.mgenerated.cpp
    EXTRA_GROUPS echoservice
)

# 8. MServiceRegistry - 服务发现集中管理进程（PoC 阶段纯内存）
mession_add_service(MServiceRegistry MServiceRegistry SServiceRegistryConfig Source/Servers/ServiceRegistry/ServiceRegistryServer.h
    SOURCES
        Source/Servers/ServiceRegistry/ServiceRegistryServer.cpp
    GENERATED_GROUPS shared serviceregistry
)

# MClientManifest generated definitions — linked into EchoService.
add_dependencies(EchoService mession_reflection_codegen)
target_sources(EchoService PRIVATE
    ${MESSION_GENERATED_DIR}/MClientManifest.mgenerated.cpp
)

# NetBench - 简单网络压测工具
add_executable(NetBench
    Source/Tools/NetBench.cpp
)
target_link_libraries(NetBench
    PRIVATE
        mession_core
)
configure_mession_compile_options(NetBench)

# LogTest - 日志模块单元测试（Task 1 起；后续 Task 追加测试 TU）
# main.cpp #includes TestMpscRingBuffer.cpp directly (see file comment for rationale);
# do not add TestMpscRingBuffer.cpp to the source list to avoid ODR-multiple-definition.
add_executable(LogTest
    Source/Common/Runtime/Log/Tests/main.cpp
)
target_include_directories(LogTest
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)
target_link_libraries(LogTest
    PRIVATE
        mession_common
)
configure_mession_compile_options(LogTest)

# MAsyncTest - P1 async primitives unit tests (MAsyncContext + Get redline)
add_executable(MAsyncTest
    Source/Common/Runtime/Async/Tests/main.cpp
)
target_include_directories(MAsyncTest
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)
target_link_libraries(MAsyncTest
    PRIVATE
        mession_common
)
configure_mession_compile_options(MAsyncTest)

# InnerTypeTest - SFutureResult<T>::InnerType 编译期契约测试
add_executable(InnerTypeTest
    Source/Common/Runtime/Async/Tests/InnerTypeTest.cpp
)
target_include_directories(InnerTypeTest
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)
target_link_libraries(InnerTypeTest
    PRIVATE
        mession_common
)
configure_mession_compile_options(InnerTypeTest)

# AwaitableTest - TAwaitable<...> 4 形态 + R 推导编译期契约测试
add_executable(AwaitableTest
    Source/Common/Runtime/Async/Tests/AwaitableTest.cpp
)
target_include_directories(AwaitableTest
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)
target_link_libraries(AwaitableTest
    PRIVATE
        mession_common
)
configure_mession_compile_options(AwaitableTest)

# AsyncFrameTest - P3 vertical-slice unit test for the MHeaderTool-generated
# AsyncFrame. Verifies the auto-generated `MHeaderTool_AsyncFrame_<Class>_<Func>`
# struct's AwaitOk pass-through semantics, AwaitedSlot snapshot, and StoredValue
# capture — without standing up a real network. The Frame struct is generated
# into `<Class>_AsyncFrames.h` (one-way include from the user header), which
# the test pulls in directly.
add_executable(AsyncFrameTest
    Source/Servers/EchoService/Tests/main.cpp
)
target_include_directories(AsyncFrameTest
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${MESSION_GENERATED_DIR}
)
target_link_libraries(AsyncFrameTest
    PRIVATE
        mession_common
        mession_netdriver
)
configure_mession_compile_options(AsyncFrameTest)
add_dependencies(AsyncFrameTest mession_reflection_codegen)
mession_attach_generated_groups(AsyncFrameTest shared)

# FreeAsyncFuncGenTest - P4 vertical-slice unit test for free-function
# MFUNCTION(Async) Frame generation. Verifies ProcessFreeFunctions scans
# namespace-scope MFUNCTION(Async), rejects transport-tag combinations,
# and EmitFreeAsyncFramesHeader emits the expected struct.
add_executable(FreeAsyncFuncGenTest
    Source/Tools/MHeaderTool/Tests/FreeAsyncFuncGenTest.cpp
)
target_include_directories(FreeAsyncFuncGenTest
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)
target_link_libraries(FreeAsyncFuncGenTest
    PRIVATE
        mht_lib
        mession_core
)
configure_mession_compile_options(FreeAsyncFuncGenTest)

# -----------------------------------------------------------------------
# Optional examples
# -----------------------------------------------------------------------
# BUILD_ASYNC_DEMO: builds Source/Examples/AsyncDemo (end-to-end C++17 async
# example, two MFUNCTION(Async) free funcs + AsyncDemoTest smoke). Default
# ON so the example is visible; set OFF to fully decouple.
option(BUILD_ASYNC_DEMO "Build the AsyncDemo end-to-end C++17 async example" ON)
if(BUILD_ASYNC_DEMO)
    add_subdirectory(Source/Examples/AsyncDemo)
endif()

# 安装目标
install(TARGETS
    GatewayServer
    MServiceRegistry
    DESTINATION bin
)

message(STATUS "=====================================")
message(STATUS "  Mession - Distributed MMO Server")
message(STATUS "=====================================")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Servers: Gateway, EchoService, MServiceRegistry (homogeneous multi-process PoC)")
message(STATUS "=====================================")
