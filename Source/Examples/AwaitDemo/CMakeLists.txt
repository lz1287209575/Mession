# AwaitDemo — await 状态机框架端到端演示（C# async 模型）
# AwaitDemoCompute 的实现由 MHeaderTool 生成（AwaitDemo_FreeAwaitImpl.mgenerated.cpp，
# 含业务逻辑段，自包含），作为独立编译单元链接；业务 .cpp 不 include 生成文件。
add_executable(AwaitDemo
    AwaitDemo.cpp
    ${MESSION_GENERATED_DIR}/AwaitDemo_FreeAwaitImpl.mgenerated.cpp
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
