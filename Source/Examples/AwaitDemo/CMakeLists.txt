# AwaitDemo — await 状态机框架端到端演示（C# async 模型）
# AwaitDemoCompute 的实现由 MHeaderTool 生成（AwaitDemo_FreeAwaitImpl.mgenerated.cpp，
# 含业务逻辑段，自包含），作为独立编译单元链接；业务 .cpp 不 include 生成文件。
add_executable(AwaitDemo
    AwaitDemo.cpp
    ComplexDemo.cpp
    MemberDemo.cpp
    BranchDemo.cpp
    ${MESSION_GENERATED_DIR}/AwaitDemo_FreeAwaitImpl.mgenerated.cpp
    ${MESSION_GENERATED_DIR}/ComplexDemo_FreeAwaitImpl.mgenerated.cpp
    ${MESSION_GENERATED_DIR}/MemberService_AwaitImpl.mgenerated.cpp
    ${MESSION_GENERATED_DIR}/BranchDemo_FreeAwaitImpl.mgenerated.cpp
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
    ${MESSION_GENERATED_DIR}/BranchDemo_FreeAwaitImpl.mgenerated.cpp
    ${MESSION_GENERATED_DIR}/ComplexDemo_FreeAwaitImpl.mgenerated.cpp
    ${MESSION_GENERATED_DIR}/MemberService_AwaitImpl.mgenerated.cpp
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
