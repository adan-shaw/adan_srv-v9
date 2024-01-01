#!/bin/sh

make lua && make posix

lua_home=lua/lua-5.4.6/src
#lua_home=lua/LuaJIT-2.1.0-beta3/src
#lua_home=lua/lua-5.1.5/src

$lua_home/lua ./test.lua

# objdump -T hive/core.so | grep Base
