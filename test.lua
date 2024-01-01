--添加*.so 搜索路径(可以暂时弃用这句话)
--package.cpath = package.cpath .. ";./?.dylib"
--package.cpath = package.cpath .. ";./?.so;./hive/?.so"

--装载./hive.lua
local hive = require "hive"

--启动4 个线程, 执行./test/main.lua
hive.start {
	thread = 4,
	main = "test.main",
}
