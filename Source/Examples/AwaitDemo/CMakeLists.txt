# AwaitDemo — await 状态机框架端到端演示（A 形态）
# AwaitDemo.cpp include codegen 生成的 AwaitDemo_FreeAwaitImpl.mgenerated.cpp
#（同 TU 编译，Helper 声明可见）；生成文件由 mession_reflection_codegen 在
# 构建时产出（DEPENDS 保证时序）。
add_executable(AwaitDemo
    AwaitDemo.cpp
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
