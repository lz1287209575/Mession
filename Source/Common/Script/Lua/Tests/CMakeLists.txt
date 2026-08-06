add_executable(LuaTypeBridgeTest
    TestLuaTypeBridge.cpp
)
add_executable(LuaCoroutineTest
    TestLuaCoroutine.cpp
)

target_include_directories(LuaTypeBridgeTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/Source
        ${CMAKE_SOURCE_DIR}/Source/Common/Script/lua/src
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(LuaCoroutineTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/Source
        ${CMAKE_SOURCE_DIR}/Source/Common/Script/lua/src
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(LuaTypeBridgeTest
    PRIVATE
        mession_lua
        mession_lua_vendor
        mession_common
)
target_link_libraries(LuaCoroutineTest
    PRIVATE
        mession_lua
        mession_lua_vendor
        mession_common
)

configure_mession_compile_options(LuaTypeBridgeTest)
configure_mession_compile_options(LuaCoroutineTest)