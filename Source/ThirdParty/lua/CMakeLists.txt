# Lua 5.4.7 vendor — 编译为 STATIC lib,不出 lua.c / luac.c
file(GLOB LUA_VENDOR_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
# 移除主入口
list(REMOVE_ITEM LUA_VENDOR_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/lua.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/luac.c"
)

add_library(mession_lua_vendor STATIC ${LUA_VENDOR_SOURCES})
set_target_properties(mession_lua_vendor PROPERTIES LINKER_LANGUAGE C)
target_include_directories(mession_lua_vendor
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(mession_lua_vendor
    PUBLIC LUA_COMPAT_5_3
)
target_compile_options(mession_lua_vendor PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
)