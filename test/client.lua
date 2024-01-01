--装载../cell.lua
local cell = require "cell"

--accepter() 会调用这个client.lua
function cell.main(fd, addr)
	print(addr, "connected")
	local obj = cell.bind(fd)
	cell.fork(function()
		local line = obj:readline "\n"
		--local line = obj:readline(fd)
		obj:write(line)
		obj:disconnect()
		cell.exit()
	end)
end