#define BLOBHASH_LIB
#include <lauxlib.h>
#include <string.h>

#include <luablob.h>

#include "sha256.h"

#ifdef MSVC_VER
	#define LUA_CFUNCTION_F int __cdecl

	#ifdef __cplusplus
		#define LUA_MODLOADER_F extern "C" __declspec(dllexport) int __cdecl
	#else
		#define LUA_MODLOADER_F __declspec(dllexport) int __cdecl
	#endif
	#define LUA_BUILD_AS_DLL
#else
	#define LUA_CFUNCTION_F int

	#ifdef __cplusplus
		#define LUA_MODLOADER_F extern "C" int
	#else
		#define LUA_MODLOADER_F int
	#endif
#endif


LUA_CFUNCTION_F lua_hash(lua_State *L)
{	//STACK: hashtype blob start? length?
	const char *hashtype;
	GenericMemoryBlob *src;
	size_t start;
	size_t length;
	GenericMemoryBlob dest;
	lua_Alloc alloc;
	void *allocud;

	alloc = lua_getallocf(L, &allocud);

	hashtype = luaL_checkstring(L, 1);
	src = luablob_checkgmb(L, 2);
	if (lua_gettop(L) > 2)
	{
		start = luaL_checkunsigned(L, 3);
	}
	else
	{
		start = 0;
	}
	
	if (lua_gettop(L) > 3)
	{
		length = luaL_checkunsigned(L, 4);
	}
	else
	{
		length = (src->usedsize - start);
	}

	if ((start + length) > src->usedsize)
	{
		luaL_error(L, "invalid arguments; start index and length are out of range");
	}

	if (strcmp(hashtype, "sha256") == 0)
	{
		luablob_newgmb(L, &dest, 32, "tight");
		sha256_hash(ptradd(src->data, start), length, (uint32_t *)dest.data);
		dest.usedsize = 32;
	}
	else
	{
		luaL_error(L, "invalid argument; hash mode '%s' is not supported; valid values are 'sha256'", hashtype);
	}

	luablob_pushgmb(L, dest);		//STACK: hashtype blob start? length? dest

	return 1;						//RETURN: dest
}

LUA_MODLOADER_F luaopen_hash(lua_State *L)
{	//STACK: modname ?

	lua_pushcfunction(L, &lua_hash);	//STACK: modname ? hashfunc

	return 1;							//RETURN: hashfunc
}