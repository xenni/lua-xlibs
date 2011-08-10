do
	local cache_clients = setmetatable({ }, { __mode = "k" })
	
	local cache_client_mt = {
		__index = function(self, key)
			if self.impl then
				local val = self.impl[key]
				if val ~= nil then
					return val
				end
			end
		
			return self.provider[key]
		end
	}
	
	local function client_doget(self, funcname, tblfuncname, ...)
		local count = select("#", ...)
		if count == 0 then
			error("expected at least one key")
		elseif count == 1 then
			local val = (...)
			if type(val) == "table" then
				return self[tblfuncname](self, val)
			else
				return self[funcname](self, val)
			end
		else
			return self[funcname](self, ...)
		end
	end
	
	local client_mt_funcs =
	{
		get = function(self, ...)
			return client_doget(cache_clients[self], "get", "tblget", ...)
		end,
		gets = function(self, ...)
			return client_doget(cache_clients[self], "gets", "tblgets", ...)
		end,
		set = function(self, key, value, exptime, flags, noreply)
			return cache_clients[self]:set(key, value, exptime, flags, noreply)
		end,
		add = function(self, key, value, exptime, flags, noreply)
			return cache_clients[self]:add(key, value, exptime, flags, noreply)
		end,
		replace = function(self, key, value, exptime, flags, noreply)
			return cache_clients[self]:replace(key, value, exptime, flags, noreply)
		end,
		append = function(self, key, value, noreply)
			return cache_clients[self]:append(key, value, noreply)
		end,
		prepend = function(self, key, value, noreply)
			return cache_clients[self]:prepend(key, value, noreply)
		end,
		cas = function(self, key, unique, value, exptime, flags, noreply)
			return cache_clients[self]:cas(key, unique, value, exptime, flags, noreply)
		end,
		delete = function(self, key, blocktime, noreply)
			return cache_clients[self]:delete(key, blocktime, noreply)
		end,
		incr = function(self, key, amount, noreply)
			return cache_clients[self]:incr(key, amount, noreply)
		end,
		decr = function(self, key, amount, noreply)
			return cache_clients[self]:incr(key, (-1 * amount), noreply)
		end,
		flushall = function(self, noreply)
			return cache_clients[self]:flushall(noreply)
		end,
		close = function(self)
			cache_clients[self]:close()
		end,
		provider = function(self, ...)
			local providerctrl = cache_clients[self].providerctrl
			if providerctrl then
				return providerctrl(self, ...)
			end
		end
	}
	
	local client_mt_getters =
	{
		version = function(self)
			return cache_clients[self]:version()
		end
	}
	
	local client_mt =
	{
		__metatable = "protected",
		__index = function(self, key)
			if type(key) == "string" then
				local val = client_mt_funcs[key]
				if val then return val end
				val = client_mt_getters[key]
				if val then return val(self) end
			end
			
			local result = cache_clients[self]:get(key)
			
			return (result and result[key] and (result[key].value or result.error))
		end,
		__newindex = function(self, key, value)
			if type(key) == "string" then
				if client_mt_funcs[key] or client_mt_getters[key] then
					error("'" .. key .. "' is readonly")
				end
			end
			
			cache_clients[self]:set(key, value, 0, 0, true)
		end
	}
	
	local client_providers = { }
	
	return setmetatable({ }, {
		__metatable = "protected",
		__newindex = function()
			error("table is readonly", 2)
		end,
		__index =
		{
			providers = client_providers,
			connect = function(providerkey, ...)
				local provider = client_providers[providerkey]
				if not provider then
					error("memcache provider '" .. tostring(providerkey) .. "' does not exist")
				end
				
				local cache = { provider = provider }
				if provider.initclient then
					provider.initclient(cache, ...)
				end
				
				local result = setmetatable({ }, client_mt)
				cache_clients[result] = cache
				setmetatable(cache, cache_client_mt)
				return result
			end
		}
	})
end