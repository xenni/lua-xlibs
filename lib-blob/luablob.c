#define LUABLOB_LIB
#include "luablob.h"
#include <lauxlib.h>
#include <string.h>
#include <stdint.h>

#ifdef MSVC_VER
	#define LUA_CFUNCTION_F int __cdecl
#else
	#define LUA_CFUNCTION_F int
#endif

#define ptradd(p, o) ((void *)(((char *)(p)) + (o)))

LUA_CFUNCTION_F lua_luablob_mt___len(lua_State *L)
{	//STACK: gmb ?
	lua_pushinteger(L, (lua_Integer)(luablob_checkgmb(L, 1)->usedsize));	//STACK: u ? usedsize
	return 1;	//RETURN: usedsize
}

LUA_CFUNCTION_F lua_luablob_mt___eq(lua_State *L)
{	//STACK: gmba gmbb
	GenericMemoryBlob *gmba;
	GenericMemoryBlob *gmbb;

	gmba = (GenericMemoryBlob *)luaL_checkudata(L, 1, "luablob_mt");
	gmbb = (GenericMemoryBlob *)luaL_checkudata(L, 2, "luablob_mt");

	if (
		(gmba == gmbb) ||
		(
			(gmba->data != NULL) &&
			(gmba->data == gmbb->data)
		)
	)
	{
		lua_pushboolean(L, 1);	//STACK: gmba gmbb true
		return 1;				//RETURN: true
	}

	if (gmba->usedsize != gmbb->usedsize)
	{
		lua_pushboolean(L, 0);	//STACK: gmba gmbb false
		return 1;				//RETURN: false
	}

	lua_pushboolean(L, (memcmp(gmba, gmbb, gmba->usedsize) == 0));	//STACK: u v result
	return 1;					//RETURN: result
}

LUA_CFUNCTION_F lua_luablob_mt___tostring(lua_State *L)
{	//STACK: u ?
	GenericMemoryBlob *gmb;

	gmb = (GenericMemoryBlob *)luaL_checkudata(L, 1, "luablob_mt");
	if (gmb->data == NULL || gmb->usedsize == 0)
	{
		lua_pushliteral(L, "{empty blob}");	//STACK: u ? '{empty blob}'
	}
	else
	{
		lua_pushlstring(L, (const char *)gmb->data, gmb->usedsize);
	}

	return 1;	//RETURN: str
}

LUA_CFUNCTION_F  lua_luablob_mt___gc(lua_State *L)
{	//STACK: u ?
	GenericMemoryBlob *gmb;

	gmb = (GenericMemoryBlob *)luaL_checkudata(L, 1, "luablob_mt");
	if (gmb != NULL)
	{
		gmb_free(gmb);
	}

	return 1;
}

size_t luablob_lua_realloc_basic(GenericMemoryBlob *blob, size_t nsize)
{
	void *allocud = NULL;

	if ((nsize < blob->usedsize) || (nsize == 0))
	{
		return blob->allocsize;
	}

	blob->data = lua_getallocf(((lua_State *)blob->userdata), &allocud)(allocud, blob->data, blob->allocsize, nsize);
	return ((blob->data == NULL) ? 0 : nsize);
}
size_t luablob_lua_realloc_tight(GenericMemoryBlob *blob, size_t nsize)
{
	void *allocud = NULL;

	if (nsize == 0)
	{
		return blob->allocsize;
	}

	blob->data = lua_getallocf(((lua_State *)blob->userdata), &allocud)(allocud, blob->data, blob->allocsize, nsize);
	return ((blob->data == NULL) ? 0 : nsize);
}
size_t luablob_lua_realloc_loose(GenericMemoryBlob *blob, size_t nsize)
{
	void *allocud = NULL;
	size_t size;

	if ((nsize < blob->usedsize) || (nsize == 0))
	{
		return blob->allocsize;
	}

	//Round to nearest 4kb boundary.
	size = ((nsize ^ 0x1000) & 0x0FFF);

	blob->data = lua_getallocf(((lua_State *)blob->userdata), &allocud)(allocud, blob->data, blob->allocsize, size);
	if (blob->data == NULL)
	{
		if (blob->allocsize >= nsize)
		{
			return nsize;
		}
		else
		{
			return 0;
		}
	}

	return size;
}
void luablob_lua_free(GenericMemoryBlob *blob)
{
	void *allocud = NULL;
	lua_getallocf(((lua_State *)blob->userdata), &allocud)(allocud, blob->data, blob->allocsize, 0);
}

LUABLOB_API(void) luablob_newgmb(lua_State *L, GenericMemoryBlob *gmb, size_t initialsize, const char *allocmode)
{
	if (initialsize <= 0)
	{
		luaL_error(L, "argument out of range; initial size must be greater than 0.");
	}

	if (strcmp(allocmode, "basic") == 0)
	{
		gmb->realloc = &luablob_lua_realloc_basic;
	}
	else if (strcmp(allocmode, "tight") == 0)
	{
		gmb->realloc = &luablob_lua_realloc_tight;
	}
	else if (strcmp(allocmode, "loose") == 0)
	{
		gmb->realloc = &luablob_lua_realloc_loose;
	}
	else
	{
		luaL_error(L, "invalid argument; allocation mode '%s' is not supported; valid values are 'basic', 'tight', 'loose'");
	}

	gmb->free = &luablob_lua_free;
	gmb->usedsize = 0;
	gmb->userdata = (void *)L;
	gmb->allocsize = 0;
	gmb->usedsize = 0;
	gmb->data = NULL;

	if (!gmb_realloc(gmb, initialsize))
	{
		luaL_error(L, "failed to allocate initial blob memory");
	}
}

LUA_CFUNCTION_F lua_blob_newblob(lua_State *L)
{	//STACK: initialsize? allocmode? ?
	size_t initialsize;
	const char *allocmode;
	GenericMemoryBlob gmb;

	if (lua_gettop(L) > 0)
	{
		switch (lua_type(L, 1))
		{
			case LUA_TNUMBER:
				initialsize = luaL_checkint(L, 1);
				if (lua_gettop(L) > 1)
				{
					allocmode = luaL_checkstring(L, 2);
				}
				else
				{
					allocmode = "basic";
				}
				break;
			case LUA_TSTRING:
				allocmode = luaL_checkstring(L, 1);
				initialsize = sizeof(lua_Number);
				break;
			default:
				luaL_error(L, "invalid argument; expected allocation size (number) or allocation mode (string)");
		}
	}
	else
	{
		initialsize = sizeof(lua_Number);
		allocmode = "basic";
	}

	luablob_newgmb(L, &gmb, initialsize, allocmode);
	luablob_pushgmb(L, gmb);	//STACK: initialsize? allocmode? ? gmb

	return 1;					//RETURN: gmb
}

void lua_blob_read_type(lua_State *L, GenericMemoryBlob *gmb, unsigned int *offset, const char *type)
{	//STACK:	start:	?
	//			end:	? value
	unsigned int size;
	unsigned int i;
	const char *str;

	luaL_checkstack(L, 1, NULL);

	if (strcmp(type, "cstr") == 0)
	{
		str = (const char *)gmb->data;
		for (i = *offset; i < gmb->usedsize; i += sizeof(char))
		{
			if (*(str + i) == '\0')
			{
				lua_pushstring(L, (str + *offset));
				return;
			}
		}

		luaL_error(L, "unable to read data; bounds out of range");
	}
	else if (strcmp(type, "u8str") == 0)
	{
		if ((*offset + sizeof(uint8_t)) > gmb->usedsize)
		{
			luaL_error(L, "unable to read data; bounds out of range");
		}
		size = *((uint8_t *)ptradd(gmb->data, *offset));
		*offset += sizeof(uint8_t);

		if ((*offset + size) > gmb->usedsize)
		{
			luaL_error(L, "unable to read data; string length out of range");
		}
		lua_pushlstring(L, ((const char *)ptradd(gmb->data, *offset)), (size_t)size);
		*offset += size;
	}
	else if (strcmp(type, "u16str") == 0)
	{
		if ((*offset + sizeof(uint16_t)) > gmb->usedsize)
		{
			luaL_error(L, "unable to read data; bounds out of range");
		}
		size = *((uint16_t *)ptradd(gmb->data, *offset));
		*offset += sizeof(uint16_t);

		if ((*offset + size) > gmb->usedsize)
		{
			luaL_error(L, "unable to read data; string length out of range");
		}
		lua_pushlstring(L, ((const char *)ptradd(gmb->data, *offset)), (size_t)size);
		*offset += size;
	}
	else if (strcmp(type, "u32str") == 0)
	{
		if ((*offset + sizeof(uint32_t)) > gmb->usedsize)
		{
			luaL_error(L, "unable to read data; bounds out of range");
		}
		size = *((uint32_t *)ptradd(gmb->data, *offset));
		*offset += sizeof(uint32_t);

		if ((*offset + size) > gmb->usedsize)
		{
			luaL_error(L, "unable to read data; string length out of range");
		}
		lua_pushlstring(L, ((const char *)ptradd(gmb->data, *offset)), (size_t)size);
		*offset += size;
	}
	else if (strcmp(type, "char") == 0)
	{
		if ((*offset + sizeof(char)) > gmb->usedsize)
		{
			luaL_error(L, "unable to read data; bounds out of range");
		}
		lua_pushlstring(L, ((const char *)ptradd(gmb->data, *offset)), sizeof(char));
		*offset += sizeof(char);
	}
	else if (strcmp(type, "i8") == 0)
	{
		if ((*offset + sizeof(int8_t)) > gmb->usedsize)
		{
			luaL_error(L, "unable to read data; bounds out of range");
		}
		lua_pushinteger(L, (lua_Integer)(*((int8_t *)ptradd(gmb->data, *offset))));
		*offset += sizeof(char);
	}
	else if (strcmp(type, "u8") == 0)
	{
		if ((*offset + sizeof(uint8_t)) > gmb->usedsize)
		{
			luaL_error(L, "unable to read data; bounds out of range");
		}
		lua_pushunsigned(L, (lua_Unsigned)(*((uint8_t *)ptradd(gmb->data, *offset))));
		*offset += sizeof(uint8_t);
	}
	else if (strcmp(type, "i16") == 0)
	{
		if ((*offset + sizeof(int16_t)) > gmb->usedsize)
		{
			luaL_error(L, "unable to read data; bounds out of range");
		}
		lua_pushinteger(L, (lua_Integer)(*((int16_t *)ptradd(gmb->data, *offset))));
		*offset += sizeof(int16_t);
	}
	else if (strcmp(type, "u16") == 0)
	{
		if ((*offset + sizeof(uint16_t)) > gmb->usedsize)
		{
			luaL_error(L, "unable to read data; bounds out of range");
		}
		lua_pushunsigned(L, (lua_Unsigned)(*((uint16_t *)ptradd(gmb->data, *offset))));
		*offset += sizeof(uint16_t);
	}
	else if (strcmp(type, "i32") == 0)
	{
		if ((*offset + sizeof(int32_t)) > gmb->usedsize)
		{
			luaL_error(L, "unable to read data; bounds out of range");
		}
		lua_pushinteger(L, (lua_Integer)(*((int32_t *)ptradd(gmb->data, *offset))));
		*offset += sizeof(int32_t);
	}
	else if (strcmp(type, "u32") == 0)
	{
		if ((*offset + sizeof(uint32_t)) > gmb->usedsize)
		{
			luaL_error(L, "unable to read data; bounds out of range");
		}
		lua_pushunsigned(L, (lua_Unsigned)(*((uint32_t *)ptradd(gmb->data, *offset))));
		*offset += sizeof(uint32_t);
	}
	else if (strcmp(type, "i64") == 0)
	{
		if ((*offset + sizeof(int64_t)) > gmb->usedsize)
		{
			luaL_error(L, "unable to read data; bounds out of range");
		}
		lua_pushinteger(L, (lua_Integer)(*((int64_t *)ptradd(gmb->data, *offset))));
		*offset += sizeof(int64_t);
	}
	else if (strcmp(type, "u64") == 0)
	{
		if ((*offset + sizeof(uint64_t)) > gmb->usedsize)
		{
			luaL_error(L, "unable to read data; bounds out of range");
		}
		lua_pushunsigned(L, (lua_Unsigned)(*((uint64_t *)ptradd(gmb->data, *offset))));
		*offset += sizeof(uint64_t);
	}
	else if (strcmp(type, "float") == 0)
	{
		if ((*offset + sizeof(float)) > gmb->usedsize)
		{
			luaL_error(L, "unable to read data; bounds out of range");
		}
		lua_pushnumber(L, (lua_Number)(*((float *)ptradd(gmb->data, *offset))));
		*offset += sizeof(float);
	}
	else if (strcmp(type, "double") == 0)
	{
		if ((*offset + sizeof(double)) > gmb->usedsize)
		{
			luaL_error(L, "unable to read data; bounds out of range");
		}
		lua_pushnumber(L, (lua_Number)(*((double *)ptradd(gmb->data, *offset))));
		*offset += sizeof(double);
	}
	else
	{
		luaL_error(L, "unrecognized datatype specifier '%s'", type);
	}
}

LUA_CFUNCTION_F lua_blob_read(lua_State *L)
{	//STACK: gmb infos...
	GenericMemoryBlob *gmb;
	GenericMemoryBlob *destblob;
	int i;
	int hasoffset;
	unsigned int offset;
	unsigned int destoffset;
	size_t size;
	int count;
	int results;
	const char *type;

	gmb = luablob_checkgmb(L, 1);

	offset = 0;
	results = 0;
	count = lua_gettop(L);
	type = NULL;
	for(i = 2; i <= count; ++i)
	{
		switch(lua_type(L, i))
		{
			case LUA_TNUMBER:
				offset += lua_tointeger(L, i);
				break;
			case LUA_TSTRING:
				type = lua_tostring(L, i);
				lua_blob_read_type(L, gmb, &offset, type);	//STACK: gmb infos... values... value
				++results;
				break;
			case LUA_TTABLE:
				luaL_checkstack(L, 1, NULL);

				lua_pushliteral(L, "type");				//STACK: gmb infos... values... 'type'
				lua_gettable(L, i);						//STACK: gmb infos... values... type
				type = (lua_isnil(L, -1) ? NULL : luaL_checkstring(L, -1));
				lua_pop(L, 1);							//STACK: gmb infos... values...

				hasoffset = 0;
				lua_pushliteral(L, "offset");			//STACK: gmb infos... values... 'offset'
				lua_gettable(L, i);						//STACK: gmb infos... values... offset
				if (!lua_isnil(L, -1))
				{
					hasoffset = 1;
					offset += lua_tounsigned(L, -1);
				}
				lua_pop(L, 1);							//STACK: gmb infos... values...

				lua_pushliteral(L, "pos");				//STACK: gmb infos... values... 'pos'
				lua_gettable(L, i);						//STACK: gmb infos... values... pos
				if (!lua_isnil(L, -1))
				{
					if (hasoffset)
					{
						luaL_error(L, "a read information table may not contain both a position and an offset");
					}
					offset = lua_tounsigned(L, -1);
				}
				lua_pop(L, 1);							//STACK: gmb infos... values...

				if (type != NULL)
				{
					if (strcmp(type, "str") == 0)
					{
						lua_pushliteral(L, "len");			//STACK: gmb infos... values... 'len'
						lua_gettable(L, i);					//STACK: gmb infos... values... len
						size = luaL_checkunsigned(L, -1);
						lua_pop(L, 1);						//STACK: gmb infos... values...

						if ((offset + size) > gmb->usedsize)
						{
							luaL_error(L, "unable to read data; bounds out of range");
						}

						lua_pushlstring(L, ((const char *)ptradd(gmb->data, offset)), size);	//STACK: gmb infos... values... value
						offset += size;
						++results;
					}
					else if (strcmp(type, "blob") == 0)
					{
						luaL_checkstack(L, 3, NULL);

						lua_pushcfunction(L, &lua_blob_newblob);	//STACK: gmb infos... values... newblob
						lua_pushliteral(L, "len");			//STACK: gmb infos... values... newblob 'len'
						lua_gettable(L, i);					//STACK: gmb infos... values... newblob len
						size = luaL_checkunsigned(L, -1);

						if ((offset + size) > gmb->usedsize)
						{
							luaL_error(L, "unable to read data; bounds out of range");
						}

						lua_pushliteral(L, "dest");			//STACK: gmb infos... values... newblob len 'dest'
						lua_gettable(L, i);					//STACK: gmb infos... values... newblob len dest
						if (lua_isnil(L, -1))
						{
							lua_pop(L, 1);					//STACK: gmb infos... values... newblob len
							lua_pushliteral(L, "mode");		//STACK: gmb infos... values... newblob len 'mode'
							lua_gettable(L, i);				//STACK: gmb infos... values... newblob len mode
							if (lua_isnil(L, -1))
							{
								lua_pop(L, 1);				//STACK: gmb infos... values.. newblob len
								lua_call(L, 1, 1);			//STACK: gmb infos... values.. value
							}
							else
							{
								lua_call(L, 2, 1);			//STACK: gmb infos... values.. value
							}

							destblob = (GenericMemoryBlob *)lua_touserdata(L, -1);
							destblob->usedsize = size;
							memcpy(destblob->data, ptradd(gmb->data, offset), size);
							++results;
						}
						else
						{
							destblob = luablob_checkgmb(L, -1);
							lua_pop(L, 2);					//STACK: gmb infos... values...

							lua_pushliteral(L, "start");	//STACK: gmb infos... values... 'start'
							lua_gettable(L, i);				//STACK: gmb infos... values... start
							destoffset = (lua_isnil(L, -1) ? 0 : luaL_checkunsigned(L, -1));
							lua_pop(L, 1);					//STACK: gmb infos... values...

							if (destoffset > destblob->usedsize)
							{
								luaL_error(L, "destination blob does not contain read start offset");
							}
							if (gmb_resize(destblob, (size + destoffset), 0 /* FALSE */) == 0)
							{
								luaL_error(L, "failed to allocate blob memory");
							}

							memcpy(ptradd(destblob->data, destoffset), ptradd(gmb->data, offset), size);
						}
						offset += size;
					}
					else
					{
						lua_blob_read_type(L, gmb, &offset, type);	//STACK: gmb infos... values... value
						++results;
					}
				}
				break;
			default:
				luaL_error(L, "invalid argument; a read information item must be an offset delta integer, a datatype specifier string, or a read information table");
		}
	}

	return results;
}

LUA_CFUNCTION_F lua_blob_write(lua_State *L)
{
	//STACK: gmb infos...
	GenericMemoryBlob *gmb;
	GenericMemoryBlob *srcblob;
	unsigned int offset;
	unsigned int srcoffset;
	const char *data;
	const char *type;
	size_t size;
	int count;
	int i;
	int hasoffset;
	size_t j;

	gmb = luablob_checkgmb(L, 1);

	offset = 0;
	count = lua_gettop(L);
	for(i = 2; i <= count; ++i)
	{
		switch(lua_type(L, i))
		{
			case LUA_TNUMBER:
				offset += lua_tointeger(L, i);
				break;
			case LUA_TSTRING:
				if (offset > gmb->usedsize)
				{
					luaL_error(L, "destination blob does not contain write start offset");
				}

				data = lua_tolstring(L, i, &size);
				if (size != 0)
				{
					if (gmb_resize(gmb, (offset + size), 0 /* FALSE */) == 0)
					{
						luaL_error(L, "failed to allocate blob memory");
					}

					memcpy(ptradd(gmb->data, offset), data, size);
					offset += size;
				}
				break;
			case LUA_TTABLE:
				luaL_checkstack(L, 1, NULL);
				lua_pushliteral(L, "type");			//STACK: gmb infos... 'type'
				lua_gettable(L, i);					//STACK: gmb infos... type
				type = (lua_isnil(L, -1) ? NULL : luaL_checkstring(L, -1));
				lua_pop(L, 1);						//STACK: gmb infos...

				hasoffset = 0;
				lua_pushliteral(L, "offset");		//STACK: gmb infos... 'offset'
				lua_gettable(L, i);					//STACK: gmb infos... offset
				if (!lua_isnil(L, -1))
				{
					hasoffset = 1;
					offset += lua_tointeger(L, -1);
				}
				lua_pop(L, 1);						//STACK: gmb infos...

				lua_pushliteral(L, "pos");			//STACK: gmb infos... 'pos'
				lua_gettable(L, i);					//STACK: gmb infos... pos
				if (!lua_isnil(L, -1))
				{
					if (hasoffset)
					{
						luaL_error(L, "a write information table may not contain both a position and an offset");
					}
					offset = luaL_checkunsigned(L, -1);
				}
				lua_pop(L, 1);						//STACK: gmb infos...

				if (offset > gmb->usedsize)
				{
					luaL_error(L, "destination blob does not contain write start offset");
				}

				if (type != NULL)
				{
					lua_pushliteral(L, "value");		//STACK: gmb infos... 'value'
					lua_gettable(L, i);					//STACK: gmb infos... value
					if (!lua_isnil(L, -1))
					{
						if (strcmp(type, "cstr") == 0)
						{
							data = luaL_checklstring(L, -1, &size);
							if (size > 1)
							{
								for(j = 0; j < (size - 1); ++j)
								{
									if (data[j] == '\0')
									{
										luaL_error(L, "specified cstr contains an internal null character");
									}
								}

								if (gmb_resize(gmb, (offset + size), 0 /* FALSE */) == 0)
								{
									luaL_error(L, "failed to allocate blob memory");
								}
								memcpy(ptradd(gmb->data, offset), data, size);
								offset += size;
							}
						}
						else if (strcmp(type, "u8str") == 0)
						{
							data = luaL_checklstring(L, -1, &size);
							if (size > UINT8_MAX)
							{
								luaL_error(L, "string too large to be represented by u8str");
							}
							if (gmb_resize(gmb, (offset + sizeof(uint8_t) + size), 0 /* FALSE */) == 0)
							{
								luaL_error(L, "failed to allocate blob memory");
							}

							*((uint8_t *)ptradd(gmb->data, offset)) = (uint8_t)size;
							offset += sizeof(uint8_t);

							memcpy(ptradd(gmb->data, offset), data, size);
							offset += size;
						}
						else if (strcmp(type, "u16str") == 0)
						{
							data = luaL_checklstring(L, -1, &size);
							if (size > UINT16_MAX)
							{
								luaL_error(L, "string too large to be represented by u16str");
							}
							if (gmb_resize(gmb, (offset + sizeof(uint16_t) + size), 0 /* FALSE */) == 0)
							{
								luaL_error(L, "failed to allocate blob memory");
							}

							*((uint16_t *)ptradd(gmb->data, offset)) = (uint16_t)size;
							offset += sizeof(uint16_t);

							memcpy(ptradd(gmb->data, offset), data, size);
							offset += size;
						}
						else if (strcmp(type, "u32str") == 0)
						{
							data = luaL_checklstring(L, -1, &size);
							if (size > UINT32_MAX)
							{
								luaL_error(L, "string too large to be represented by u32str");
							}
							if (gmb_resize(gmb, (offset + sizeof(uint32_t) + size), 0 /* FALSE */) == 0)
							{
								luaL_error(L, "failed to allocate blob memory");
							}

							*((uint32_t *)ptradd(gmb->data, offset)) = (uint32_t)size;
							offset += sizeof(uint32_t);

							memcpy(ptradd(gmb->data, offset), data, size);
							offset += size;
						}
						else if (strcmp(type, "char") == 0)
						{
							data = luaL_checklstring(L, -1, &size);
							if (size != 0)
							{
								lua_checkstack(L, 1);
								lua_pushliteral(L, "index");	//STACK: gmb infos... value 'index'
								lua_gettable(L, i);				//STACK: gmb infos... value index
								srcoffset = (lua_isnil(L, -1) ? 0 : luaL_checkunsigned(L, -1));
								lua_pop(L, 1);					//STACK: gmb infos... value
								if (srcoffset >= size)
								{
									luaL_error(L, "start index out of range");
								}

								if (gmb_resize(gmb, (offset + sizeof(char)), 0 /* FALSE */) == 0)
								{
									luaL_error(L, "failed to allocate blob memory");
								}

								*((char *)ptradd(gmb->data, offset)) = data[srcoffset];
								offset += sizeof(char);
							}
						}
						else if (strcmp(type, "i8") == 0)
						{
							if (gmb_resize(gmb, (offset + sizeof(int8_t)), 0 /* FALSE */) == 0)
							{
								luaL_error(L, "failed to allocate blob memory");
							}

							*((int8_t *)ptradd(gmb->data, offset)) = (int8_t)luaL_checkinteger(L, -1);
							offset += sizeof(int8_t);
						}
						else if (strcmp(type, "u8") == 0)
						{
							if (gmb_resize(gmb, (offset + sizeof(uint8_t)), 0 /* FALSE */) == 0)
							{
								luaL_error(L, "failed to allocate blob memory");
							}

							*((uint8_t *)ptradd(gmb->data, offset)) = (uint8_t)luaL_checkunsigned(L, -1);
							offset += sizeof(uint8_t);
						}
						else if (strcmp(type, "i16") == 0)
						{
							if (gmb_resize(gmb, (offset + sizeof(int16_t)), 0 /* FALSE */) == 0)
							{
								luaL_error(L, "failed to allocate blob memory");
							}

							*((int16_t *)ptradd(gmb->data, offset)) = (int16_t)luaL_checkinteger(L, -1);
							offset += sizeof(int16_t);
						}
						else if (strcmp(type, "u16") == 0)
						{
							if (gmb_resize(gmb, (offset + sizeof(uint16_t)), 0 /* FALSE */) == 0)
							{
								luaL_error(L, "failed to allocate blob memory");
							}

							*((uint16_t *)ptradd(gmb->data, offset)) = (uint16_t)luaL_checkunsigned(L, -1);
							offset += sizeof(uint16_t);
						}
						else if (strcmp(type, "i32") == 0)
						{
							if (gmb_resize(gmb, (offset + sizeof(int32_t)), 0 /* FALSE */) == 0)
							{
								luaL_error(L, "failed to allocate blob memory");
							}

							*((int32_t *)ptradd(gmb->data, offset)) = (int32_t)luaL_checkinteger(L, -1);
							offset += sizeof(int32_t);
						}
						else if (strcmp(type, "u32") == 0)
						{
							if (gmb_resize(gmb, (offset + sizeof(uint32_t)), 0 /* FALSE */) == 0)
							{
								luaL_error(L, "failed to allocate blob memory");
							}

							*((uint16_t *)ptradd(gmb->data, offset)) = (uint32_t)luaL_checkunsigned(L, -1);
							offset += sizeof(uint32_t);
						}
						else if (strcmp(type, "i64") == 0)
						{
							if (gmb_resize(gmb, (offset + sizeof(int64_t)), 0 /* FALSE */) == 0)
							{
								luaL_error(L, "failed to allocate blob memory");
							}

							*((int64_t *)ptradd(gmb->data, offset)) = (int64_t)luaL_checkinteger(L, -1);
							offset += sizeof(int64_t);
						}
						else if (strcmp(type, "u64") == 0)
						{
							if (gmb_resize(gmb, (offset + sizeof(uint64_t)), 0 /* FALSE */) == 0)
							{
								luaL_error(L, "failed to allocate blob memory");
							}

							*((uint64_t *)ptradd(gmb->data, offset)) = (uint64_t)luaL_checkunsigned(L, -1);
							offset += sizeof(uint64_t);
						}
						else if (strcmp(type, "float") == 0)
						{
							if (gmb_resize(gmb, (offset + sizeof(float)), 0 /* FALSE */) == 0)
							{
								luaL_error(L, "failed to allocate blob memory");
							}

							*((float *)ptradd(gmb->data, offset)) = (float)luaL_checknumber(L, -1);
							offset += sizeof(float);
						}
						else if (strcmp(type, "double") == 0)
						{
							if (gmb_resize(gmb, (offset + sizeof(double)), 0 /* FALSE */) == 0)
							{
								luaL_error(L, "failed to allocate blob memory");
							}

							*((double *)ptradd(gmb->data, offset)) = (double)luaL_checknumber(L, -1);
							offset += sizeof(double);
						}
						else if (strcmp(type, "str") == 0)
						{
							data = luaL_checklstring(L, -1, &size);
							if (size != 0)
							{
								if (gmb_resize(gmb, (offset + size), 0 /* FALSE */) == 0)
								{
									luaL_error(L, "failed to allocate blob memory");
								}

								memcpy(ptradd(gmb->data, offset), data, size);
								offset += size;
							}
						}
						else if (strcmp(type, "blob") == 0)
						{
							srcblob = luablob_checkgmb(L, -1);
							if (srcblob->usedsize != 0)
							{
								lua_checkstack(L, 2);
								lua_pushliteral(L, "start");	//STACK: gmb infos... value 'start'
								lua_gettable(L, i);				//STACK: gmb infos... value start
								if (lua_isnil(L, -1))
								{
									srcoffset = 0;
								}
								else
								{
									srcoffset = luaL_checkint(L, -1);
								}
								lua_pop(L, 1);					//STACK: gmb infos... value

								lua_pushliteral(L, "count");	//STACK: gmb infos... value 'count'
								lua_gettable(L, i);				//STACK: gmb infos... value count
								if (lua_isnil(L, -1))
								{
									size = (srcblob->usedsize - srcoffset);
								}
								else
								{
									size = (size_t)luaL_checkint(L, -1);
								}
								lua_pop(L, 1);					//STACK: gmb infos... value

								if ((srcoffset + size) > srcblob->usedsize)
								{
									luaL_error(L, "access to value luablob was out of bounds");
								}

								if (gmb_resize(gmb, (offset + size), 0 /* FALSE */) == 0)
								{
									luaL_error(L, "failed to allocate blob memory");
								}

								memcpy(ptradd(gmb->data, offset), ptradd(srcblob->data, srcoffset), size);
								offset += size;
							}
						}
						else
						{
							luaL_error(L, "unrecognized datatype specifier '%s'", type);
						}
					}

					lua_pop(L, 1);	//STACK: gmb infos...
				}
				break;
			case LUA_TNIL:
				break;
			default:
				luaL_error(L, "invalid argument; a write information item must be nil, an offset delta integer, a data string, or a write information table");
		}
	}

	return 0;
}

LUA_CFUNCTION_F lua_blob_clear(lua_State *L)
{	//STACK: gmb start? count?
	GenericMemoryBlob *gmb;
	int start;
	size_t count;
	size_t size;

	gmb = luablob_checkgmb(L, 1);

	if (lua_gettop(L) > 1)
	{
		start = luaL_checkint(L, 2);
		if (start < 0)
		{
			luaL_error(L, "argument out of range; start index must be non-negative");
		}

		if (lua_gettop(L) > 2)
		{
			count = (size_t)luaL_checkunsigned(L, 3);

			size = (start + count);
			if (size > gmb->usedsize)
			{
				if (gmb_resize(gmb, size, 0 /* FALSE */) == 0)
				{
					luaL_error(L, "failed to allocate blob memory");
				}
			}
		}
		else
		{
			count = (gmb->usedsize - start);
		}
	}
	else
	{
		start = 0;
		count = gmb->usedsize;
	}

	memset(ptradd(gmb->data, start), 0, count);

	return 0;
}

LUA_CFUNCTION_F lua_blob_resize(lua_State *L)
{	//STACK: gmb newsize trim? ?
	GenericMemoryBlob *gmb;
	size_t nsize;
	int trim;

	gmb = luablob_checkgmb(L, 1);
	nsize = (size_t)luaL_checkunsigned(L, 2);

	if (nsize == 0)
	{
		luaL_error(L, "invalid argument: new size must be greater than 0.");
	}

	if (lua_gettop(L) > 2)
	{
		luaL_checktype(L, 3, LUA_TBOOLEAN);
		trim = lua_toboolean(L, 3);
	}
	else
	{
		trim = 0;
	}

	if (gmb_resize(gmb, nsize, trim) == 0)
	{
		luaL_error(L, "failed to allocate blob memory");
	}

	return 0;
}

LUA_CFUNCTION_F lua_blob_freeblob(lua_State *L)
{	//STACK: gmb ?
	gmb_free(luablob_checkgmb(L, 1));

	return 0;	//RETURN
}

LUABLOB_API(void) luablob_pushgmb(lua_State *L, GenericMemoryBlob blob)
{	//STACK: ?
	GenericMemoryBlob *luablob;

	luaL_checkstack(L, 2, NULL);

	luablob = (GenericMemoryBlob *)lua_newuserdata(L, sizeof(GenericMemoryBlob));	//STACK: ? blob
	*luablob = blob;

	lua_pushliteral(L, "luablob_mt");	//STACK: ? blob 'luablob_mt'
	lua_gettable(L, LUA_REGISTRYINDEX);	//STACK: ? blob luablob_mt
	lua_setmetatable(L, -2);			//STACK: ? blob
}

LUABLOB_API(int) luablob_isgmb(lua_State *L, int index)
{	//STACK: ? blob
	luaL_checkstack(L, 2, NULL);

	lua_getmetatable(L, -1);			//STACK: ? blob blob_mt
	lua_pushliteral(L, "luablob_mt");	//STACK: ? blob blob_mt 'luablob_mt'
	lua_gettable(L, LUA_REGISTRYINDEX);	//STACK: ? blob blob_mt luablob_mt
	if (lua_compare(L, -1, -2, LUA_OPEQ))
	{
		lua_pop(L, 2); //STACK: ? blob

		if (((GenericMemoryBlob *)lua_touserdata(L, -1))->data == NULL)
		{
			return 0;	//FALSE (it is freed)
		}
		else
		{
			return 1;	//TRUE
		}
	}
	else
	{
		lua_pop(L, 2); //STACK: ? blob
		return 0;  //FALSE
	}
}

LUABLOB_API(GenericMemoryBlob *) luablob_togmb(lua_State *L, int index)
{	//STACK: ? blob
	luaL_checkstack(L, 2, NULL);

	lua_getmetatable(L, -1);			//STACK: ? blob blob_mt
	lua_pushliteral(L, "luablob_mt");	//STACK: ? blob blob_mt 'luablob_mt'
	lua_gettable(L, LUA_REGISTRYINDEX);	//STACK: ? blob blob_mt luablob_mt
	if (lua_compare(L, -1, -2, LUA_OPEQ))
	{
		lua_pop(L, 2); //STACK: ? blob
		return (GenericMemoryBlob *)lua_touserdata(L, -1);
	}
	else
	{
		lua_pop(L, 2); //STACK: ? blob
		return NULL;
	}
}

LUABLOB_API(GenericMemoryBlob *) luablob_checkgmb(lua_State *L, int index)
{	//STACK: ? blob
	GenericMemoryBlob *gmb;

	gmb = (GenericMemoryBlob *)luaL_checkudata(L, index, "luablob_mt");
	if (gmb->data == NULL)
	{
		luaL_error(L, "unable to use freed blob");
	}

	//STACK: ? blob
	return gmb;
}

LUABLOB_API(int) gmb_resize(GenericMemoryBlob *blob, size_t nsize, int trim)
{
	int result;
	if ((nsize > blob->usedsize) || trim)
	{
		result = gmb_realloc(blob, nsize);
		if (nsize > blob->usedsize)
		{
			memset(ptradd(blob->data, blob->usedsize), 0, (nsize - blob->usedsize));
		}
		if (result != 1)
		{
			return result;
		}
	}

	blob->usedsize = nsize;
	return 1;
}

LUABLOB_API(int) gmb_realloc(GenericMemoryBlob *blob, size_t nsize)
{
	size_t allocsize;

	if (nsize == 0)
	{
		gmb_free(blob);
		return -1;
	}

	allocsize = blob->realloc(blob, nsize);
	if (allocsize == 0)
	{
		return 0;
	}
	else
	{
		blob->allocsize = allocsize;

		if (blob->usedsize > nsize)
		{
			blob->usedsize = nsize;
		}

		return 1;
	}
}

LUABLOB_API(void) gmb_free(GenericMemoryBlob *blob)
{
	if (blob->free != NULL)
	{
		blob->free(blob);
	}

	blob->data = NULL;
	blob->free = NULL;
	blob->realloc = NULL;
	blob->usedsize = 0;
	blob->allocsize = 0;
}

const luaL_Reg luablob_mt_funcs[] = {
	{"__len", &lua_luablob_mt___len},
	{"__eq", &lua_luablob_mt___eq},
	{"__gc", &lua_luablob_mt___gc},
	{"__tostring", &lua_luablob_mt___tostring},
	{NULL, NULL}
};

const luaL_Reg luablob_mt___index_funcs[] =
{
	{"read", &lua_blob_read},
	{"write", &lua_blob_write},
	{"clear", &lua_blob_clear},
	{"resize", &lua_blob_resize},
	{"free", &lua_blob_freeblob},
	{NULL, NULL}
};


LUA_MODLOADER_F luaopen_blob(lua_State *L)
{	//STACK: modname ?

	luaL_newmetatable(L, "luablob_mt");			//STACK: modname ? luablob_mt
	luaL_setfuncs(L, luablob_mt_funcs, 0);
	lua_pushliteral(L, "__index");				//STACK: modname ?  luablob_mt '__index'
	lua_createtable(L, 0, 5);					//STACK: modname ? luablob_mt '__index' {~0}
	luaL_setfuncs(L, luablob_mt___index_funcs, 0);
	lua_settable(L, -3);						//STACK: modname ? luablob_mt
	lua_pop(L, 1);								//STACK: modname ?

	lua_pushcfunction(L, &lua_blob_newblob);	//STACK: modname ? newblob

	return 1;									//RETURN: newblob
}