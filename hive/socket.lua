--装载../cell.lua
local cell = require "cell"
--装载../cell.lua 中的c.socket
local csocket = require "cell.c.socket"
local command = {}
local message = {}
local sockets = {}
local type = type
local pcall = pcall
csocket.init()



--成功返回fd, 失败返回nil [csocket.connect() 总是失败返回nil, 导致不能完成test]
function command.connect(source,addr,port)
	local id = csocket.connect(addr, port)
	print("command.connect(): id=",id)--for test
	if id then
		sockets[id] = source
		return id
	end
end

--listen() 默认addr是: INADDR_ANY(混杂模式)!! 传递c.self = source? (这里貌似报错了)
--成功返回fd, 失败返回nil
function command.listen(source, port)
	local id = csocket.listen(port)
	print("command.connect(): id=",id)--for test
	if id then
		sockets[id] = source
		return id
	end
end

function command.forward(fd, addr)
	local data = sockets[fd]
	sockets[fd] = addr
	local v = nil
	if type(data) == "table" then
		for i=1,#data do
			v = data[i]
			if not pcall(cell.rawsend, addr, 6, v[1], v[2], v[3]) then
				csocket.freepack(v[3])
				message.disconnect(v[1])
			end
		end
	end
end

function message.disconnect(fd)
	if sockets[fd] then
		sockets[fd] = nil
		csocket.close(fd)
	end
end

cell.command(command)
cell.message(message)
cell.dispatch {
	id = 6, -- socket
	dispatch = csocket.send, -- fd, sz, msg
	replace = true,
}

function cell.main()
	local result = {}
	local v = nil
	local c = nil
	while true do
		for i = 1, csocket.poll(result) do
			v = result[i]
			c = sockets[v[1]]
			if c then
				if type(v[3]) == "string" then
					-- accept: listen fd, new fd , ip
					if not pcall(cell.rawsend,c, 6, v[1], v[2], v[3]) then
						message.disconnect(v[1])
					else
						sockets[v[2]] = {}
					end
				elseif type(c) == "table" then
					table.insert(c, {v[1],v[2],v[3]})
				else
					-- forward: fd , size , message
					if not pcall(cell.rawsend,c, 6, v[1], v[2], v[3]) then
						csocket.freepack(v[3])
						message.disconnect(v[1])
					end
				end
			else
				csocket.freepack(v[3])
			end
		end
		cell.sleep(0)
	end
end