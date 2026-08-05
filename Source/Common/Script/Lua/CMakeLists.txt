add_library(mession_lua STATIC
    LuaScriptState.cpp
)
target_include_directories(mession_lua
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/Source
)
target_link_libraries(mession_lua
    PUBLIC
        mession_lua_vendor
        mession_common
)
configure_mession_compile_options(mession_lua)