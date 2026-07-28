# MHeaderTool - 反射代码生成工具
# 模块化重构版本 (P4: split into library + executable + test)
#
# Test target (FreeAsyncFuncGenTest) lives in /root/Mession/CMakeLists.txt
# alongside AsyncFrameTest — that is the project's established pattern for
# test executable declarations.

# Library TU — contains ProcessFreeFunctions (extracted from MHeaderTool.cpp
# so tests can link it without colliding with MHeaderTool.cpp's main()).
add_library(mht_lib STATIC
    MHeaderToolLib.cpp
)
target_link_libraries(mht_lib PUBLIC
    mession_core
)
target_include_directories(mht_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Source
)
configure_mession_compile_options(mht_lib)
target_compile_options(mht_lib PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/GR->
)

# Executable: thin wrapper holding main()
add_executable(MHeaderTool
    MHeaderTool.cpp
)
target_link_libraries(MHeaderTool PRIVATE
    mht_lib
)
target_include_directories(MHeaderTool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Source
)
configure_mession_compile_options(MHeaderTool)
target_compile_options(MHeaderTool PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/GR->
)