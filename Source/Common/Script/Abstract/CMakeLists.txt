add_library(mession_script_abstract STATIC
    EScriptLanguage.h
    EReloadMode.h
    EReloadResult.h
    SScriptEngineConfig.h
    TVariant.h
    TVariant.cpp
    ScriptErrorCodes.h
    IScriptModule.h
    IScriptEngine.h
    IScriptRepl.h
)

target_include_directories(mession_script_abstract
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_SOURCE_DIR}/Source
)

target_link_libraries(mession_script_abstract
    PUBLIC mession_common
)

target_compile_options(mession_script_abstract PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)