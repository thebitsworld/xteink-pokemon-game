/* Runs one Lua test script for an app in apps/ against the firmware's own
 * Lua 5.4.7 (lib/Lua). Usage: LuaAppLogicTest <apps dir> <test script>.
 * The script sees the apps directory as the global APPS and fails by raising
 * an error (assert()). */
#include <stdio.h>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s <apps dir> <test.lua>\n", argv[0]);
    return 2;
  }
  lua_State* L = luaL_newstate();
  luaL_openlibs(L);
  lua_pushstring(L, argv[1]);
  lua_setglobal(L, "APPS");
  const int status = luaL_dofile(L, argv[2]);
  if (status != LUA_OK) {
    fprintf(stderr, "%s\n", lua_tostring(L, -1));
    lua_close(L);
    return 1;
  }
  lua_close(L);
  return 0;
}
