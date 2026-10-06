# Logic tests for the Lua apps in apps/, run with the firmware's own Lua.
file(GLOB LUA_CORE_SOURCES "${REPO_ROOT}/lib/Lua/src/*.c")
add_library(lua_host STATIC ${LUA_CORE_SOURCES})
target_include_directories(lua_host PUBLIC "${REPO_ROOT}/lib/Lua/src")
target_compile_options(lua_host PRIVATE -w)
target_link_libraries(lua_host PUBLIC m)

add_executable(LuaAppLogicTest lua_test_runner.c)
target_link_libraries(LuaAppLogicTest PRIVATE lua_host)

foreach(app minesweeper connectfour solitaire knucklebones yacht nonogram checkers seabattle hearts hex go)
  add_test(NAME LuaAppLogic_${app}
           COMMAND LuaAppLogicTest "${REPO_ROOT}/apps" "${CMAKE_CURRENT_SOURCE_DIR}/${app}_test.lua")
endforeach()
