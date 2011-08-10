#ifndef LUABLOB_H
#define LUABLOB_H

#ifdef MSVC_VER
	#ifdef LUABLOB_LIB
		#define LUABLOB_DECLSPEC __declspec(dllexport)
	#else
		#define LUABLOB_DECLSPEC __declspec(dllimport)
	#endif

	#ifdef __cplusplus
		#define LUABLOB_API(rt) extern "C" LUABLOB_DECLSPEC rt __cdecl
		#define LUA_MODLOADER_F extern "C" LUABLOB_DECLSPEC int __cdecl
	#else
		#define LUABLOB_API(rt) LUABLOB_DECLSPEC rt __cdecl
	#endif
	#define LUA_BUILD_AS_DLL
#else
	#ifdef __cplusplus
		#ifdef LUABLOB_LIB
			#define LUABLOB_API(rt) extern "C" rt
		#else
			#define LUABLOB_API(rt) rt
		#endif
	#else
		#define LUABLOB_API(rt) rt
	#endif
#endif
#define LUA_MODLOADER_F LUABLOB_API(int)

#include <lua.h>

struct GenericMemoryBlob_s;
typedef struct GenericMemoryBlob_s GenericMemoryBlob;

struct GenericMemoryBlob_s
{
	size_t (*realloc)(GenericMemoryBlob *blob, size_t nsize);
	void (*free)(GenericMemoryBlob *blob);

	size_t allocsize;
	size_t usedsize;

	void *userdata;
	void* data;
};

LUABLOB_API(void) luablob_newgmb(lua_State *L, GenericMemoryBlob *gmb, size_t initialsize, const char *allocmode);
LUABLOB_API(void) luablob_pushgmb(lua_State *L, GenericMemoryBlob blob);
LUABLOB_API(int) luablob_isgmb(lua_State *L, int index);
LUABLOB_API(GenericMemoryBlob *) luablob_togmb(lua_State *L, int index);
LUABLOB_API(GenericMemoryBlob *) luablob_checkgmb(lua_State *L, int index);

LUABLOB_API(int) gmb_resize(GenericMemoryBlob *blob, size_t nsize, int trim);
LUABLOB_API(int) gmb_realloc(GenericMemoryBlob *blob, size_t nsize);
LUABLOB_API(void) gmb_free(GenericMemoryBlob *blob);

LUA_MODLOADER_F luaopen_blob(lua_State *L);

#endif