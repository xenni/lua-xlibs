do
	local native = require("lua-sqlite3")
	
	local checkfuncs = { }
	local function makeudwrapmt(errget, errprefix)
		local fullerrprefix;
		if (errprefix == nil or errprefix == "") then
			fullerrprefix = ""
		else
			fullerrprefix = (errprefix .. " ")
		end
		local fullerrget = (fullerrprefix .. errget)
		local mt =
		{
			__objs = { },
			__members = { }
		}
		mt.__index = function(t, k)
			local obj = mt.__objs[t];
			if obj == nil then error(fullerrget, 2) end
			
			if type(k) == "string" then
				local func = mt.__members["get_" .. k]
				if func ~= nil then return func(obj, t, k) end
			end
			
			return nil
		end
		mt.__newindex = function(t, k, v)
			local obj = mt.__objs[t];
			if db == nil then error(fullergetrget, 2) end
			
			if type(k) == "string" then
				local func = mt.__members["set_" .. k]
				if func ~= nil then return func(obj, t, v, k) end
			end
			
			error((fullerrprefix .. "member '" .. tostring(k) .. "' is readonly or does not exist"), 2)
		end
		
		checkfuncs[mt] = function(t)
			if mt.__objs[t] == nil then
				error(fullerrget, 3)
			end
		end
		
		return mt
	end
	
	local db_mt = makeudwrapmt("connection is no longer open", "sqlite3 error:")
	local stmt_mt = makeudwrapmt("statement is no longer valid", "sqlite3 error:")
	local blob_mt = makeudwrapmt("blob is no longer open", "sqlite3 error:")
	
	blob_mt.__len = function(t)
		checkfuncs[blob_mt](t)
		return passerrors(native.blobbytes, 2, blob_mt.__objs[t])
	end
	
	local function passerrors(f, l, ...)
		local results = table.pack(pcall(f, ...))
		if results[1] then
			return table.unpack(results, 2)
		else
			error(results[2], (l + 1))
		end
	end
	
	local function udwrapfunc(objmt, funcname, nativefuncname)
		objmt.__members["get_" .. funcname] = function(obj, t, k)
			return function(...)
				checkfuncs[objmt](t)
				return passerrors(native[nativefuncname or funcname], 2, obj, ...)
			end
		end
	end
	local function udwrapinit(initmt, funcname, objmt, nativefuncname)
		initmt.__members["get_" .. funcname] = function(initobj, initt, initk)
			return function(...)
				checkfuncs[initmt](initt)
				local results = table.pack(pcall(native[nativefuncname or funcname], initobj, ...))
				if results[1] then
					if results[2] then
						local tobj = { }
						objmt.__objs[tobj] = (results[3] or results[2])
						setmetatable(tobj, objmt)
						return tobj
					else
						return table.unpack(results, 2)
					end
				else
					error(results[2], 2)
				end
			end
		end
	end
	local function udwrapclose(objmt, funcname, nativefuncname)
		objmt.__members["get_" .. funcname] = function(obj, t, k)
			return function(...)
				checkfuncs[objmt](obj)
				local results = table.pack(pcall(native[nativefuncname or funcname], obj, ...))
				if results[1] then
					if results[2] then
						objmt.__objs[t] = nil
					else
						return table.unpack(results, 2)
					end
				else
					error(results[2], 2)
				end
			end
		end
	end
	local function udwrapprop(objmt, propname, setfunc, nativefuncname)
		if setfunc then
			objmt.__members["set_" .. propname] = function(obj, t, v, k)
				return passerrors(native[nativefuncname or propname], 2, obj, v)
			end
		else
			objmt.__members["get_" .. propname] = function(obj, t, k)
				return passerrors(native[nativefuncname or propname], 2, obj)
			end
		end
	end
	
	udwrapfunc(db_mt, "backup")
	udwrapprop(db_mt, "changes")
	udwrapclose(db_mt, "close")
	udwrapprop(db_mt, "oncollationneeded", true, "setcollationneededhandler")
	udwrapfunc(db_mt, "createcollation")
	udwrapfunc(db_mt, "createfunction")
	udwrapfunc(db_mt, "exec")
	udwrapprop(db_mt, "autocommit", false, "getautocommit")
	udwrapfunc(db_mt, "interrupt")
	udwrapprop(db_mt, "lastinsertrowid", false, "getlastinsertrowid")
	udwrapinit(db_mt, "prepare", stmt_mt)
	udwrapinit(db_mt, "openblob", blob_mt, "blobopen")
	
	udwrapfunc(stmt_mt, "bind")
	udwrapfunc(stmt_mt, "bindzeroblob")
	udwrapfunc(stmt_mt, "clearbindings")
	udwrapfunc(stmt_mt, "columns")
	udwrapclose(stmt_mt, "finialize")
	udwrapfunc(stmt_mt, "reset")
	udwrapfunc(stmt_mt, "step")
	
	udwrapfunc(blob_mt, "close", "blobclose")
	udwrapfunc(blob_mt, "read", "blobread")
	udwrapfunc(blob_mt, "reopen", "blobreopen")
	udwrapfunc(blob_mt, "write", "blobwrite")
	
	local sqlite = { }
	local sqlite_contents = {
		dbnull = native.dbnull,
		sqlitever = native.sqltiever
	}
	local sqlite_mt = {
		__index = sqlite_contents
	}
	
	sqlite_contents.open = function(path, flags)
		local db = passerrors(native.open, 2, path, flags)
		local t = { }
		setmetatable(t, db_mt)
		db_mt.__objs[t] = db
		return t
	end
	
	setmetatable(sqlite, sqlite_mt)
	
	return sqlite
end