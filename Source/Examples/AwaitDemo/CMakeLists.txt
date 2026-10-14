# AwaitDemo — await 状态机框架端到端演示（C# async 模型）
# AwaitDemoCompute 的实现由 MHeaderTool 生成（AwaitDemo_FreeAwaitImpl.mgenerated.cpp，
# 含业务逻辑段，自包含），作为独立编译单元链接；业务 .cpp 不 include 生成文件。

# 生成物就绪性守卫。
#
# MHeaderTool 只有在能读到 compile_commands.json 时才会去解析 *.Async.cpp（await 业务体），
# 而 compile_commands.json 是本工程 generate 阶段才写出来的——全新检出的第一次 configure
# 必然还没有它。此时生成的 await 产物不完整，下面两个目标会因为源文件不存在而让整个
# configure 失败，进而连 compile_commands.json 都拿不到，形成死锁。
#
# 所以第一次 configure 先跳过它们：configure 得以完成 → 写出 compile_commands.json →
# 再跑一次 configure 时 MHeaderTool 就能产出全部 await 产物，这两个目标自动出现。
# Scripts/build_from_scratch.sh 会自动跑第二遍。
set(MESSION_AWAITDEMO_GENERATED_SOURCES
    "${MESSION_GENERATED_DIR}/AwaitDemo_Async_FreeAwaitImpl.mgenerated.cpp"
    "${MESSION_GENERATED_DIR}/ComplexDemo_Async_FreeAwaitImpl.mgenerated.cpp"
    "${MESSION_GENERATED_DIR}/MemberDemo_Async_FreeAwaitImpl.mgenerated.cpp"
    "${MESSION_GENERATED_DIR}/MemberService_AwaitImpl.mgenerated.cpp"
    "${MESSION_GENERATED_DIR}/BranchDemo_Async_FreeAwaitImpl.mgenerated.cpp"
    "${MESSION_GENERATED_DIR}/StringDemo_Async_FreeAwaitImpl.mgenerated.cpp"
)

set(MESSION_AWAITDEMO_READY TRUE)
set(MESSION_AWAITDEMO_MISSING "")
foreach(_AwaitGenerated IN LISTS MESSION_AWAITDEMO_GENERATED_SOURCES)
    if(NOT EXISTS "${_AwaitGenerated}")
        set(MESSION_AWAITDEMO_READY FALSE)
        list(APPEND MESSION_AWAITDEMO_MISSING "${_AwaitGenerated}")
    endif()
endforeach()
unset(_AwaitGenerated)

if(NOT MESSION_AWAITDEMO_READY)
    message(STATUS
        "AwaitDemo / AwaitCodegenTest 暂缓注册: await 生成物尚未就绪,再跑一次 configure 即可。"
        "缺失: ${MESSION_AWAITDEMO_MISSING}")
    return()
endif()

add_executable(AwaitDemo
    AwaitDemo.cpp
    ComplexDemo.cpp
    MemberDemo.cpp
    BranchDemo.cpp
    StringDemo.cpp
    ${MESSION_AWAITDEMO_GENERATED_SOURCES}
)
target_include_directories(AwaitDemo
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/Source
        ${MESSION_GENERATED_DIR}
)
target_link_libraries(AwaitDemo
    PRIVATE
        mession_common
)
configure_mession_compile_options(AwaitDemo)
add_dependencies(AwaitDemo mession_reflection_codegen)

# AwaitCodegenTest — await 状态机 codegen 系统化测试（回归保护）
# 覆盖 14 个形态（单/多 await、循环、真异步、并发、类成员、分支全套、嵌套）。
# 实现由 MHeaderTool 生成（codegen Impl），测试只断言结果值。
add_executable(AwaitCodegenTest
    Tests/AwaitCodegenTestMain.cpp
    BranchDemo.cpp
    ComplexDemo.cpp
    MemberDemo.cpp
    StringDemo.cpp
    ${MESSION_GENERATED_DIR}/BranchDemo_Async_FreeAwaitImpl.mgenerated.cpp
    ${MESSION_GENERATED_DIR}/ComplexDemo_Async_FreeAwaitImpl.mgenerated.cpp
    ${MESSION_GENERATED_DIR}/MemberDemo_Async_FreeAwaitImpl.mgenerated.cpp
    ${MESSION_GENERATED_DIR}/MemberService_AwaitImpl.mgenerated.cpp
    ${MESSION_GENERATED_DIR}/StringDemo_Async_FreeAwaitImpl.mgenerated.cpp
)
target_include_directories(AwaitCodegenTest
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests
        ${CMAKE_SOURCE_DIR}/Source
        ${MESSION_GENERATED_DIR}
)
target_link_libraries(AwaitCodegenTest
    PRIVATE
        mession_common
)
configure_mession_compile_options(AwaitCodegenTest)
add_dependencies(AwaitCodegenTest mession_reflection_codegen)

# 回归入口：ctest -R AwaitCodegen（或 ctest --test-dir Build）
add_test(NAME AwaitCodegen COMMAND AwaitCodegenTest)
