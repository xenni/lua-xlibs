#include <string.h>

#ifdef MSVC_VER
#define LUA_BUILD_AS_DLL
#define SQLITE_API __declspec(dllimport)
#endif

#define SQLITE_OMIT_DEPRECATED

#include <sqlite3.h>
#include <lua.h>
#include <lauxlib.h>
#include <luablob.h>

#ifdef MSVC_VER
	#define LUA_CFUNCTION_F int __cdecl
	#ifdef __cplusplus
		#define LUA_MODLOADER_F extern "C" __declspec(dllexport) int __cdecl
	#else
		#define LUA_MODLOADER_F __declspec(dllexport) int __cdecl
	#endif
#else
	#define LUA_CFUNCTION_F int
	#ifdef __cplusplus
		#define LUA_MODLOADER_F extern "C" int
	#else
		#define LUA_MODLOADER_F int
	#endif
#endif

// calls an sqlite3_* function f and arguments and throws a generic Lua error if the call fails
#define sqlite3(f, ...) { int rc = (sqlite3_ ## f ## (__VA_ARGS__)); if (rc != SQLITE_OK) { luaL_error(L, ("sqlite3 error (%d): operation '" ## #f ## "' failed"), rc); } }
// calls an sqlite3_* function f with the specified database handle and arguments and throws a detailed Lua error if the call fails
#define sqlite3db(db, f, ...) if ((sqlite3_ ## f ## (db, ## __VA_ARGS__)) != SQLITE_OK) { luaL_error(L, "sqlite3 error (%d): %s", sqlite3_extended_errcode(db), sqlite3_errmsg(db)); }

LUA_CFUNCTION_F lua_sqlite3_backup(lua_State *L)
{	//STACK: src srcname destpath destname throwonerror? ?
	sqlite3 *srcdb;
	const char *srcname;
	const char *destname;
	sqlite3 *destdb;
	struct sqlite3_backup *backup;
	int errcode;
	const char *errmsg;
	int throwonerror;
	int retriesleft;

	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	srcdb = (sqlite3 *)lua_touserdata(L, 1);
	srcname = luaL_checkstring(L, 2);
	destname = luaL_checkstring(L, 4);

	if (lua_gettop(L) > 4)
	{
		luaL_checktype(L, 5, LUA_TBOOLEAN);
		throwonerror = lua_toboolean(L, 5);
	}
	else
	{
		throwonerror = 1;
	}

	sqlite3(open_v2, luaL_checkstring(L, 3), &destdb, (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_PRIVATECACHE), NULL);
	sqlite3_busy_timeout(destdb, 250);

	backup = sqlite3_backup_init(destdb, destname, srcdb, srcname);
	if (backup == NULL)
	{
		errcode = sqlite3_extended_errcode(destdb);
		errmsg = sqlite3_errmsg(destdb);

		sqlite3db(destdb, close);

		luaL_error(L, "sqlite3 error (%d): %s", errcode, errmsg);
	}

	retriesleft = 5;
do_step:
	switch(sqlite3_backup_step(backup, -1))
	{
		case SQLITE_DONE:
			sqlite3_backup_finish(backup);
			if (throwonerror)
			{
				sqlite3db(destdb, close);

				return 0;
			}
			else
			{
				sqlite3db(destdb, close);

				lua_pushboolean(L, 1);		//STACK: src srcname destpath destname throwonerror? ? true
				return 1;					//RETURN: true
			}
		case SQLITE_OK:		//this should never happen, but we will just make the call again if it ever does
			goto do_step;
		case SQLITE_BUSY:
			sqlite3_backup_finish(backup);
			if (throwonerror)
			{
				sqlite3db(destdb, close);

				luaL_error(L, "sqlite3 error (%d): destination database busy", SQLITE_BUSY);
			}
			else
			{
				sqlite3db(destdb, close);

				lua_pushboolean(L, 0);		//STACK: src srcname destpath destname throwonerror? ? false
				lua_pushfstring(L, "sqlite3 error (%d): destination database busy", SQLITE_BUSY);	//STACK: src srcname destpath destname throwonerror? ? false errmsg
				return 2;	//RETURN: false errmsg
			}
		case SQLITE_LOCKED:
			if (retriesleft--)
			{
				sqlite3_sleep(50);
				goto do_step;
			}

			sqlite3_backup_finish(backup);
			if (throwonerror)
			{
				sqlite3db(destdb, close);

				luaL_error(L, "sqlite3 error (%d): destination database locked", SQLITE_LOCKED);
			}
			else
			{
				sqlite3db(destdb, close);

				lua_pushboolean(L, 0);		//STACK: src srcname destpath destname throwonerror? ? false
				lua_pushfstring(L, "sqlite3 error (%d): destination database locked", SQLITE_LOCKED);	//STACK: src srcname destpath destname throwonerror? ? false errmsg
				return 2;	//RETURN: false errmsg
			}
		default:
			sqlite3_backup_finish(backup);
			if (throwonerror)
			{
				errcode = sqlite3_extended_errcode(destdb);
				errmsg = sqlite3_errmsg(destdb);

				sqlite3db(destdb, close);

				luaL_error(L, "sqlite3 error (%d): %s", errcode, errmsg);
			}
			else
			{
				lua_pushboolean(L, 0);		//STACK: src srcname destpath destname throwonerror? ? false
				lua_pushfstring(L, "sqlite3 error (%d): %s", sqlite3_extended_errcode(destdb), sqlite3_errmsg(destdb));	//STACK: src srcname destpath destname throwonerror? ? false errmsg

				sqlite3db(destdb, close);

				return 2;	//RETURN: false errmsg
			}
	}
}

void lua_sqlite3_colbind(lua_State *L, sqlite3_stmt *stmt, int colid)
{	//STACK: ? value
	size_t lstrlen = 0;
	GenericMemoryBlob *blob;

	switch(lua_type(L, -1))
	{
		case LUA_TNIL:
		case_lua_tnil:
			sqlite3(bind_null, stmt, colid);
			break;
		case LUA_TNUMBER:
			sqlite3(bind_double, stmt, colid, (double)lua_tonumber(L, -1));
			break;
		case LUA_TBOOLEAN:
			sqlite3(bind_int, stmt, colid, lua_toboolean(L, -1));
			break;
		case LUA_TSTRING:
			sqlite3(bind_text, stmt, colid, lua_tolstring(L, -1, &lstrlen), lstrlen, SQLITE_TRANSIENT);
			break;
		case LUA_TTABLE:
			lua_pushliteral(L, "sqlite3_dbnull");	//STACK: ? value 'sqlite3_dbnull'
			lua_gettable(L, LUA_REGISTRYINDEX);		//STACK: ? value sqlite3_dbnull
			if (lua_compare(L, -1, -2, LUA_OPEQ))
			{
				lua_pop(L, 1);						//STACK: ? value
				goto case_lua_tnil;
			}
			else
			{
				lua_pop(L, 1);						//STACK: ? value
				goto case_default;
			}
			break;
		case LUA_TUSERDATA:
			blob = luablob_checkgmb(L, -1);
			sqlite3(bind_blob, stmt, colid, (const void *)blob->data, blob->usedsize, SQLITE_TRANSIENT);
			break;
		default:
		case_default:
			luaL_error(L, "sqlite3 error: bind value must be nil, a number, a boolean, a string, a luablob, or dbnull");
			break;
	}
}

LUA_CFUNCTION_F lua_sqlite3_bind(lua_State *L)
{	//STACK: stmt map|(id value) ?
	sqlite3_stmt *stmt;

	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	stmt = (sqlite3_stmt *)lua_touserdata(L, 1);

	switch(lua_type(L, 2))
	{
		case LUA_TNUMBER:				//STACK: stmt id value ?
			lua_sqlite3_colbind(L, stmt, (int)lua_tointeger(L, 2));
			break;
		case LUA_TSTRING:				//STACK: stmt id value ?
			lua_sqlite3_colbind(L, stmt, sqlite3_bind_parameter_index(stmt, lua_tostring(L, 2)));
			break;
		case LUA_TTABLE:				//STACK: stmt map ?
			lua_pushnil(L);				//STACK: stmt map ? nil
			while(lua_next(L, 2) != 0)	//STACK: stmt map ? key value
			{
				switch(lua_type(L, -2))
				{
					case LUA_TNUMBER:
						lua_sqlite3_colbind(L, stmt, (int)lua_tointeger(L, -2));	//STACK: stmt map ? key value
						break;
					case LUA_TSTRING:
						lua_sqlite3_colbind(L, stmt, sqlite3_bind_parameter_index(stmt, lua_tostring(L, -2)));	//STACK: stmt map ? key value
						break;
					default:
						luaL_error(L, "sqlite3 error: binding identifier must be an integer or a string");
				}
										//STACK: stmt map ? key value
				lua_pop(L, 1);			//STACK: stmt map ? key
			}
			break;
		default:
			luaL_error(L, "sqlite3 error: binding information must be an integer or a string followed by a value, or a table mapping integers and/or strings to values");
	}

	return 0;
}

LUA_CFUNCTION_F lua_sqltie3_bind_zeroblob(lua_State *L)
{	//STACK: stmt map|(id size)
	sqlite3_stmt *stmt;

	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	stmt = (sqlite3_stmt *)lua_touserdata(L, 1);

	switch(lua_type(L, 2))
	{
		case LUA_TNUMBER:				//STACK: stmt id size ?
			sqlite3(bind_zeroblob, stmt, luaL_checkinteger(L, 2), luaL_checkinteger(L, 3));
			break;
		case LUA_TSTRING:				//STACK: stmt id size ?
			sqlite3(bind_zeroblob, stmt, sqlite3_bind_parameter_index(stmt, lua_tostring(L, 2)), luaL_checkinteger(L, 3));
			break;
		case LUA_TTABLE:				//STACK: stmt map ?
			lua_pushnil(L);				//STACK: stmt map ? nil
			while(lua_next(L, 2) != 0)	//STACK: stmt map ? key size
			{
				switch(lua_type(L, -2))
				{
					case LUA_TNUMBER:
						sqlite3(bind_zeroblob, stmt, luaL_checkinteger(L, -2), luaL_checkinteger(L, -1));
						break;
					case LUA_TSTRING:
						sqlite3(bind_zeroblob, stmt, sqlite3_bind_parameter_index(stmt, lua_tostring(L, -2)), luaL_checkinteger(L, -1));
						break;
					default:
						luaL_error(L, "sqlite3 error: binding identifier must be an integer or a string");
				}
										//STACK: stmt map ? key size
				lua_pop(L, 1);			//STACK: stmt map ? key
			}
			break;
		default:
			luaL_error(L, "sqlite3 error: zeroblob binding information must be an integer or a string followed by an integer size, or a table mapping integers and/or strings to integer sizes");
	}

	return 0;
}

LUA_CFUNCTION_F lua_sqlite3_blob_bytes(lua_State *L)
{	//STACK: blob ?
	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	lua_pushinteger(L, sqlite3_blob_bytes((sqlite3_blob *)lua_touserdata(L, 1)));	//STACK: blob ? size
	return 1;																		//RETURN: size
}

LUA_CFUNCTION_F lua_sqlite3_blob_close(lua_State *L)
{	//STACK: blob ?
	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	sqlite3(blob_close, (sqlite3_blob *)lua_touserdata(L, 1));

	return 0;
}

LUA_CFUNCTION_F lua_sqlite3_blob_open(lua_State *L)
{	//STACK: db dbname table column row readonly? ?
	sqlite3 *db;
	const char *dbname;
	const char *table;
	const char *column;
	lua_Integer row;
	int readwrite;
	sqlite3_blob *blob;

	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	db = (sqlite3 *)lua_touserdata(L, 1);
	dbname = luaL_checkstring(L, 2);
	table = luaL_checkstring(L, 3);
	column = luaL_checkstring(L, 4);
	row = luaL_checkinteger(L, 5);

	if (lua_gettop(L) > 5)
	{
		luaL_checktype(L, 6, LUA_TBOOLEAN);
		readwrite = !lua_toboolean(L, 6);
	}
	else
	{
		readwrite = 1;
	}

	sqlite3db(db, blob_open, dbname, table, column, (sqlite3_int64)row, readwrite, &blob);

	lua_pushlightuserdata(L, blob);		//STACK: db dbname table column row readonly? ? blob
	return 1;							//RETURN: blob
}

LUA_CFUNCTION_F lua_sqlite3_blob_read(lua_State *L)
{	//STACK: blob count? offset? ?
	sqlite3_blob *blob;
	GenericMemoryBlob gmb;
	int count;
	int offset;

	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	blob = (sqlite3_blob *)lua_touserdata(L, 1);

	if (lua_gettop(L) > 2)
	{
		offset = luaL_checkint(L, 3);
	}
	else
	{
		offset = 0;
	}
	if (lua_gettop(L) > 1)
	{
		count = luaL_checkint(L, 2);
	}
	else
	{
		count = (sqlite3_blob_bytes(blob) - offset);
	}

	luablob_newgmb(L, &gmb, count, "tight");
	sqlite3(blob_read, blob, gmb.data, count, offset);
	luablob_pushgmb(L, gmb);

	return 1;									//RETURN: data
}

LUA_CFUNCTION_F lua_sqlite3_blob_reopen(lua_State *L)
{	//STACK: blob row
	sqlite3_blob *blob;
	lua_Integer row;

	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	blob = (sqlite3_blob *)lua_touserdata(L, 1);
	row = luaL_checkinteger(L, 2);

	sqlite3(blob_reopen, blob, (sqlite3_int64)row);

	return 0;
}

LUA_CFUNCTION_F lua_sqlite3_blob_write(lua_State *L)
{	//STACK: blob data count? offset? ?
	sqlite3_blob *blob;
	GenericMemoryBlob* gmb;
	int count;
	int offset;

	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	blob = (sqlite3_blob *)lua_touserdata(L, 1);
	gmb = luablob_checkgmb(L, 2);

	if (lua_gettop(L) > 3)
	{
		offset = luaL_checkint(L, 4);
	}
	else
	{
		offset = 0;
	}

	if (lua_gettop(L) > 2)
	{
		count = luaL_checkint(L, 3);
		if (count < 0)
		{
			luaL_error(L, "sqlite3 error: count must be non-negative");
		}
		if (gmb->usedsize > (unsigned int)count)
		{
			luaL_error(L, "sqlite3 error: count is larger than the size of the data buffer");
		}
	}
	else
	{
		count = gmb->usedsize;
	}

	lua_pop(L, 1);	//STACK: blob data count? offset? ?

	if (count != 0)
	{
		sqlite3(blob_write, blob, gmb->data, count, offset);
	}

	return 0;
}

LUA_CFUNCTION_F lua_sqlite3_changes(lua_State *L)
{   //STACK: db ?
	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	lua_pushinteger(L, sqlite3_changes((sqlite3 *)lua_touserdata(L, 1)));	//STACK: db ? changes
	return 1;		//RETURN: changes
}

LUA_CFUNCTION_F lua_sqlite3_clear_bindings(lua_State *L)
{	//STACK: stmt ?
	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	sqlite3_clear_bindings((sqlite3_stmt *)lua_touserdata(L, 1));
	return 0;
}

LUA_CFUNCTION_F lua_sqlite3_close(lua_State *L)
{	//STACK: db throwonbusy? throwonerror? ?
	sqlite3 *db;
	int throwonbusy;
	int throwonerror;

	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	db = (sqlite3 *)lua_touserdata(L, 1);

	if (lua_gettop(L) > 1)
	{
		luaL_checktype(L, 2, LUA_TBOOLEAN);
		throwonbusy = throwonerror = lua_toboolean(L, 2);

		if (!throwonbusy && lua_gettop(L) > 2)
		{
			luaL_checktype(L, 3, LUA_TBOOLEAN);
			throwonerror = lua_toboolean(L, 3);
		}
	}
	else
	{
		throwonbusy = 0;
		throwonerror = 0;
	}

	switch(sqlite3_close(db))
	{
		case SQLITE_OK:
			if (throwonbusy || throwonerror)
			{
				return 0;
			}
			else
			{
				lua_pushboolean(L, 1);	//STACK:  db throwonbusy? throwonerror? ? true
				return 1;				//RETURN: true
			}
		case SQLITE_BUSY:
			throwonerror = throwonbusy;
			//fall through
		default:
			if (throwonerror)
			{
				luaL_error(L, "sqlite3 error (%d): %s", sqlite3_extended_errcode(db), sqlite3_errmsg(db));
			}
			else
			{
				lua_pushboolean(L, 0);	//STACK:  db throwonbusy? throwonerror? ? false
				lua_pushfstring(L, "sqlite3 error (%d): %s", sqlite3_extended_errcode(db), sqlite3_errmsg(db)); //STACK: db throwonbusy? throwonerror? ? false errmsg
				return 2;				//RETURN: false errmsg
			}
	}
}

void sqlite3_collation_needed_callback(void *arg, sqlite3* db, int etxtrep, const char *name)
{
	lua_State *L = (lua_State *)arg;							//STACK: ?

	luaL_checkstack(L, 2, NULL);
	lua_pushliteral(L, "sqlite3_collation_needed_callback");	//STACK: ? 'sqlite3_collation_needed_callback'
	lua_gettable(L, LUA_REGISTRYINDEX);							//STACK: ? sqlite3_collation_needed_callback
	lua_pushstring(L, (const char *)name);						//STACK: ? sqlite3_collation_needed_callback name
	if (lua_pcall(L, 1, 0, 0) != LUA_OK)						//STACK: ? errmsg?
	{															//STACK: ? errmsg
		sqlite3_log(SQLITE_ERROR, "lua error in 'collation_needed_callback': %s", lua_tostring(L, -1));
		lua_pop(L, 1);											//STACK: ?
	}
																//STACK: ?
}

LUA_CFUNCTION_F lua_sqlite3_collation_needed(lua_State *L)
{	//STACK: db func
	sqlite3 *db;
	int doregister;

	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	db = (sqlite3 *)lua_touserdata(L, 1);
	switch (lua_type(L, 2))
	{
		case LUA_TNIL:
			lua_pushliteral(L, "sqlite3_collation_needed_callback");	//STACK: db func 'sqlite3_collation_needed_callback'
			lua_pushnil(L);												//STACK: db func 'sqlite3_collation_needed_callback' nil
			lua_settable(L, LUA_REGISTRYINDEX);							//STACK: db func

			sqlite3_collation_needed(db, NULL, NULL);
			break;
		case LUA_TFUNCTION:
			lua_pushliteral(L, "sqlite3_collation_needed_callback");	//STACK: db func 'sqlite3_collation_needed_callback'
			lua_gettable(L, LUA_REGISTRYINDEX);							//STACK: db func sqlite3_collation_needed_callback
			doregister = lua_isnil(L, 3);
			lua_pop(L, 1);												//STACK: db func
			lua_pushliteral(L, "sqlite3_collation_needed_callback");	//STACK: db func 'sqlite3_collation_needed_callback'
			lua_pushvalue(L, 2);										//STACK: db func 'sqlite3_collation_needed_callback' func
			lua_settable(L, LUA_REGISTRYINDEX);							//STACK: db func

			if (doregister)
			{
				sqlite3_collation_needed(db, (void *)L, &sqlite3_collation_needed_callback);
			}
			break;
		default:
			luaL_checktype(L, 2, LUA_TFUNCTION); //always fails, will throw the proper lua error
	}

	return 0;
}

void lua_sqlite3_column_pushvalue(lua_State *L, sqlite3_stmt *stmt, int colindex)
{	//STACK: stmt ids... values...
	const void *blob;
	int blobsz;
	GenericMemoryBlob gmb;

	switch(sqlite3_column_type(stmt, colindex))
	{
		case SQLITE_INTEGER:
			lua_pushinteger(L, (lua_Integer)sqlite3_column_int64(stmt, colindex));	//STACK: stmt ids... values... value
			break;
		case SQLITE_FLOAT:
			lua_pushnumber(L, (lua_Number)sqlite3_column_double(stmt, colindex));	//STACK: stmt ids... values... value
			break;
		case SQLITE_TEXT:
			lua_pushlstring(L, (const char *)sqlite3_column_text(stmt, colindex), sqlite3_column_bytes(stmt, colindex));  //STACK: stmt ids... values... value
			break;
		case SQLITE_BLOB:
			luaL_checkstack(L, 4, NULL);
			blob = sqlite3_column_blob(stmt, colindex);
			blobsz = sqlite3_column_bytes(stmt, colindex);
			luablob_newgmb(L, &gmb, blobsz, "tight");
			memcpy(gmb.data, blob, blobsz);
			luablob_pushgmb(L, gmb);												//STACK: stmt ids... values... value
			break;
		case SQLITE_NULL:
			lua_pushnil(L);															//STACK: stmt ids... values... nil
			break;
	}
}

LUA_CFUNCTION_F lua_sqlite3_columns(lua_State *L)
{	//STACK: stmt ids...
	sqlite3_stmt *stmt;
	int colcount;
	int i;
	int j;
	int resultcount;
	const char *colname;
	int colnamelen;
	const char *jname;
	int colindex;

	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	stmt = (sqlite3_stmt *)lua_touserdata(L, 1);
	colcount = (lua_gettop(L) - 1);

	resultcount = sqlite3_column_count(stmt);

	luaL_checkstack(L, colcount, NULL);
	for (i = 1; i <= colcount;)
	{
		i++;
		switch(lua_type(L, i))
		{
			case LUA_TNUMBER:
				colindex = (lua_tointeger(L, i) - 1);
				if (colindex < 0 || colindex >= resultcount)
				{
					luaL_error(L, "sqlite3 error: column (%d) does not exist in the result set", (colindex + 1));
				}
				lua_sqlite3_column_pushvalue(L, stmt, colindex);		//STACK: stmt ids... values... value
				break;
			case LUA_TSTRING:
				colname = luaL_checkstring(L, i);
				colnamelen = strlen(colname);
				for (j = 0; j < resultcount; j++)
				{
					jname = sqlite3_column_name(stmt, j);
					if (colnamelen == strlen(jname) && sqlite3_strnicmp(colname, jname, colnamelen) == 0)
					{
						lua_sqlite3_column_pushvalue(L, stmt, j);		//STACK: stmt ids... values... value
						break;
					}
				}
				if (j == resultcount)
				{
					luaL_error(L, "sqlite3 error: column '%s' does not exist in the result set", colname);
				}
				break;
			default:
				luaL_error(L, "sqlite3 error: column identifier must be a number index beginning at 1 or a string specifying a column name");
		}
	}

	return colcount;	//RETURN: results...
}

struct sqlite3_function_info
{
	lua_State *L;
	const char *name;
};

void sqlite3_collation_destroy(void *arg)
{
	struct sqlite3_function_info *info;
	lua_State *L;
	const char* name;

	info = (struct sqlite3_function_info *)arg;
	L = info->L;
	name = info->name;

	luaL_checkstack(L, 3, NULL);
	lua_pushliteral(L, "sqlite3_collations");		//STACK: ? 'sqlite3_collations'
	lua_gettable(L, LUA_REGISTRYINDEX);				//STACK: ? sqlite3_collations
	lua_pushstring(L, name);						//STACK: ? sqlite3_collations name
	lua_pushnil(L);									//STACK: ? sqlite3_collations name nil
	lua_settable(L, -3);							//STACK: ? sqlite3_collations
	lua_pop(L, 1);									//STACK: ?

	sqlite3_free(info);
}

int sqlite3_collation_xcompare(void *arg, int arg2, const void *str1, int arg4, const void *str2)
{
	struct sqlite3_function_info *info;
	lua_State *L;
	const char* name;
	int result;
	int resultisnum;

	info = (struct sqlite3_function_info *)arg;
	L = info->L;									//STACK: ?
	name = info->name;

	luaL_checkstack(L, 5, NULL);
	lua_pushliteral(L, "sqlite3_collations");		//STACK: ? 'sqlite3_collations'
	lua_gettable(L, LUA_REGISTRYINDEX);				//STACK: ? sqlite3_collations
	lua_pushstring(L, name);						//STACK: ? sqlite3_collations name
	lua_gettable(L, -2);							//STACK: ? sqlite3_collations xcompare
	lua_pushstring(L, (const char *)str1);			//STACK: ? sqlite3_collations xcompare str1
	lua_pushstring(L, (const char *)str2);			//STACK: ? sqlite3_collations xcompare str1 str2
	lua_pushstring(L, name);						//STACK: ? sqlite3_collations xcompare str1 str2 name
	if (lua_pcall(L, 3, 1, 0) != LUA_OK)			//STACK: ? sqlite3_collations errmsg|result
	{												//STACK: ? sqlite3_collations errmsg
		sqlite3_log(SQLITE_ERROR, "error in lua collation '%s': %s", name, lua_tostring(L, -1)); //log the error
		lua_pop(L, 2);								//STACK: ?
		return 0;	//return that they are equal, because there is nothing else we can do
	}
	else
	{												//STACK: ? sqlite3_collations result
		result = (int)lua_tointegerx(L, -1, &resultisnum);
		lua_pop(L, 2);								//STACK: ?
		if (!resultisnum)
		{
			sqlite3_log(SQLITE_ERROR, "warning for lua collation '%s': failed to return an integer", name);	//log the warning
		}
		return result;
	}
}

LUA_CFUNCTION_F lua_sqlite3_create_collation_v2(lua_State *L)
{	//STACK: db name func
	sqlite3 *db;
	const char *name;
	struct sqlite3_function_info *info;
	int doregister;

	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	db = (sqlite3 *)lua_touserdata(L, 1);
	name = luaL_checkstring(L, 2);
	switch(lua_type(L, 3))
	{
		case LUA_TFUNCTION:
		case LUA_TNIL:
			break;
		default:
			luaL_checktype(L, 3, LUA_TFUNCTION);	//will always fail and throw proper error message
	}

	lua_pushliteral(L, "sqlite3_collations");		//STACK: db name func 'sqlite3_collations'
	lua_gettable(L, LUA_REGISTRYINDEX);				//STACK: db name func sqlite3_collations
	lua_pushvalue(L, 2);							//STACK: db name func sqlite3_collations name
	lua_gettable(L, 4);								//STACK: db name func sqlite3_collations xcompare
	doregister = lua_isnil(L, 5);
	lua_pop(L, 1);									//STACK: db name func sqlite3_collations
	lua_pushvalue(L, 2);							//STACK: db name func sqlite3_collations name
	lua_pushvalue(L, 3);							//STACK: db name func sqlite3_collations name func
	lua_settable(L, 4);								//STACK: db name func sqlite3_collations

	if (doregister)
	{
		if (lua_isnil(L, 3))
		{
			sqlite3db(db, create_collation_v2, name, SQLITE_UTF8, NULL, NULL, NULL);
		}
		else
		{
			info = (struct sqlite3_function_info *)sqlite3_malloc(sizeof(struct sqlite3_function_info));
			info->L = L;
			info->name = name;
			if (sqlite3_create_collation_v2(db, name, SQLITE_UTF8, (void *)info, &sqlite3_collation_xcompare, &sqlite3_collation_destroy) != SQLITE_OK)
			{
				sqlite3_collation_destroy(info);

				lua_pushvalue(L, 2);						//STACK: db name func sqlite3_collations name
				lua_pushnil(L);								//STACK: db name func sqlite3_collations name nil
				lua_settable(L, 4);							//STACK: db name func sqlite3_collations

				luaL_error(L, "sqlite3 error (%d): %s", sqlite3_extended_errcode(db), sqlite3_errmsg(db));
			}
		}
	}

	return 0;
}

void sqlite3_function_aggregate_xstep(sqlite3_context *ctx, int numvalues, sqlite3_value **values)
{
	struct sqlite3_function_info *info;
	lua_State *L;
	const char *name;
	int i;
	size_t lstrlen = 0;

	info = (struct sqlite3_function_info *)sqlite3_user_data(ctx);
	L = info->L;													//STACK: ?
	name = info->name;

	luaL_checkstack(L, 7, NULL);
	lua_pushliteral(L, "sqlite3_function_aggregate_xsteps");		//STACK: ? 'sqlite3_function_aggregate_xsteps'
	lua_gettable(L, LUA_REGISTRYINDEX);								//STACK: ? sqlite3_function_aggregate_xsteps
	lua_pushstring(L, name);										//STACK: ? sqlite3_function_aggregate_xsteps name
	lua_gettable(L, -2);											//STACK: ? sqlite3_function_aggregate_xsteps xstep
	lua_remove(L, -2);												//STACK: ? xstep
	lua_createtable(L, numvalues, 0);								//STACK: ? xstep {~0}
	for (i=0; i < numvalues; i++)
	{
		lua_pushinteger(L, i);														//STACK: ? xstep {~0} index
		switch(sqlite3_value_type(values[i]))
		{
			case SQLITE_INTEGER:
			case SQLITE_FLOAT:
				lua_pushnumber(L, (lua_Number)sqlite3_value_double(values[i]));		//STACK: ? xstep {~0} index value
				break;
			case SQLITE_BLOB:
				lua_pushlightuserdata(L, (void *)sqlite3_value_blob(values[i]));	//STACK: ? xstep {~0} index value
				break;
			case SQLITE_NULL:
				lua_pushliteral(L, "sqlite3_dbnull");								//STACK: ? xstep {~0} index 'sqlite3_dbnull'
				lua_gettable(L, LUA_REGISTRYINDEX);									//STACK: ? xstep {~0} index sqlite3_dbnull
				break;
			case SQLITE_TEXT:
				lua_pushlstring(L, (const char *)sqlite3_value_text(values[i]), (sqlite3_value_bytes(values[i]) + sizeof(char)));	//STACK: ? xstep {~0} index value
				break;
		}
		lua_settable(L, -3);										//STACK: ? xstep {~0}
	}
	lua_pushlightuserdata(L, (void *)ctx);							//STACK: ? xstep {~0} sqlite3ctx
	lua_pushliteral(L, "sqlite3_function_aggregate_ctxts");			//STACK: ? xstep {~0} sqlite3ctx 'sqlite3_function_aggregate_ctxts'
	lua_gettable(L, LUA_REGISTRYINDEX);								//STACK: ? xstep {~0} sqlite3ctx sqlite3_function_aggregate_ctxts
	lua_pushvalue(L, -2);											//STACK: ? xstep {~0} sqlite3ctx sqlite3_function_aggregate_ctxts sqlite3ctx
	lua_gettable(L, -2);											//STACK: ? xstep {~0} sqlite3ctx sqlite3_function_aggregate_ctxts aggregate_ctxt|nil
	if (lua_isnil(L, -1))
	{																//STACK: ? xstep {~0} sqlite3ctx sqlite3_function_aggregate_ctxts nil
		lua_pop(L, 1);												//STACK: ? xstep {~0} sqlite3ctx sqlite3_function_aggregate_ctxts
		lua_newtable(L);											//STACK: ? xstep {~0} sqlite3ctx sqlite3_function_aggregate_ctxts {~1}
		lua_pushvalue(L, -3);										//STACK: ? xstep {~0} sqlite3ctx sqlite3_function_aggregate_ctxts {~1} sqlite3ctx
		lua_pushvalue(L, -2);										//STACK: ? xstep {~0} sqlite3ctx sqlite3_function_aggregate_ctxts {~1} sqlite3ctx {~1}
		lua_settable(L, -4);										//STACK: ? xstep {~0} sqlite3ctx sqlite3_function_aggregate_ctxts {~1}
	}
																	//STACK: ? xstep {~0} sqlite3ctx sqlite3_function_aggregate_ctxts aggregate_ctxt
	lua_remove(L, -2);												//STACK: ? xstep {~0} sqlite3ctx aggregate_ctxt
	if (lua_pcall(L, 3, 0, 0) != LUA_OK)							//STACK: ? errmsg?
	{																//STACK: ? errmsg
		sqlite3_result_error(ctx, lua_tolstring(L, -1, &lstrlen), lstrlen);
		lua_pop(L, 1);												//STACK: ?
	}
}

void sqlite3_function_aggregate_xfinal(sqlite3_context *ctx)
{
	struct sqlite3_function_info *info;
	lua_State *L;
	const char *name;
	size_t lstrlen = 0;

	info = (struct sqlite3_function_info *)sqlite3_user_data(ctx);
	L = info->L;													//STACK: ?
	name = info->name;

	luaL_checkstack(L, 4, NULL);
	lua_pushliteral(L, "sqlite3_function_aggregate_xfinals");		//STACK: ? 'sqlite3_function_aggregate_xfinals'
	lua_gettable(L, LUA_REGISTRYINDEX);								//STACK: ? sqlite3_function_aggregate_xfinals
	lua_pushstring(L, name);										//STACK: ? sqlite3_function_aggregate_xfinals name
	lua_gettable(L, -2);											//STACK: ? sqlite3_function_aggregate_xfinals xfinal
	lua_remove(L, -2);												//STACK: ? xfinal
	lua_pushlightuserdata(L, ctx);									//STACK: ? xfinal sqlite3ctx
	lua_pushliteral(L, "sqlite3_function_aggregate_ctxts");			//STACK: ? xfinal sqlite3ctx 'sqlite3_function_aggregate_ctxts'
	lua_gettable(L, LUA_REGISTRYINDEX);								//STACK: ? xfinal sqlite3ctx sqlite3_function_aggregate_ctxts
	lua_pushvalue(L, -2);											//STACK: ? xfinal sqlite3ctx sqlite3_function_aggregate_ctxts sqlite3ctx
	lua_gettable(L, -2);											//STACK: ? xfinal sqlite3ctx sqlite3_function_aggregate_ctxts aggregate_ctxt
	lua_remove(L, -2);												//STACK: ? xfinal sqlite3ctx aggregate_ctxt
	if (lua_pcall(L, 2, 0, 0) != LUA_OK)							//STACK: ? errmsg?
	{																//STACK: ? errmsg
		sqlite3_result_error(ctx, lua_tolstring(L, -1, &lstrlen), lstrlen);
		lua_pop(L, 1);												//STACK: ?
	}
																	//STACK: ?
	lua_pushliteral(L, "sqlite3_function_aggregate_ctxts");			//STACK: ? 'sqlite3_function_aggregate_ctxts'
	lua_gettable(L, LUA_REGISTRYINDEX);								//STACK: ? sqlite3_function_aggregate_ctxts
	lua_pushvalue(L, -2);											//STACK: ? sqlite3_function_aggregate_ctxts sqlite3ctx
	lua_pushnil(L);													//STACK: ? sqlite3_function_aggregate_ctxts sqlite3ctx nil
	lua_settable(L,	-3);											//STACK: ? sqlite3_function_aggregate_ctxts
	lua_pop(L, 1);													//STACK: ?
}
void sqlite3_function_aggregate_destroy(void *arg)
{
	struct sqlite3_function_info *info;
	lua_State *L;
	const char *name;

	info = (struct sqlite3_function_info *)arg;
	L = info->L;
	name = info->name;

	lua_pushliteral(L, "sqlite3_function_aggregate_xsteps");		//STACK: ? 'sqlite3_function_aggregate_xsteps'
	lua_gettable(L, LUA_REGISTRYINDEX);								//STACK: ? sqlite3_function_aggregate_xsteps
	lua_pushstring(L, name);										//STACK: ? sqlite3_function_aggregate_xsteps name
	lua_pushnil(L);													//STACK: ? sqlite3_function_aggregate_xsteps name nil
	lua_settable(L, 5);												//STACK: ? sqlite3_function_aggregate_xsteps
	lua_pop(L, 1);													//STACK: ?

	lua_pushliteral(L, "sqlite3_function_aggregate_xfinals");		//STACK: ? 'sqlite3_function_aggregate_xfinals'
	lua_gettable(L, LUA_REGISTRYINDEX);								//STACK: ? sqlite3_function_aggregate_xfinals
	lua_pushstring(L, name);										//STACK: ? sqlite3_function_aggregate_xfinals name
	lua_pushnil(L);													//STACK: ? sqlite3_function_aggregate_xfinals name nil
	lua_settable(L, 5);												//STACK: ? sqlite3_function_aggregate_xfinals
	lua_pop(L, 1);													//STACK: ?

	lua_pushliteral(L, "sqlite3_function_aggregate_ctxts");			//STACK: ? 'sqlite3_function_aggregate_ctxts'
	lua_gettable(L, LUA_REGISTRYINDEX);								//STACK: ? sqlite3_function_aggregate_ctxts
	lua_pushstring(L, name);										//STACK: ? sqlite3_function_aggregate_ctxts name
	lua_pushnil(L);													//STACK: ? sqlite3_function_aggregate_ctxts name nil
	lua_settable(L, 5);												//STACK: ? sqlite3_function_aggregate_ctxts
	lua_pop(L, 1);													//STACK: ?

	sqlite3_free(info);
}
void sqlite3_function_scalar_xfunc(sqlite3_context *ctx, int numvalues, sqlite3_value **values)
{
	struct sqlite3_function_info *info;
	lua_State *L;
	const char *name;
	int i;
	size_t lstrlen = 0;

	info = (struct sqlite3_function_info *)sqlite3_user_data(ctx);
	L = info->L;													//STACK: ?
	name = info->name;

	luaL_checkstack(L, 6, NULL);
	lua_pushliteral(L, "sqlite3_function_scalars");					//STACK: ? 'sqlite3_function_scalars'
	lua_gettable(L, LUA_REGISTRYINDEX);								//STACK: ? sqlite3_function_scalars
	lua_pushstring(L, name);										//STACK: ? sqlite3_function_scalars name
	lua_gettable(L, -2);											//STACK: ? xfunc
	lua_remove(L, -2);
	lua_createtable(L, numvalues, 0);								//STACK: ? xfunc {~0}
	for (i=0; i < numvalues; i++)
	{
		lua_pushinteger(L, i);														//STACK: ? xfunc {~0} index
		switch(sqlite3_value_type(values[i]))
		{
			case SQLITE_INTEGER:
			case SQLITE_FLOAT:
				lua_pushnumber(L, (lua_Number)sqlite3_value_double(values[i]));		//STACK: ? xfunc {~0} index value
				break;
			case SQLITE_BLOB:
				lua_pushlightuserdata(L, (void *)sqlite3_value_blob);				//STACK: ? xstep {~0} index value
				break;
			case SQLITE_NULL:
				lua_pushliteral(L, "sqlite3_dbnull");								//STACK: ? xfunc {~0} index 'sqlite3_dbnull'
				lua_gettable(L, LUA_REGISTRYINDEX);									//STACK: ? xfunc {~0} index sqlite3_dbnull
				break;
			case SQLITE_TEXT:
				lua_pushlstring(L, (const char *)sqlite3_value_text(values[i]), (sqlite3_value_bytes(values[i]) + sizeof(char)));	//STACK: ? xfunc {~0} index value
				break;
		}
		lua_settable(L, -3);										//STACK: ? xfunc {~0}
	}
																	//STACK: ? xfunc {~0}
	lua_pushlightuserdata(L, (void *)ctx);							//STACK: ? xfunc {~0} sqlite3ctx
	if (lua_pcall(L, 2, 0, 0) != LUA_OK)							//STACK: ? errmsg?
	{																//STACK: ? errmsg
		sqlite3_result_error(ctx, lua_tolstring(L, -1, &lstrlen), lstrlen);
		lua_pop(L, 1);												//STACK: ?
	}
}
void sqlite3_function_scalar_destroy(void *arg)
{
	struct sqlite3_function_info *info;
	lua_State *L;
	const char *name;

	info = (struct sqlite3_function_info *)arg;
	L = info->L;													//STACK: ?
	name = info->name;

	lua_pushliteral(L, "sqlite3_function_scalars");					//STACK: ? 'sqlite3_function_scalars'
	lua_gettable(L, LUA_REGISTRYINDEX);								//STACK: ? sqlite3_function_scalars
	lua_pushstring(L, name);										//STACK: ? sqlite3_function_scalars name
	lua_pushnil(L);													//STACK: ? sqlite3_function_scalars name nil
	lua_settable(L, 4);												//STACK: ? sqlite3_function_scalars
	lua_pop(L, 1);													//STACK: ?

	sqlite3_free(info);
}

LUA_CFUNCTION_F lua_sqlite3_create_function_v2(lua_State *L)
{	//STACK: db name func|(step final)
	sqlite3 *db;
	const char *name;
	struct sqlite3_function_info *info;
	int doregister;

	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	db = (sqlite3 *)lua_touserdata(L, 1);
	name = luaL_checkstring(L, 2);
	switch(lua_type(L, 3))
	{
		case LUA_TFUNCTION:
			break;
		case LUA_TNIL:
			sqlite3db(db, create_function_v2, name, -1, SQLITE_UTF8, NULL, NULL, NULL, NULL, NULL);
			return 0;
		default:
			luaL_checktype(L, 3, LUA_TFUNCTION); //will always fail and throw proper error message
	}

	if (lua_gettop(L) > 3)
	{																	//STACK: db name step final
		luaL_checktype(L, 4, LUA_TFUNCTION);

		info = (struct sqlite3_function_info *)sqlite3_malloc(sizeof(struct sqlite3_function_info));
		info->L = L;
		info->name = name;

		lua_pushliteral(L, "sqlite3_function_aggregate_xsteps");		//STACK: db name step final 'sqlite3_function_aggregate_xsteps'
		lua_gettable(L, LUA_REGISTRYINDEX);								//STACK: db name step final sqlite3_function_aggregate_xsteps
		lua_pushvalue(L, 2);											//STACK: db name step final sqlite3_function_aggregate_xsteps name
		lua_pushvalue(L, 3);											//STACK: db name step final sqlite3_function_aggregate_xsteps name step
		lua_settable(L, 5);												//STACK: db name step final sqlite3_function_aggregate_xsteps
		lua_pop(L, 1);													//STACK: db name step final

		lua_pushliteral(L, "sqlite3_function_aggregate_xfinals");		//STACK: db name step final 'sqlite3_function_aggregate_xfinals'
		lua_gettable(L, LUA_REGISTRYINDEX);								//STACK: db name step final sqlite3_function_aggregate_xfinals
		lua_pushvalue(L, 2);											//STACK: db name step final sqlite3_function_aggregate_xfinals name
		lua_pushvalue(L, 4);											//STACK: db name step final sqlite3_function_aggregate_xfinals name final
		lua_settable(L, 5);												//STACK: db name step final sqlite3_function_aggregate_xfinals
		lua_pop(L, 1);													//STACK: db name step final

		lua_pushliteral(L, "sqlite3_function_aggregate_ctxts");			//STACK: db name step final 'sqlite3_function_aggregate_ctxts'
		lua_gettable(L, LUA_REGISTRYINDEX);								//STACK: db name step final sqlite3_function_aggregate_ctxts
		lua_pushvalue(L, 2);											//STACK: db name step final sqlite3_function_aggregate_ctxts name
		lua_gettable(L, 5);												//STACK: db name step final sqlite3_function_aggregate_ctxts ctxt
		doregister = lua_isnil(L, 6);
		lua_pop(L, 1);													//STACK: db name step final sqlite3_function_aggregate_ctxts
		lua_pushvalue(L, 2);											//STACK: db name step final sqlite3_function_aggregate_ctxts name
		lua_newtable(L);												//STACK: db name step final sqlite3_function_aggregate_ctxts name {~0}
		lua_settable(L, 5);												//STACK: db name step final sqlite3_function_aggregate_ctxts
		lua_pop(L, 1);													//STACK: db name step final

		if (doregister)
		{
			sqlite3db(db, create_function_v2, name, -1, SQLITE_UTF8, (void *)info, NULL, &sqlite3_function_aggregate_xstep, &sqlite3_function_aggregate_xfinal, &sqlite3_function_aggregate_destroy);
		}
	}
	else
	{																	//STACK: db name func
		info = (struct sqlite3_function_info *)sqlite3_malloc(sizeof(struct sqlite3_function_info));
		info->L = L;
		info->name = name;

		lua_pushliteral(L, "sqlite3_function_scalars");					//STACK: db name func 'sqlite3_function_scalars'
		lua_gettable(L, LUA_REGISTRYINDEX);								//STACK: db name func sqlite3_function_scalars
		lua_pushvalue(L, 2);											//STACK: db name func sqlite3_function_scalars name
		lua_gettable(L, 4);												//STACK: db name func sqlite3_function_scalars xfunc
		doregister = lua_isnil(L, 5);
		lua_pop(L, 1);													//STACK: db name func sqlite3_function_scalars
		lua_pushvalue(L, 2);											//STACK: db name func sqlite3_function_scalars name
		lua_pushvalue(L, 3);											//STACK: db name func sqlite3_function_scalars name func
		lua_settable(L, 4);												//STACK: db name func sqlite3_function_scalars
		lua_pop(L, 1);													//STACK: db name func

		if (doregister)
		{
			sqlite3db(db, create_function_v2, name, -1, SQLITE_UTF8, (void *)info, &sqlite3_function_scalar_xfunc, NULL, NULL, &sqlite3_function_scalar_destroy);
		}
	}

	return 0;
}

LUA_CFUNCTION_F lua_sqlite3_db_handle(lua_State* L)
{	//STACK: stmt ?
	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);

	lua_pushlightuserdata(L, (void *)sqlite3_db_handle((sqlite3_stmt *)lua_touserdata(L, 1)));	//STACK: stmt ? db

	return 1;	//RETURN: db
}

int sqlite3_exec_callback(void *arg, int cols, char **coltxts, char **colnames)
{	//STACK: db sql func throwonabort? throwonerror? ?
	lua_State *L = (lua_State *)arg;
	int i;
	int funcbase;

	lua_pushvalue(L, 3);						//STACK: db sql func throwonabort? throwonerror? ? func
	lua_createtable(L, cols, cols);				//STACK: db sql func throwonabort? throwonerror? ? func {~0}
	for (i = 0; i < cols; i++)
	{											//STACK: db sql func throwonabort? throwonerror? ? func {~0}
		lua_pushstring(L, colnames[i]);			//STACK: db sql func throwonabort? throwonerror? ? func {~0} colname
		lua_pushstring(L, coltxts[i]);			//STACK: db sql func throwonabort? throwonerror? ? func {~0} colname coltxt
		lua_settable(L, -3);					//STACK: db sql func throwonabort? throwonerror? ? func {~0}
		lua_pushinteger(L, (i + 1));			//STACK: db sql func throwonabort? throwonerror? ? func {~0} colindx
		lua_pushstring(L, coltxts[i]);			//STACK: db sql func throwonabort? throwonerror? ? func {~0} colindx coltxt
		lua_settable(L, -3);					//STACK: db sql func throwonabort? throwonerror? ? func {~0}
	}
												//STACK: db sql func throwonabort? throwonerror? ? func {~0}
	funcbase = lua_gettop(L);
	if (lua_pcall(L, 1, LUA_MULTRET, 0) != LUA_OK)	//STACK: db sql func throwonabort? throwonerror? ? errmsg?|continue? ?
	{												//STACK: db sql func throwonabort? throwonerror? ? errmsg
		lua_pushboolean(L, 1);						//STACK: db sql func throwonabort? throwonerror? ? errmsg true
		return -1; //signal that an error has occurred; errmsg, true are 'returned' to lua_sqlite3_exec
	}
	else
	{											//STACK: db sql func throwonabort? throwonerror? ? continue? ?
		if (lua_gettop(L) > funcbase)
		{										//STACK: db sql func throwonabort? throwonerror? ? continue ?
			lua_pop(L, lua_gettop(L) - (funcbase - 1));		//STACK: db sql func throwonabort? throwonerror? ? continue
			if (!lua_toboolean(L, -1))
			{									//STACK: db sql func throwonabort? throwonerror? ? false
				return -1; //signal that we no longer want to continue; false is 'returned' to lua_sqlite3_exec
			}

			lua_pop(L, 1);						//STACK: db sql func throwonabort? throwonerror? ?
		}
												//STACK: db sql func throwonabort? throwonerror? ?
	}
												//STACK: db sql func throwonabort? throwonerror? ?
	return 0; //signal that no error has occurred and we want to continue
}

LUA_CFUNCTION_F lua_sqlite3_exec(lua_State *L)
{	//STACK: db sql func? throwonabort? throwonerror? ?
	sqlite3 *db;
	const char *sql;
	int throwonabortindex;
	int throwonabort;
	int throwonerror;
	int (*callback)(void *, int, char **, char **);
	char *errmsg;
	const char *cerrmsg;

	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	db = (sqlite3 *)lua_touserdata(L, 1);
	sql = luaL_checkstring(L, 2);

	if (lua_gettop(L) > 2)
	{
		switch(lua_type(L, 3))
		{
			case LUA_TBOOLEAN:
				throwonabortindex = 3;
				callback = NULL;
				break;
			case LUA_TFUNCTION:
				throwonabortindex = 4;
				callback = &sqlite3_exec_callback;
				break;
			case LUA_TNIL:
				throwonabortindex = 4;
				callback = NULL;
				break;
			default:
				luaL_checktype(L, 3, LUA_TFUNCTION); //this will fail, sending the proper lua error
		}

		if (lua_gettop(L) >= throwonabortindex)
		{
			luaL_checktype(L, throwonabortindex, LUA_TBOOLEAN);
			throwonabort = throwonerror = lua_toboolean(L, throwonabortindex);

			if (!throwonabort && lua_gettop(L) > throwonabortindex)
			{
				luaL_checktype(L, (throwonabortindex + 1), LUA_TBOOLEAN);
				throwonerror = lua_toboolean(L, (throwonabortindex + 1));
			}
		}
	}
	else
	{
		throwonabort = 0;
		throwonerror = 0;
	}

	switch(sqlite3_exec(db, sql, callback, (void *)L, &errmsg))
	{
		case SQLITE_OK:					//STACK: db sql func? throwonabort? throwonerror? ?
		case_sqlite_ok:
			if (throwonabort || throwonerror)
			{
				return 0;
			}
			else
			{
				lua_pushboolean(L, 1);	//STACK: db sql func? throwonabort? throwonerror? ? true
				return 1;				//RETURN: true
			}
		case SQLITE_ABORT:				//STACK: db sql func throwonabort? throwonerror? ? errmsg? waserr
			if (!lua_toboolean(L, -1))
			{							//STACK: db sql func throwonabort? throwonerror? ? false
				lua_pop(L, 1);			//STACK: db sql func throwonabort? throwonerror? ?
				goto case_sqlite_ok;
			}
			lua_pop(L, 1);				//STACK: db sql func throwonabort? throwonerror? ? errmsg?

			if (throwonabort)
			{
				lua_error(L);			//ERROR: errmsg
			}
			else
			{
				lua_pushboolean(L, 0);	//STACK: db sql func throwonabort? throwonerror? ? errmsg false
				lua_insert(L, -2);		//STACK: db sql func throwonabort? throwonerror? ? false errmsg
				return 2;				//RETURN: false errmsg
			}
		default:						//STACK: db sql func? throwonabort? throwonerror? ?
			if(!throwonerror)
			{
				lua_pushboolean(L, 0);	//STACK: db sql func? throwonbusy? throwonerror? ? false
			}

			if (errmsg == NULL)
			{
				cerrmsg = sqlite3_errmsg(db);
			}
			lua_pushfstring(L, "sqlite3 error (%d): %s", sqlite3_extended_errcode(db), ((errmsg == NULL) ? cerrmsg : errmsg)); //STACK: db sql func? throwonbusy? throwonerror? ? false? errmsg
			if (errmsg != NULL)
			{
				sqlite3_free(errmsg);
			}

			if (throwonerror)
			{ 					//STACK: db sql func? throwonbusy? throwonerror? ? errmsg
				lua_error(L);	//ERROR: errmsg
			}
			else
			{					//STACK: db sql func? throwonbusy? throwonerror? ? false errmsg
				return 2;		//RETURN: false errmsg
			}
	}
}

LUA_CFUNCTION_F lua_sqlite3_finalize(lua_State *L)
{	//STACK: stmt ?
	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	sqlite3_finalize((sqlite3_stmt *)lua_touserdata(L, 1));
	return 0;
}

LUA_CFUNCTION_F lua_sqlite3_get_autocommit(lua_State *L)
{	//STACK: db ?
	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	lua_pushboolean(L, sqlite3_get_autocommit((sqlite3 *)lua_touserdata(L, 1)));	//STACK: db ? autocommit
	return 1;	//RETURN: autocommit
}

LUA_CFUNCTION_F lua_sqlite3_interrupt(lua_State *L)
{	//STACK: db ?
	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	sqlite3_interrupt((sqlite3 *)lua_touserdata(L, 1));
	return 0;
}

LUA_CFUNCTION_F lua_sqlite3_last_insert_rowid(lua_State *L)
{	//STACK: db ?
	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	lua_pushinteger(L, (lua_Integer)sqlite3_last_insert_rowid((sqlite3 *)lua_touserdata(L, 1)));	//STACK: db ? lastinsertrowid
	return 1;		//RETURN: lastinsertrowid
}

LUA_CFUNCTION_F lua_sqlite3_open_v2(lua_State *L)
{	//STACK: filename flags? ?
	sqlite3 *db;

	if (lua_gettop(L) > 1 && !lua_isnil(L, 2))
	{
		int flags = 0;
		const char *sflags = luaL_checkstring(L, 2);

		if (strchr(sflags, 'r'))
		{
			flags |= SQLITE_OPEN_READONLY;
		}
		else
		{
			flags |= SQLITE_OPEN_READWRITE;

			if (strchr(sflags, 'c'))
			{
				flags |= SQLITE_OPEN_CREATE;
			}
		}

		if (strchr(sflags, 'p'))
		{
			flags |= SQLITE_OPEN_PRIVATECACHE;
		}
		else
		{
			flags |= SQLITE_OPEN_SHAREDCACHE;
		}

		sqlite3(open_v2, luaL_checkstring(L, 1), &db, flags, NULL);
	}
	else
	{
		//NOTE: Because Lua is intended to be lightweight and efficient, SQLITE_OPEN_SHAREDCACHE is used as the default caching mode. This is not the default functionality in SQLite.
		sqlite3(open_v2, luaL_checkstring(L, 1), &db, (SQLITE_OPEN_READWRITE | SQLITE_OPEN_SHAREDCACHE), NULL);
	}
	sqlite3_busy_timeout(db, 250);

	lua_pushlightuserdata(L, (void *)db);	//STACK: filename flags? ? db
	return 1;								//RETURN: db
}

LUA_CFUNCTION_F lua_sqlite3_prepare_v2(lua_State *L)
{	//STACK: db sql throwonerror? ?
	sqlite3 *db;
	const char *sql;
	int throwonerror;
	sqlite3_stmt *stmt;

	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	db = (sqlite3 *)lua_touserdata(L, 1);
	sql = luaL_checkstring(L, 2);

	if (lua_gettop(L) > 2)
	{
		luaL_checktype(L, 3, LUA_TBOOLEAN);
		throwonerror = lua_toboolean(L, 3);
	}
	else
	{
		throwonerror = 1;
	}

	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK)
	{
		if (!throwonerror)
		{
			lua_pushboolean(L, 1);					//STACK: db slq throwonerror? ? true
		}
		lua_pushlightuserdata(L, (void *)stmt);		//STACK: db sql throwonerror? ? true? stmt
		return (throwonerror ? 1 : 2);				//RETURN: true? stmt
	}
	else
	{
		if (throwonerror)
		{
			luaL_error(L, "sqlite3 error (%d): %s", sqlite3_extended_errcode(db), sqlite3_errmsg(db));
		}
		else
		{
			lua_pushboolean(L, 0);					//STACK: db sql throwonerror? ? false
			lua_pushfstring(L, "sqlite3 error (%d): %s", sqlite3_extended_errcode(db), sqlite3_errmsg(db));	//STACK: db sql throwonerror? ? false errmsg
			return 2;								//RETURN: false errmsg
		}
	}
}

LUA_CFUNCTION_F lua_sqlite3_reset(lua_State *L)
{	//STACK: stmt ?
	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	sqlite3_reset((sqlite3_stmt *)lua_touserdata(L, 1));
	return 0;
}

LUA_CFUNCTION_F lua_sqlite3_result(lua_State *L)
{	//STACK: ctxt value ?
	sqlite3_context *ctxt;
	size_t lstrlen = 0;
	GenericMemoryBlob *blob;
	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	ctxt = (sqlite3_context *)lua_touserdata(L, 1);

	luaL_checkany(L, 2);
	switch(lua_type(L, 2))
	{
		case LUA_TNIL:
		case_lua_tnil:
			sqlite3_result_null(ctxt);
			break;
		case LUA_TNUMBER:
			sqlite3_result_double(ctxt, (double)lua_tonumber(L, 2));
			break;
		case LUA_TBOOLEAN:
			sqlite3_result_int(ctxt, lua_toboolean(L, 2));
			break;
		case LUA_TSTRING:
			sqlite3_result_text(ctxt, lua_tolstring(L, 2, &lstrlen), lstrlen, SQLITE_TRANSIENT);
			break;
		case LUA_TTABLE:
			lua_pushliteral(L, "sqlite3_dbnull");	//STACK: ctxt value ? 'sqlite3_dbnull'
			lua_gettable(L, LUA_REGISTRYINDEX);		//STACK: ctxt value ? sqlite3_dbnull
			lua_pushvalue(L, 2);					//STACK: ctxt value ? sqlite3_dbnull value
			if (lua_compare(L, -1, -2, LUA_OPEQ))
			{
				lua_pop(L, 1);						//STACK: ctxt value ? sqlite3_dbnull
				goto case_lua_tnil;
			}
			else
			{
				lua_replace(L, -2);						//STACK: ctxt value ? value
				goto case_default;
			}
		case LUA_TUSERDATA:
			blob = luablob_checkgmb(L, 2);
			sqlite3_result_blob(ctxt, (const void *)blob->data, blob->usedsize, SQLITE_TRANSIENT);
			break;
		default:
		case_default:
			luaL_error(L, "sqlite3 error: result value must be nil, a number, a boolean, a string, a userdata supporting the len operator, or dbnull");
	}

	return 0;
}

LUA_CFUNCTION_F lua_sqlite3_result_zeroblob(lua_State *L)
{	//STACK: ctxt size ?
	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	sqlite3_result_zeroblob((sqlite3_context *)lua_touserdata(L, 1), luaL_checkinteger(L, 2));
	return 0;
}

LUA_CFUNCTION_F lua_sqlite3_step(lua_State *L)
{	//STACK: stmt throwonerror? ?
	sqlite3_stmt *stmt;
	sqlite3* stmtdb;
	int throwonerror;

	luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
	stmt = (sqlite3_stmt *)lua_touserdata(L, 1);

	if (lua_gettop(L) > 1)
	{
		luaL_checktype(L, 2, LUA_TBOOLEAN);
		throwonerror = lua_toboolean(L, 2);
	}
	else
	{
		throwonerror = 0;
	}

	switch(sqlite3_step(stmt))
	{
		case SQLITE_OK:
			lua_pushboolean(L, 1);	//STACK: stmt throwonerror? ? true
			lua_pushboolean(L, 0);	//STACK: stmt throwonerror? ? true false
			return 2;				//RETURN: true false
		case SQLITE_ROW:
			lua_pushboolean(L, 1);	//STACK: stmt throwonerror? ? true
			lua_pushboolean(L, 1);	//STACK: stmt throwonerror? ? true true
			return 2;				//RETURN: true true
		case SQLITE_DONE:
			lua_pushboolean(L, 0);	//STACK: stmt throwonerror? ? false
			lua_pushboolean(L, 0);	//STACK: stmt throwonerror? ? false false
			return 2;				//RETURN: false false
		default:
			stmtdb = sqlite3_db_handle(stmt);

			if (throwonerror)
			{
				luaL_error(L, "sqlite3 error (%d): %s", sqlite3_extended_errcode(stmtdb), sqlite3_errmsg(stmtdb));
			}
			else
			{
				lua_pushboolean(L, 0);	//STACK: stmt throwonerror? ? false
				lua_pushfstring(L, "sqlite3 error (%d): %s", sqlite3_extended_errcode(stmtdb), sqlite3_errmsg(stmtdb)); //STACK: stmt throwonerror? ? false errmsg
				return 2;				//RETURN: false errmsg
			}
	}
}

const luaL_Reg funcs[] = {
	{"backup", &lua_sqlite3_backup},
	{"bind", &lua_sqlite3_bind},
	{"bindzeroblob", &lua_sqltie3_bind_zeroblob},
	{"blobbytes", &lua_sqlite3_blob_bytes},
	{"blobclose", &lua_sqlite3_blob_close},
	{"blobopen", &lua_sqlite3_blob_open},
	{"blobread", &lua_sqlite3_blob_read},
	{"blobreopen", &lua_sqlite3_blob_reopen},
	{"blobwrite", &lua_sqlite3_blob_write},
	{"changes", &lua_sqlite3_changes},
	{"clearbindings", &lua_sqlite3_clear_bindings},
	{"close", &lua_sqlite3_close},
	{"setcollationneededhandler", &lua_sqlite3_collation_needed},
	{"columns", &lua_sqlite3_columns},
	{"createcollation", &lua_sqlite3_create_collation_v2},
	{"createfunction", &lua_sqlite3_create_function_v2},
	{"dbhandle", &lua_sqlite3_db_handle},
	{"exec", &lua_sqlite3_exec},
	{"finalize", &lua_sqlite3_finalize},
	{"getautocommit", &lua_sqlite3_get_autocommit},
	{"interrupt", &lua_sqlite3_interrupt},
	{"getlastinsertrowid", &lua_sqlite3_last_insert_rowid},
	{"open", &lua_sqlite3_open_v2},
	{"prepare", &lua_sqlite3_prepare_v2},
	{"reset", &lua_sqlite3_reset},
	{"result", &lua_sqlite3_result},
	{"resultzeroblob", &lua_sqlite3_result_zeroblob},
	{"step", &lua_sqlite3_step},
	{NULL, NULL}
};

LUA_MODLOADER_F luaopen_sqlite3(lua_State *L)
{								//STACK: modname ?
	if (sqlite3_libversion_number() != SQLITE_VERSION_NUMBER)
	{
		luaL_error(L, "sqlite3 native library version ('%s') is not compatible with this sqlite3 native wrapper library; expected '%s'", sqlite3_libversion(), SQLITE_VERSION);
	}

	luaL_requiref(L, "blob-lua", luaopen_blob, 0 /* FALSE */);	//STACK: modname ? blob
	lua_pop(L, 1);												//STACK: modname ?

	lua_pushliteral(L, "sqlite3_function_aggregate_xsteps");	//STACK: modname ? 'sqlite3_function_aggregate_xsteps'
	lua_newtable(L);											//STACK: modname ? 'sqlite3_function_aggregate_xsteps' {~0}
	lua_settable(L, LUA_REGISTRYINDEX);							//STACK: modname ?

	lua_pushliteral(L, "sqlite3_function_aggregate_xfinals");	//STACK: modname ? 'sqlite3_function_aggregate_xfinals'
	lua_newtable(L);											//STACK: modname ? 'sqlite3_function_aggregate_xfinals' {~1}
	lua_settable(L, LUA_REGISTRYINDEX);							//STACK: modname ?

	lua_pushliteral(L, "sqlite3_function_aggregate_ctxts");		//STACK: modname ? 'sqlite3_function_aggregate_ctxts'
	lua_newtable(L);											//STACK: modname ? 'sqlite3_function_aggregate_ctxts' {~2}
	lua_settable(L, LUA_REGISTRYINDEX);							//STACK: modname ?

	lua_pushliteral(L, "sqlite3_function_scalars");				//STACK: modname ? 'sqlite3_function_scalars'
	lua_newtable(L);											//STACK: modname ? 'sqlite3_function_scalars' {~3}
	lua_settable(L, LUA_REGISTRYINDEX);							//STACK: modname ?
		
	luaL_newlib(L, funcs);										//STACK: modname ? {~5}

	lua_pushliteral(L, "dbnull");								//STACK: modname ? {~5} 'dbnull' {~4}
	lua_newtable(L);											//STACK: modname ? {~5} 'dbnull' {~4}
	lua_pushliteral(L, "sqlite3_dbnull");						//STACK: modname ? {~5} 'dbnull' {~4} 'sqlite3_dbnull'
	lua_pushvalue(L, -2);										//STACK: modname ? {~5} 'dbnull' {~4} 'sqlite3_dbnull' {~4}
	lua_settable(L, LUA_REGISTRYINDEX);							//STACK: modname ? {~5} 'dbnull' {~4}
	lua_settable(L, -3);										//STACK: modname ? {~5}

	lua_pushliteral(L, "sqlitever");							//STACK: modname ? {~5} 'sqlitever'
	lua_pushliteral(L, SQLITE_VERSION);							//STACK: modname ? {~5} 'sqlitever' version
	lua_settable(L, -3);										//STACK: modname ? {~5}

	return 1;					//RETURN: {~5}
}