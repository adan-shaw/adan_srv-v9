--装载../cell.lua
local cell = require "cell"

cell.command {
	ping = function()
		cell.sleep(1)
		return "pong"
	end,
	pingEx = function(...)
		cell.sleep(1)
		return ...
	end
}

function cell.main(...)
	print("pingpong.lua->cell.main(): pingpong.lua launched")
	return ...
end