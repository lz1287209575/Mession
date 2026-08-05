add_executable(ScriptAbstractTest
    TestScriptAbstract.cpp
)
target_include_directories(ScriptAbstractTest
    PRIVATE ${CMAKE_SOURCE_DIR}/Source
)
target_link_libraries(ScriptAbstractTest
    PRIVATE mession_script_abstract mession_common
)
target_compile_options(ScriptAbstractTest PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
)