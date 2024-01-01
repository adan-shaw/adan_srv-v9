--装载../cell.lua
local cell = require "cell"

--cell 接收客户端请求, 然后调用./test/client.lua, 并传递参数: fd, addr
local function accepter(fd, addr, listen_fd)
	print("Accept from ", listen_fd)
	-- can't read fd in this function, because socket.cell haven't forward data from fd
	local client = cell.cmd("launch", "test.client", fd, addr)
	-- return cell the data from fd will forward to, you can also return nil for forwarding to self
	return client
end

function cell.main()
	--打印cell table 有那些方法
	print("[cell main]",cell.self)

	-- save listen_fd for prevent gc.
	cell.listen("127.0.0.1:8888",accepter)
	cell.sleep(100)

	--[cell.cmd] - echo
	print(cell.cmd("echo","server: Hello world"))

	--[cell.cmd] - launch调用: ./test/pingpong.lua, 传递参数="cell.main() values"到cell.main()
	local ping, pong = cell.cmd("launch", "test.pingpong","cell.main() values")
	--打印launch 调用的返回值
	print(ping,pong)

	--[cell.call] - 调用./test/pingpong.lua 中, cell.command {} 中的ping元素
	print(cell.call(ping, "ping"))
	--[cell.call] - 调用./test/pingpong.lua 中, cell.command {} 中的pingEx 元素(这个{}中, 一个元素 = 一个方法, 都可以cell.call()), 并传递参数"pingEx() values"
	print(cell.call(ping, "pingEx", "pingEx() values"))

	--创建fork 子进程(实际上是coroutine协程), 9秒后杀死所有cell.call,ping 
	cell.fork(function()
		-- kill ping after 9 second (100 = 1 second, 1 = 1ms)
		cell.sleep(900)
		cell.cmd("kill",ping) end
	)

	cell.fork(function()
		--休息一秒后, 启动一个connect() 到server, 测试是否可以成功connect()
		cell.sleep(100)
		local sock = cell.connect("localhost", 8888)
		local line = sock:readline(fd)
		print(line)
		sock:write(line .. "\n") end
	)

	--调用10 次cell.call,ping
	for i=1,10 do
		--pcall() - 调用./test/pingpong.lua 中, cell.command {} 中的pingEx 元素(这个{}中, 一个元素 = 一个方法, 都可以pcall()), 并传递参数"pingEx() values"
		print(pcall(cell.call,ping, "pingEx", "pingEx() values[pcall]"))
		cell.sleep(100)
		print("pcall(cell.call,ping, \"pingEx\", \"pingEx() values[pcall]\"):",i)
	end

	--退出
	cell.exit()
end