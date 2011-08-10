do
	local memcache = require("memcache")
	local net = require("sockets-lua")
	local newblob = require("blob-lua") 		
	--NOTE: calls to blob:free() are optional (blobs are GC'ed); they are simply present to avoid creating unnessecary memory pressure when it convenient to do so
	
	function proto_text_parseresult(result, expected)
		local keyword
		local message
		
		keyword = result:match("^(%u+)\r\n")
		if keyword then
			message = keyword
		else
			keyword, message = result:match("^(%u+) (.+)\r\n")
			if not keyword then
				error("memcache protocol violation detected")
			end
			message = (keyword .. " " .. message)
		end
		
		if keyword == "ERROR" then
			error("invalid memcache command (" .. message .. ")")
		end
		if keyword == "CLIENT_ERROR" then
			error("memcache protocol violation detected (" .. message .. ")")
		end
		if keyword == "SERVER_ERROR" then
			error("memcache error (" .. message .. ")")
		end
		
		if (
			keyword ~= expected and
			(keyword ~= "NOT_STORED" and (expected == "STORED" or expected == "CAS")) and
			(keyword ~= "EXISTS" and expected == "CAS") and
			(keyword ~= "NOT_FOUND" and (expected == "CAS" or expected == "DELETED"))
		) then
			error("memcache protocol violation detected");
		else
			return message
		end
	end
	
	function proto_text_handleresult(cache, expected)
		local alive
		local result
		
		alive, result = cache.net.recv(cache, -1)
		if not result then
			return "RESPONSE_UNAVAILABLE"
		end
	
		return proto_text_parseresult(tostring(result), expected)
	end
	
	function proto_text_clean(val)
		if type(val) == "number" then
			return tostring(val)
		elseif type(val) == "string" then
			if val:match("[%c%s]") then
				error("text must not contain any spaces or control characters")
			end
			return val
		else
			error("text must be a number or string")
		end
	end
	
	function proto_text_checknum(val, len)
		-- we have no guarantee that lua can handle numbers as large as 2^32 (much less 2^64), so we are ignoring len for now
	
		if val == nil then
			return "0"
		elseif type(val) == "string" then
			val = tonumber(val)
		end
		
		if val < 0 then
			error("number must be non-negative")
		end
		
		return tostring(val)
	end
	
	function proto_text_formatval(val)
		if type(val) == "string" then
			return { len = tostring(#val), val = val }
		elseif type(val) == "number" then
			val = tostring(val)
			return { len = tostring(#val), val = val }
		elseif type(val) == "userdata" then
			return { len = tostring(#val), val = { type = "blob", value = val } }
		end
	end
	
	function proto_text_store(cache, func, key, value, exptime, flags, noreply)
		key = proto_text_clean(key)
		value = proto_text_formatval(value)
		exptime = proto_text_clean(exptime or 0)
		flags = proto_text_checknum((flags or 0), 4)
		noreply = ((noreply and " noreply") or nil)
		
		cache.net.send(cache, func, " ", key, " ", flags, " ", exptime, " ", value.len, noreply, "\r\n", value.val, "\r\n")
		
		if not noreply then
			return proto_text_handleresult(cache, "STORED")
		end
	end
	
	function proto_text_get(cache, gets, keys)
		local writeops = { }
		local results = { }
		local alive
		local data
		local key
		local result
		local size
		
		writeops[1] = ((gets and "gets") or "get")
		for i = 1, #keys do
			writeops[(i * 2)] = " "
			writeops[(i * 2) + 1] = proto_text_clean(keys[i])
		end
		writeops[#writeops + 1] = "\r\n"
		
		cache.net.send(cache, table.unpack(writeops))
		writeops = nil
		
		while true do
			alive, data = cache.net.recv(cache, -1)
			if not data then
				break
			end
			
			data = tostring(data)
			if data:match("^END\r\n$") then
				break
			end
			
			result = { }
			if gets then
				key, result.flags, size, result.unqiue = data:match("^VALUE ([^%c%s]+) (%d+) (%d+) (%d+)\r\n$")
			else
				key, result.flags, size = data:match("^VALUE ([^%c%s]+) (%d+) (%d+)\r\n$")
			end
			
			if key then
				alive, result.value = cache.net.recv(cache, tonumber(size))
				if not result.value then
					break
				end
				alive, data = cache.net.recv(cache, 2)
				if tostring(data) ~= "\r\n" then
					error("memcache protocol violation detected")
				end
				
				results[key] = result
			else
				results.error = proto_text_parseresult(data, nil)
				break
			end
			
			if not alive then
				error("the connection to the remote memcache server has been terminated")
			end
		end
		
		return results
	end
	
	local proto_text =
	{
		get = function(cache, ...)
			return proto_text_get(cache, false, table.pack(...))
		end,
		gets = function(cache, ...)
			return proto_text_get(cache, true, table.pack(...))
		end,
		set = function(cache, key, value, exptime, flags, noreply)
			return proto_text_store(cache, "set", key, value, exptime, flags, noreply)
		end,
		add = function(cache, key, value, exptime, flags, noreply)
			return proto_text_store(cache, "add", key, value, exptime, flags, noreply)
		end,
		replace = function(cache, key, value, exptime, flags, noreply)
			return proto_text_store(cache, "replace", key, value, exptime, flags, noreply)
		end,
		append = function(cache, key, value, noreply)
			return proto_text_store(cache, "append", key, value, "0", "0", noreply)
		end,
		prepend = function(cache, key, value, noreply)
			return proto_text_store(cache, "prepend", key, value, "0", "0", noreply)
		end,
		cas = function(cache, key, unique, value, exptime, flags, noreply)
			key = proto_text_clean(key)
			value = proto_text_formatval(value)
			unique = proto_text_checknum(unique, 8)
			exptime = proto_text_clean(exptime or 0)
			flags = proto_text_checknum((flags or 0), 4)
			noreply = ((noreply and " noreply") or nil)
			
			cache.net.send(cache, "cas ", key, " ", flags, " ", exptime, " ", value.len, " ", tostring(unique), noreply, "\r\n", value.val, "\r\n")
			
			if not noreply then
				return proto_text_handleresult(cache, "CAS")
			end
		end,
		delete = function(cache, key, blocktime, noreply)
			key = proto_text_clean(key)
			blocktime = ((blocktime and (" " .. proto_text_clean(blocktime))) or nil)
			noreply = ((noreply and " noreply") or nil)
			
			cache.net.send(cache, "delete ", key, blocktime, noreply, "\r\n")
			
			if not noreply then
				return proto_text_handleresult(cache, "DELETED")
			end
		end,
		incr = function(cache, key, amount, noreply)
			local alive
			local result
			local value
		
			key = proto_text_clean(key)
			if type(amount) ~= "number" then
				error("invalid amount; expected a number")
			end
			noreply = ((noreply and " noreply") or nil)
		
			if amount >= 0 then
				cache.net.send(cache, "incr ", key, " ", tostring(amount), noreply, "\r\n")
			else
				cache.net.send(cache, "decr ", key, " ", tostring(-1 * amount), noreply, "\r\n")
			end
			
			if not noreply then
				alive, result = cache.net.recv(cache, -1)
				if not result then
					return nil
				end
				result = tostring(result)
				value = result:match("%s*(%d+)%s*\r\n")
				if value then
					return tonumber(value)
				else
					return proto_text_parseresult(result, nil)
				end
			end
		end,
		flushall = function(cache, noreply)
			cache.net.send(cache, "flush_all\r\n")
			if not noreply then
				return proto_text_handleresult(cache, "OK")
			end
		end,
		version = function(cache)
			local alive
			local result
			
			cache.net.send(cache, "version\r\n")
			return proto_text_handleresult(cache, "VERSION")
		end,
		close = function(cache)
			return cache.net.close(cache)
		end
	}
	
	local function closesocket(cache)
		if cache.connected then
			cache.connected = false
			pcall(cache.socket.close, cache.socket)
			cache.socket = nil
		end
	end
	
	local function checkconnected(cache)
		if not cache.connected or not cache.socket then
			error("the connection to the remote memcache server has been terminated")
		end
	end
	
	local net_tcp =
	{
		send = function(cache, ...)
			checkconnected(cache)
			
			local buffer = newblob()
			buffer:write(...)
			cache.socket:send(buffer)
			buffer:free()
		end,
		recv = function(cache, size)
			checkconnected(cache)
		
			if size == -1 then	--read a line
				local buffer = newblob("loose")
				local alive
				local data
				
				while true do
					alive, data = cache.socket:receive(1)
					buffer:write(#buffer, { type = "blob", value = data })
					
					if tostring(data) == "\n" then
						data:free()
						return true, buffer
					else
						data:free()
					end
					
					if not alive then
						closesocket(cache)
						return false, buffer
					end
				end
			else
				return cache.socket:receive(size)
			end
		end,
		close = closesocket
	}
	
	local net_udp_send_header = newblob(6, "tight")
	net_udp_send_header:write(net.tonet(0, "us"), net.tonet(1, "us"), net.tonet(0, "us"))
	
	local function freeblobs(tbl)
		for k, v in pairs(tbl) do
			v:free()
		end
	end
	
	local function recvudpmessage(cache)
		local hasdata
		local data
		local reqid
		local seqnum
		local dataexdgrams
		local datagrams = { }
		local exdgrams = -1
		local selectresult
		
		checkconnected(cache)
		
		while true do
			selectresult = net.select({cache.socket}, nil, nil, cache.timeout)
			if selectresult == nil then	--message reassembly timed out
				freeblobs(datagrams)
				return false
			end
			
			--[[
				we use 65527 as it is the maximum UDP data block allowed by RFC 768
				we allow partial buffer filling for the likely case that the datagram is not completely full
				NOTE: hasdata could technically be false if the 'connection' was gracefully closed
					  we are assuming a space-time rift has not occured, as UDP sockets cannot be gracefully closed by the remote endpoint
			]]
			hasdata, data = cache.socket:receive(65527, true)
			if hasdata then
				reqid, seqnum, dataexdgrams = data:read({ type = "str", len = 2 }, { type = "str", len = 2 }, { type = "str", len = 2 })
				
				if net.fromnet(reqid, true) == cache.curreq then
					seqnum = net.fromnet(seqnum, true)
					dataexdgrams = net.fromnet(dataexdgrams, true)
				
					if exdgrams == -1 then
						exdgrams = dataexdgrams
						if exdgrams == 0 then
							data:free()
							error("memcache UDP protocol violation occured")
						end
					elseif exdgrams ~= dataexdgrams then
						data:free()
						freeblobs(datagrams)
						error("memcache UDP protocol violation occured")
					end
					if seqnum >= exdgrams then
						data:free()
						freeblobs(datagrams)
						error("memcache UDP protocol violation occured")
					end
					if datagrams[seqnum + 1] then
						if datagrams[seqnum + 1] ~= data then
							error("UDP packet data inconsistency detected")
						end
						data:free()
					else
						datagrams[seqnum + 1] = data
					end
					
					--check to see if assembly is complete, we can do this with the # operator as it will stop counting at the first gap
					if #datagrams == exdgrams then
						local writeops = { }
						for i = 1, exdgrams do
							writeops[i] = { type = "blob", value = datagrams[i], start = 8 }
						end
						cache.udpstream:write(table.unpack(writeops))
						freeblobs(datagrams)
						return true
					end
				end
			end
		end
	end
	
	local net_udp =
	{
		send = function(cache, ...)
			checkconnected(cache)
		
			local buffer = newblob()
			buffer:write(net.tonet(cache.reqnum, "us"), { type = "blob", value = net_udp_send_header }, ...)
			
			--[[
				the memcache protocol requires that a request fit into a single datagram
				65507 is the maximum data length of a single UDP frame over IPv4
				(we are ignoring IPv6, as the maximum theroretical gain could only be 20 bytes)
			]]
			if #buffer > 65507 then 		 
				buffer:free()
				error("request is too large to be sent over UDP")
			end
			
			cache.socket:send(buffer)
			buffer:free()
			
			cache.curreq = cache.reqnum
			cache.reqnum = (cache.reqnum + 1)
			if cache.reqnum > 65535 then	--65535 is maximum request ID supported by the memcache protocol
				cache.reqnum = 0
			end
		end,
		recv = function(cache, size)
			if cache.udpstream == nil then
				cache.udpstream = newblob("loose")
				cache.streamempty = true
			end
			
			if size == -1 then
				local searchstart = 1
				local matchstart
			
				while true do
					if not cache.streamempty then
						--size is the offset of the \n from the end of the stream, which is the size of the line
						matchstart, size = tostring(cache.udpstream):find("\n", searchstart, true)
					end
					if cache.streamempty or matchstart == nil then
						searchstart = #cache.udpstream
						if recvudpmessage(cache) then
							cache.streamempty = false
						else
							--the entire stream is bad now :(
							cache.streamempty = true
							cache.udpstream:resize(1, false)	
							return true, nil
						end
					else
						break
					end
				end
			else
				while cache.streamempty or #cache.udpstream < size do
					if recvudpmessage(cache) then
						cache.streamempty = false
					else
						--the entire stream is bad now :(
						cache.streamempty = true
						cache.udpstream:resize(1, false)	
						return true, nil
					end
				end
			end
			
			local result = newblob(size, "tight")
			result:write({ type = "blob", value = cache.udpstream, count = size })
			
			--this is very inefficient, but it is a quick and dirty way of ensuring that the size of the cached stream does not grow indefinately
			cache.udpstream:write({ type = blob, value = cache.udpstream, start = size })
			if #cache.udpstream == size then
				cache.streamempty = true
				cache.udpstream:resize(1, false)
			else
				cache.udpstream:resize((#cache.udpstream  - size), false)
			end
			
			return true, result
		end,
		close = closesocket
	}
	
	local function initsocket(cache, server, udptimeout)
		local protocol = server.protocol
	
		if protocol == "tcp" then
			cache.net = net_tcp
		elseif protocol == "udp" then
			cache.reqnum = 0
			cache.timeout = (udptimeout or 250)	    --default timeout is 250ms
			cache.net = net_udp
		else
			error("transport protocol '" .. protocol .. "' is unsupported")
		end
	
		local sock = net.newsocket(net.getaddrinfo("localhost", nil, protocol, server.family, server.sockettype, false)[1])
		sock:connect(server)
		cache.socket = sock
		cache.connected = true
	end
	
	memcache.providers.ascii_socket =
	{
		initclient = function(cache, server, udptimeout)
			initsocket(cache, server, udptimeout)
			cache.impl = proto_text
		end
	}
	
end