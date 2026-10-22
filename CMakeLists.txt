cmake_minimum_required(VERSION 3.15)
project(Mession VERSION 1.0.0 LANGUAGES CXX C)

# compile_commands.json 仅供 IDE、clangd 和离线分析工具使用；构建图本身
# 不应以它是否已生成作为前置条件。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "" FORCE)

include(FetchContent)
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
    if(DEFINED ENV{DEFAULT_BUILD_TYPE})
        set(CMAKE_BUILD_TYPE_INITIAL "$ENV{DEFAULT_BUILD_TYPE}")
    else()
        set(CMAKE_BUILD_TYPE_INITIAL "Release")
    endif()
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

# --- fmt (FetchContent, header-only) ---------------------------------------
set(FMT_HEADER_ONLY ON CACHE BOOL "Use fmt header-only build" FORCE)
set(FMT_DIR "" CACHE PATH "Local fmt source directory (skips FetchContent)")
if(FMT_DIR AND EXISTS "${FMT_DIR}/CMakeLists.txt")
    # 本地源码也走 FetchContent,只是用 SOURCE_DIR 指过去。这里必须仍然调用
    # MakeAvailable: 只设 FMT_SOURCE_DIR 不会创建 fmt::fmt-header-only 目标,
    # 后面的 target_link_libraries 会以"目标不存在"收场(--fmt-dir 因此失效)。
    set(FMT_SOURCE_DIR "${FMT_DIR}")
    FetchContent_Declare(fmt SOURCE_DIR "${FMT_SOURCE_DIR}")
    FetchContent_MakeAvailable(fmt)
else()
    FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG        11.0.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(fmt)
endif()

# fmt 以头文件形式传播到绝大多数目标。把它标记为系统包含目录，避免项目的
# -Wall/-Wextra 把第三方模板实现中的警告混入业务代码的构建输出。
get_target_property(MESSION_FMT_INCLUDE_DIRS fmt-header-only INTERFACE_INCLUDE_DIRECTORIES)
if(MESSION_FMT_INCLUDE_DIRS)
    set_property(TARGET fmt-header-only PROPERTY INTERFACE_INCLUDE_DIRECTORIES "")
    target_include_directories(fmt-header-only SYSTEM INTERFACE ${MESSION_FMT_INCLUDE_DIRS})
endif()
unset(MESSION_FMT_INCLUDE_DIRS)

# Core 库（底层运行时：Common 下的网络/并发/事件循环基础能力）
set(MESSION_CORE_SOURCES
    Source/Common/IO/Socket/SocketPlatform.cpp
    Source/Common/IO/Socket/Socket.cpp
    Source/Common/Runtime/EventLoop/TaskEventLoop.cpp
    Source/Common/Runtime/EventLoop/NetEventLoop.cpp
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
target_link_libraries(mession_core PUBLIC fmt::fmt-header-only)
configure_mession_compile_options(mession_core)

# Common 库（公共组件：日志/RPC/连接等中层能力）
set(MESSION_COMMON_SOURCES
    Source/Common/Runtime/Async/AsyncContext.cpp
    Source/Common/Runtime/Actor/MActorHandle.cpp
    Source/Common/Runtime/Actor/MActorSystem.cpp
    Source/Common/Runtime/Concurrency/SignalHandler.cpp
    Source/Common/Runtime/Concurrency/SubReactorPool.cpp
    Source/Common/Runtime/Concurrency/WorkerPool.cpp
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
    Source/Common/Net/Rpc/RpcManifest.cpp
    Source/Common/Net/Rpc/RpcServerCall.cpp
    Source/Common/Net/Rpc/RpcTransport.cpp
    Source/Common/Net/Rpc/MClientManifest.cpp
    Source/Common/Net/Rpc/MClientDownlinkManifest.cpp
    Source/Common/Net/Rpc/MRpcChannel.cpp
    Source/Common/Runtime/Actor/ActorMember.cpp
    Source/Common/Runtime/Actor/MemberRpcManifest.cpp
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
add_subdirectory(Source/Common/Script/Tests)

# Lua 5.4 + Teal script engine (Task 1 of 2026-08-05 plan)
option(MESSION_USE_LUA "Enable Lua 5.4 + Teal script engine" ON)
if(MESSION_USE_LUA)
    add_subdirectory(Source/ThirdParty/lua)  # vendor Lua 5.4.7 (moved 2026-08-11)
    add_subdirectory(Source/Common/Script/Lua)  # mession_lua impl
    add_subdirectory(Source/Common/Script/Lua/Tests)
endif()

# MHeaderTool - 反射代码生成工具 (模块化版本)
add_subdirectory(Source/Tools/MHeaderTool)

set(MESSION_HEADERTOOL_MANIFEST "${MESSION_GENERATED_DIR}/MHeaderToolTargets.cmake")
file(MAKE_DIRECTORY "${MESSION_GENERATED_DIR}")

set(MESSION_HEADERTOOL_EXE "${CMAKE_SOURCE_DIR}/Bin/MHeaderTool${CMAKE_EXECUTABLE_SUFFIX}")

# 全新检出时 Bin/MHeaderTool 不存在,必须在 configure 期先编出一个宿主工具:
# 否则 Build/Generated/ 为空,MHeaderToolTargets.cmake 里的 MESSION_GENERATED_GROUPS
# 也是空,各 add_executable 会因为找不到 *.mgenerated.cpp 而直接报错。
#
# 这里编的是一个独立子工程(Source/Tools/MHeaderTool/Bootstrap),而不是拼一条编译器
# 命令行——链接 libtooling 必须走 Clang/LLVM 的导入目标,手写 -l 旗标在 Ubuntu 上会因为
# libclang-cpp 只提供 libclang-cpp.so.19.1、没有 .so 别名而失败,写死版本号又会随 LLVM
# 升级漂移。另外 A2 重构把生成器实现搬到了 Generation/*.cpp,旧的单文件命令行只编
# MHeaderTool.cpp,会以 undefined reference 收场(这正是全新检出 configure 失败的直接原因);
# Bootstrap 工程 include 同一份 Sources.cmake,不会再漏编。
#
# 子工程用默认生成器(与顶层生成器无关),所以全新机器上 Ninja 不在 PATH 也不影响引导。
set(MESSION_HEADERTOOL_BOOTSTRAP_DIR "${CMAKE_BINARY_DIR}/MHeaderToolBootstrap")
set(MESSION_HEADERTOOL_BOOTSTRAP_BASENAME "bootstrap_mht${CMAKE_EXECUTABLE_SUFFIX}")
set(MESSION_HEADERTOOL_BOOTSTRAP_EXE
    "${MESSION_HEADERTOOL_BOOTSTRAP_DIR}/${MESSION_HEADERTOOL_BOOTSTRAP_BASENAME}")
# 多配置生成器(VS/Xcode)会把可执行文件放进每配置子目录。
if(CMAKE_CONFIGURATION_TYPES)
    if(NOT CMAKE_BUILD_TYPE)
        list(GET CMAKE_CONFIGURATION_TYPES 0 _mession_bootstrap_config)
    else()
        set(_mession_bootstrap_config "${CMAKE_BUILD_TYPE}")
    endif()
    set(MESSION_HEADERTOOL_BOOTSTRAP_EXE
        "${MESSION_HEADERTOOL_BOOTSTRAP_DIR}/${_mession_bootstrap_config}/${MESSION_HEADERTOOL_BOOTSTRAP_BASENAME}")
    unset(_mession_bootstrap_config)
endif()

# Fresh checkout bootstrap:
# If Bin/MHeaderTool does not exist yet, build a temporary host tool at configure-time
# so generated reflection sources and manifest are available in the first build.
if(NOT EXISTS "${MESSION_HEADERTOOL_EXE}")
    set(_mession_bootstrap_args
        -S "${CMAKE_SOURCE_DIR}/Source/Tools/MHeaderTool/Bootstrap"
        -B "${MESSION_HEADERTOOL_BOOTSTRAP_DIR}"
        "-DMHT_SOURCE_ROOT=${CMAKE_SOURCE_DIR}/Source"
        -DCMAKE_BUILD_TYPE=Release
    )
    # Clang_DIR 由上面 add_subdirectory(Source/Tools/MHeaderTool) 里的
    # find_package(Clang CONFIG) 写进 cache,引导工程复用它,不再二次探测。
    if(Clang_DIR)
        list(APPEND _mession_bootstrap_args "-DClang_DIR=${Clang_DIR}")
    endif()
    # clang 资源头目录同样透传（顶层已由 MHeaderTool 子目录算出），保证引导工具与
    # 正式构建指向同一个内置头目录。
    if(MESSION_CLANG_RESOURCE_DIR_RESOLVED)
        list(APPEND _mession_bootstrap_args
            "-DMESSION_CLANG_RESOURCE_DIR=${MESSION_CLANG_RESOURCE_DIR_RESOLVED}")
    endif()
    # Visual Studio 生成器不接受显式指定 CMAKE_CXX_COMPILER。
    if(NOT MSVC)
        list(APPEND _mession_bootstrap_args "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
    endif()

    message(STATUS "Bootstrapping MHeaderTool (configure-time host tool)...")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" ${_mession_bootstrap_args}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE MESSION_HEADERTOOL_BOOTSTRAP_RESULT
        ERROR_VARIABLE MESSION_HEADERTOOL_BOOTSTRAP_ERROR
        OUTPUT_VARIABLE MESSION_HEADERTOOL_BOOTSTRAP_OUTPUT
    )
    if(MESSION_HEADERTOOL_BOOTSTRAP_RESULT EQUAL 0)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${MESSION_HEADERTOOL_BOOTSTRAP_DIR}"
                    --target bootstrap_mht --parallel "${DEFAULT_JOBS}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE MESSION_HEADERTOOL_BOOTSTRAP_RESULT
            ERROR_VARIABLE MESSION_HEADERTOOL_BOOTSTRAP_BUILD_ERROR
            OUTPUT_VARIABLE MESSION_HEADERTOOL_BOOTSTRAP_BUILD_OUTPUT
        )
    endif()
    if(NOT MESSION_HEADERTOOL_BOOTSTRAP_RESULT EQUAL 0)
        message(WARNING
            "Failed to build configure-time MHeaderTool bootstrap (code ${MESSION_HEADERTOOL_BOOTSTRAP_RESULT}). "
            "Generation will be skipped, so the first build is expected to fail unless "
            "Bin/MHeaderTool already exists. See Scripts/build_from_scratch.sh for a guided "
            "fresh-checkout build.\n"
            "${MESSION_HEADERTOOL_BOOTSTRAP_ERROR}${MESSION_HEADERTOOL_BOOTSTRAP_BUILD_ERROR}"
            "${MESSION_HEADERTOOL_BOOTSTRAP_OUTPUT}${MESSION_HEADERTOOL_BOOTSTRAP_BUILD_OUTPUT}")
    else()
        message(STATUS "MHeaderTool bootstrap ready: ${MESSION_HEADERTOOL_BOOTSTRAP_EXE}")
    endif()
    unset(MESSION_HEADERTOOL_BOOTSTRAP_RESULT)
    unset(MESSION_HEADERTOOL_BOOTSTRAP_ERROR)
    unset(MESSION_HEADERTOOL_BOOTSTRAP_OUTPUT)
    unset(MESSION_HEADERTOOL_BOOTSTRAP_BUILD_ERROR)
    unset(MESSION_HEADERTOOL_BOOTSTRAP_BUILD_OUTPUT)
    unset(_mession_bootstrap_args)
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
    "--client-downlink-manifest=${MESSION_GENERATED_DIR}/MClientDownlinkManifest.mgenerated.cpp"
    "--client-downlink-header=${MESSION_GENERATED_DIR}/MClientDownlinkManifest.mgenerated.h"
    "--member-rpc-manifest=${MESSION_GENERATED_DIR}/MemberRpcManifest.mgenerated.cpp"
    "--lua-stdlib-hint-lua=${MESSION_GENERATED_DIR}/Mession.lua"
    "--lua-stdlib-hint-teal=${MESSION_GENERATED_DIR}/Mession.d.tl"
)

# manifest 决定初始生成源文件和目标图。仅在它尚不存在时于 configure 阶段生成；
# 之后把工作交给下方唯一的 build-time custom command。此前每次 configure 都扫描
# 全部 Source，而任一源码内容变化又会强制 configure，导致一次 build 内重复跑多遍
# MHeaderTool，并把所有生成源的时间戳都刷新。
if(NOT EXISTS "${MESSION_HEADERTOOL_MANIFEST}" AND EXISTS "${MESSION_HEADERTOOL_RUNNER}")
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

# namespace enum 注册单元编进 mession_common——所有链接 mession_common 的
# 目标(服务/测试)都能用反射查 enum(FindEnum 非空)。
if(TARGET mession_common AND MESSION_GENERATED_ENUM_SOURCES)
    target_sources(mession_common PRIVATE ${MESSION_GENERATED_ENUM_SOURCES})
endif()
# 注册引导(MHeaderTool 生成)——configure 时文件已存在才加(首次 codegen 前不存在)
if(TARGET mession_common AND EXISTS "${MESSION_GENERATED_DIR}/MEnumRegistration.mgenerated.cpp")
    target_sources(mession_common PRIVATE ${MESSION_GENERATED_DIR}/MEnumRegistration.mgenerated.cpp)
endif()
# 成员 RPC 注册表(MHeaderTool 生成)——框架成员分发器查表用,编进 common
if(TARGET mession_common AND EXISTS "${MESSION_GENERATED_DIR}/MemberRpcManifest.mgenerated.cpp")
    target_sources(mession_common PRIVATE ${MESSION_GENERATED_DIR}/MemberRpcManifest.mgenerated.cpp)
endif()

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
    target_link_libraries(${TargetName}
        PUBLIC fmt::fmt-header-only
    )
    configure_mession_compile_options(${TargetName})
endfunction()

foreach(GroupName IN LISTS MESSION_GENERATED_GROUPS)
    mession_define_generated_group("${GroupName}")
endforeach()

# Protocol 消息 round-trip 测试(#5 协议样例)——消息注册单元经 shared 组
# (OBJECT 库全量链接),静态库用 --start-group 解决循环引用。
add_executable(ProtocolMessagesTest
    Source/Protocol/Tests/ProtocolMessagesTest.cpp
)
target_include_directories(ProtocolMessagesTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Source
    ${MESSION_GENERATED_DIR}
)
target_link_libraries(ProtocolMessagesTest PRIVATE
    mession_generated_shared
    -Wl,--start-group mession_netdriver mession_common mession_core -Wl,--end-group
)
configure_mession_compile_options(ProtocolMessagesTest)
add_dependencies(ProtocolMessagesTest mession_reflection_codegen)

# MemberFrameworkTest — F0 ActorMember 框架单测(继承序列化 / GMemberRpcEntries
# 查表 / 成员挂载+GetActorMember / 框架成员分发 round-trip / FindActorMember)。
# 注意:不链接整个 mession_generated_echo/player 组(会带入服务实现符号)。
# 这里显式编入需要的生成单元(MPlayerItemContainer/MPlayerActor/
# FMemberDispatchRequest)与成员实现 MPlayerItemContainer.cpp;协议消息
# FPlayer* 在 shared 组。
add_executable(MemberFrameworkTest
    Source/Common/Runtime/Actor/Tests/MemberFrameworkTest.cpp
    Source/Servers/Player/Members/MPlayerItemContainer.cpp
    ${MESSION_GENERATED_DIR}/MPlayerItemContainer.mgenerated.cpp
    ${MESSION_GENERATED_DIR}/MPlayerActor.mgenerated.cpp
    ${MESSION_GENERATED_DIR}/FMemberDispatchRequest.mgenerated.cpp
)
target_include_directories(MemberFrameworkTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Source
    ${MESSION_GENERATED_DIR}
)
target_link_libraries(MemberFrameworkTest PRIVATE
    mession_generated_shared
    -Wl,--start-group mession_netdriver mession_common mession_core -Wl,--end-group
)
configure_mession_compile_options(MemberFrameworkTest)
add_dependencies(MemberFrameworkTest mession_reflection_codegen)

# configure 阶段刷新 manifest 以建立正确目标图；构建阶段仍以 stamp 和输入
# 时间戳保持生成物与最终编译出的 MHeaderTool 同步。
file(GLOB_RECURSE MESSION_HEADERTOOL_INPUTS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/Source/*.h"
    "${CMAKE_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_SOURCE_DIR}/Source/*.c"
)
# GLOB CONFIGURE_DEPENDS 只负责发现文件的增删。不要把每个文件内容都加入
# CMAKE_CONFIGURE_DEPENDS：普通内容修改应由下面的 custom command 增量处理，而不是
# 在每个 target 的 check-build-system 阶段重新 configure / 全量扫描。
set(MESSION_HEADERTOOL_STAMP "${MESSION_GENERATED_DIR}/MHeaderTool.stamp")
set(MESSION_HEADERTOOL_BUILD_CMD
    "$<TARGET_FILE:MHeaderTool>"
    "--source-root=${CMAKE_SOURCE_DIR}/Source"
    "--output-dir=${MESSION_GENERATED_DIR}"
    "--cmake-manifest=${MESSION_HEADERTOOL_MANIFEST}"
    "--client-manifest=${MESSION_GENERATED_DIR}/MClientManifest.mgenerated.cpp"
    "--client-downlink-manifest=${MESSION_GENERATED_DIR}/MClientDownlinkManifest.mgenerated.cpp"
    "--client-downlink-header=${MESSION_GENERATED_DIR}/MClientDownlinkManifest.mgenerated.h"
    "--member-rpc-manifest=${MESSION_GENERATED_DIR}/MemberRpcManifest.mgenerated.cpp"
    "--lua-stdlib-hint-lua=${MESSION_GENERATED_DIR}/Mession.lua"
    "--lua-stdlib-hint-teal=${MESSION_GENERATED_DIR}/Mession.d.tl"
)
add_custom_command(
    OUTPUT "${MESSION_HEADERTOOL_STAMP}"
    COMMAND ${MESSION_HEADERTOOL_BUILD_CMD}
    COMMAND "${CMAKE_COMMAND}" -E touch "${MESSION_HEADERTOOL_STAMP}"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS MHeaderTool ${MESSION_HEADERTOOL_INPUTS}
    COMMENT "Generating reflection glue with MHeaderTool"
    VERBATIM
)
add_custom_target(mession_reflection_codegen DEPENDS "${MESSION_HEADERTOOL_STAMP}")

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

# 仅在内容变化时更新 configure 期产生的源码。file(WRITE) 每次 configure 都会
# 刷新 mtime，导致各 Service 的 main.cpp 即使文本相同也被重新编译。
function(mession_write_file_if_different OutputPath Content)
    if(EXISTS "${OutputPath}")
        file(READ "${OutputPath}" ExistingContent)
        if(ExistingContent STREQUAL Content)
            return()
        endif()
    endif()
    file(WRITE "${OutputPath}" "${Content}")
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
    string(CONCAT GeneratedMainContent
        "// Auto-generated by mession_add_service — DO NOT EDIT\n"
        "#include \"${REL_HEADER}\"\n"
        "#include \"Servers/App/ServiceMain.h\"\n"
        "\n"
        "int main(int argc, char** argv)\n"
        "{\n"
        "    return MServiceMain::Run<${ServiceClass}, ${ConfigClass}>(argc, argv);\n"
        "}\n"
    )
    mession_write_file_if_different("${GeneratedMain}" "${GeneratedMainContent}")

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

# 1. Gateway Server - 网关服务器
mession_add_service(GatewayServer MGatewayServer SGatewayConfig Source/Servers/Gateway/GatewayServer.h
    SOURCES
        Source/Servers/Gateway/GatewayServer.cpp
    GENERATED_GROUPS shared gateway
    GENERATED_SOURCES
        ${MESSION_GENERATED_DIR}/MClientManifest.mgenerated.cpp
        ${MESSION_GENERATED_DIR}/MClientDownlinkManifest.mgenerated.cpp
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
        Source/Servers/EchoService/MRankListActor.cpp
    GENERATED_GROUPS shared sample
    GENERATED_SOURCES
        ${MESSION_GENERATED_DIR}/FSampleEchoRequest.mgenerated.cpp
        ${MESSION_GENERATED_DIR}/FSampleEchoResponse.mgenerated.cpp
        ${MESSION_GENERATED_DIR}/MEchoService_AwaitImpl.mgenerated.cpp
    EXTRA_GROUPS echoservice
)

# 7b. PlayerService - Player 业务进程(2026-08-14 player-design §2):
#     零业务协议 + actor 集合;业务在 ActorMember 上,进程接收走框架成员分发器。
mession_add_service(PlayerService MPlayerService SPlayerServiceConfig Source/Servers/Player/PlayerService.h
    SOURCES
        Source/Servers/Player/PlayerService.cpp
        Source/Servers/Player/Members/MPlayerItemContainer.cpp
    GENERATED_GROUPS shared
    EXTRA_GROUPS player
)

# 8. MServiceRegistry - 服务发现集中管理进程（PoC 阶段纯内存）
mession_add_service(MServiceRegistry MServiceRegistry SServiceRegistryConfig Source/Servers/ServiceRegistry/ServiceRegistryServer.h
    SOURCES
        Source/Servers/ServiceRegistry/ServiceRegistryServer.cpp
    GENERATED_GROUPS shared serviceregistry
    GENERATED_SOURCES
        ${MESSION_GENERATED_DIR}/SServiceRegistryConfig.mgenerated.cpp
)

# MClientManifest generated definitions — linked into EchoService.
add_dependencies(EchoService mession_reflection_codegen)
target_sources(EchoService PRIVATE
    ${MESSION_GENERATED_DIR}/MClientManifest.mgenerated.cpp
    ${MESSION_GENERATED_DIR}/MClientDownlinkManifest.mgenerated.cpp
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

# AwaitableTest — P5 类型层：SAwaiter 三方法 + TAwaitable 模板（KD-1 / KD-2）
# AwaitableTest — P5 类型层：SAwaiter 三方法 + TAwaitable 模板（KD-1 / KD-2）
add_executable(AwaitableTest
    Source/Common/Runtime/Async/Tests/AwaitableTestMain.cpp
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

# StringUtilsTest - MFormat / MStringBuilder unit tests
# StringUtilsTestMain.cpp #includes TestStringUtilsFormat.cpp directly (see file
# comment for rationale); do not add TestStringUtilsFormat.cpp to the source list
# to avoid ODR-multiple-definition.
add_executable(StringUtilsTest
    Source/Common/Runtime/Tests/StringUtilsTestMain.cpp
)
target_include_directories(StringUtilsTest
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)
target_link_libraries(StringUtilsTest
    PRIVATE
        mession_common
)
configure_mession_compile_options(StringUtilsTest)

# InnerTypeTest - SFutureResult<T>::InnerType 编译期契约测试
# Source file removed; temporarily disabled
# add_executable(InnerTypeTest
#     Source/Common/Runtime/Async/Tests/InnerTypeTest.cpp
# )
# target_include_directories(InnerTypeTest
#     PRIVATE
#         ${CMAKE_CURRENT_SOURCE_DIR}
#         ${CMAKE_CURRENT_SOURCE_DIR}/Source
# )
# target_link_libraries(InnerTypeTest
#     PRIVATE
#         mession_common
# )
# configure_mession_compile_options(InnerTypeTest)

# AwaitableTest - TAwaitable<...> 4 形态 + R 推导编译期契约测试
# Source file removed; temporarily disabled
# add_executable(AwaitableTest
#     Source/Common/Runtime/Async/Tests/AwaitableTest.cpp
# )
# target_include_directories(AwaitableTest
#     PRIVATE
#         ${CMAKE_CURRENT_SOURCE_DIR}
#         ${CMAKE_CURRENT_SOURCE_DIR}/Source
# )
# target_link_libraries(AwaitableTest
#     PRIVATE
#         mession_common
# )
# configure_mession_compile_options(AwaitableTest)


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
enable_testing()
add_subdirectory(Source/Examples/AwaitDemo)
endif()

# 安装目标
install(TARGETS
    GatewayServer
    MServiceRegistry
    DESTINATION bin
)

# *.Async.cpp IDE 索引(自动):每次构建把 codegen 专用源合并进 compile_commands.json。
# 背景:*.Async.cpp 业务编译不编译(见 spec 2026-07-24 §7.2.1),CMake 不为其生成
# 编译命令 → 基于 compile_commands 的 IDE(clangd/VS Code/CLion)不索引、无补全。
# 脚本生成与 MHeaderTool fallback 一致的命令(-fsyntax-only -x c++ -I -D...),幂等。
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_custom_target(update_async_compile_commands ALL
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_SOURCE_DIR}/Scripts/merge_async_compile_commands.py
                    ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/Source
            COMMENT "Merging *.Async.cpp into compile_commands.json (IDE indexing)"
            VERBATIM)
    endif()
endif()

message(STATUS "=====================================")
message(STATUS "  Mession - Distributed MMO Server")
message(STATUS "=====================================")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Servers: Gateway, EchoService, MServiceRegistry (homogeneous multi-process PoC)")
message(STATUS "=====================================")
