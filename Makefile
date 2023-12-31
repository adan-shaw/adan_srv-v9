# 更换不同的lua 版本(最贴切是lua 5.4.3, lua5.1.5/luajit 需要做一点调整)
lua_home=lua/lua-5.4.3
#lua_home=lua/LuaJIT-2.1.0-beta3/src
#lua_home=lua/lua-5.1.5/src

LIB_DYNAMIC_HEADFILE_PATH += -I$(lua_home)
LIB_DYNAMIC_PATH += -L$(lua_home)
LDFLAGS += -llua51



SRC=\
src/hive.c \
src/hive_cell.c \
src/hive_seri.c \
src/hive_scheduler.c \
src/hive_env.c \
src/hive_cell_lib.c \
src/hive_system_lib.c \
src/hive_socket_lib.c

all :
	echo 'make win or make posix or make macosx'

win : hive/core.dll
posix : hive/core.so
macosx: hive/core.dylib

# linux/freebsd
hive/core.so : $(SRC)
	gcc $(LIB_DYNAMIC_HEADFILE_PATH) -g3 -Wall --shared -fPIC -o $@ $^ -lpthread
	#gcc $(LIB_DYNAMIC_HEADFILE_PATH) -Wall --shared -fPIC -o $@ $^ -lpthread

# win(或许永远不会用到)
LUALIB_MINGW=-I/usr/local/include -L/usr/local/bin -llua51
#LUALIB_MINGW=-I/usr/local/include -L/usr/local/bin -llua52
hive/core.dll : $(SRC)
	gcc $(LIB_DYNAMIC_HEADFILE_PATH) -g3 -Wall --shared -o $@ $^ -lpthread -march=i686 -lws2_32
	#gcc $(LUALIB_MINGW) -g -Wall --shared -o $@ $^ -lpthread -march=i686 -lws2_32

# macos
hive/core.dylib : $(SRC)
	gcc $(LIB_DYNAMIC_HEADFILE_PATH) -g3 -Wall -bundle -undefined dynamic_lookup -fPIC -o $@ $^ -lpthread

clean :
	rm -rf hive/core.dll hive/core.so hive/core.dylib hive/core.dylib.dSYM