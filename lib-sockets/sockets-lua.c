#include <luablob.h>	//also includes lua.h just how we want it

#ifdef MSVC_VER
# define LUA_CFUNCTION_F int __cdecl
# ifdef __cplusplus
#  define LUA_MODLOADER_F extern "C" __declspec(dllexport) int __cdecl
# else
#  define LUA_MODLOADER_F __declspec(dllexport) int __cdecl
# endif
#else
# define LUA_CFUNCTION_F int
# ifdef __cplusplus
#  define LUA_MODLOADER_F extern "C" int
# else
#  define LUA_MODLOADER_F int
# endif
#endif

#include <lauxlib.h>
#include <math.h>

//This avoids the complaints of some compliers when performing pointer addition
#define ptradd(p, o) ((void *)(((char *)(p)) + (o)))

#ifdef _WIN32
# include <WinSock2.h>	//also includes windows.h just how we want it
# include <WS2tcpip.h>

//definitions to match cross platform api
# define close(s) closesocket(s)
# define sockerr WSAGetLastError()

void luaerrorec(lua_State *L, int ec)
{
	LPTSTR msg;
	lua_checkstack(L, 1);
	FormatMessage((FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM), NULL, ec, 0, (LPTSTR)(&msg), 0, NULL);
	lua_pushfstring(L, "socket error: (%d) %s", ec, msg);
	LocalFree(msg);
	lua_error(L);
}
#else
# include <errno.h>
# include <sys/types.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <netdb.h>
# include <unistd.h>

//definitions to match cross platform api
# define sockerr errno
# define INVALID_SOCKET (-1)
void luaerrorec(lua_State *L, int ec)
{
	luaL_error(L, "socket error: (%d) %s", ec, strerror(ec));
}
typedef int SOCKET;

typedef unsigned long u_long;		//NOTICE: You may need to redefine this value to match the return types of htonl and ntohl. This value *must* be unsigned.
typedef unsigned short u_short;		//NOTICE: You may need to redefine this value to match the return types of htons and ntohs. This value *must* be unsigned.
#endif

typedef signed long s_long;			//NOTICE: You may need to redefine this value to be the *signed* equivalent of u_long. This *must* have the same size as u_long.
typedef signed short s_short;		//NOTICE: You may need to redefine this value to be the *signed* equivalent of u_short. This *must* have the same size as u_short.

#define DEFAULTBACKLOG 20
struct lua_sockaddr_info
{
	struct sockaddr_storage *addr;
	int parentref;
};

LUA_CFUNCTION_F luasockets_sockaddr_mt___index(lua_State *L)
{	//STACK: sockaddr k
	struct lua_sockaddr_info *sai;
	const char *k;
	char p[INET6_ADDRSTRLEN + 1];

	sai = (struct lua_sockaddr_info *)lua_touserdata(L, 1);

	if (lua_type(L, 2) == LUA_TSTRING)
	{
		k = lua_tostring(L, 2);

		if (strcmp(k, "address") == 0)
		{
			switch(sai->addr->ss_family)
			{
				case AF_INET:
					memset(&p, 0, sizeof(p));
					inet_ntop(AF_INET, &(((struct sockaddr_in *)sai->addr)->sin_addr), (char *)(&p), (sizeof(p) - sizeof(char)));
					lua_pushstring(L, (const char *)(&p));		//STACK: sockaddr k address
					break;
				case AF_INET6:
					memset(&p, 0, sizeof(p));
					inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sai->addr)->sin6_addr), (char *)(&p), (sizeof(p) - sizeof(char)));
					lua_pushstring(L, (const char *)(&p));		//STACK: sockaddr k address
					break;
				default:
					lua_pushnil(L);		//STACK: sockaddr k nil
					break;
			}
		}
		else if (strcmp(k, "port") == 0)
		{
			switch(sai->addr->ss_family)
			{
				case AF_INET:
					lua_pushunsigned(L, ntohs(((struct sockaddr_in *)sai->addr)->sin_port));	//STACK: sockaddr k port
					break;
				case AF_INET6:
					lua_pushunsigned(L, ntohs(((struct sockaddr_in6 *)sai->addr)->sin6_port));	//STACK: sockaddr k port
					break;
				default:
					lua_pushnil(L);		//STACK: sockaddr k nil
					break;
			}
		}
		else if (strcmp(k, "family") == 0)
		{
			switch(sai->addr->ss_family)
			{
				case AF_INET:
					lua_pushliteral(L, "ipv4");
					break;
				case AF_INET6:
					lua_pushliteral(L, "ipv6");
					break;
				default:
					lua_pushnil(L);		//STACK: sockaddr k nil
					break;
			}
		}
		else
		{
			lua_pushnil(L);				//STACK: sockaddr k nil
		}
	}
	else
	{
		lua_pushnil(L);					//STACK: sockaddr k nil
	}

	return 1;							//RETURN: result
}

LUA_CFUNCTION_F luasockets_sockaddr_mt___newindex(lua_State *L)
{	//STACK: sockaddr k v
	struct lua_sockaddr_info *sai;
	const char *k;

	sai = (struct lua_sockaddr_info *)lua_touserdata(L, 1);

	if (lua_type(L, 2) == LUA_TSTRING)
	{
		k = lua_tostring(L, 2);

		if (strcmp(k, "address") == 0)
		{
			luaL_error(L, "cannot set readonly field 'address'");
		}
		else if (strcmp(k, "port") == 0)
		{
			switch(sai->addr->ss_family)
			{
				case AF_INET:
					((struct sockaddr_in *)(sai->addr))->sin_port = htons(luaL_checkunsigned(L, 3));
					break;
				case AF_INET6:
					((struct sockaddr_in6 *)(sai->addr))->sin6_port = htons(luaL_checkunsigned(L, 3));
					break;
				default:
					luaL_error(L, "the specified key does not exist");
			}
		}
		else if (strcmp(k, "family") == 0)
		{
			luaL_error(L, "cannot set readonly field 'family'");
		}
		else
		{
			luaL_error(L, "the specified key does not exist");
		}
	}
	else
	{
		luaL_error(L, "the specified key does not exist");
	}

	return 0;
}

LUA_CFUNCTION_F luasockets_sockaddr_mt___tostring(lua_State *L)
{	//STACK: sockaddr
	struct lua_sockaddr_info *sai;
	sai = (struct lua_sockaddr_info *)lua_touserdata(L, 1);

	switch(sai->addr->ss_family)
	{
		case AF_INET:
		case AF_INET6:
			lua_pushliteral(L, "[");					//STACK: sockaddr '['
			lua_pushliteral(L, "address");				//STACK: sockaddr '[' 'address'
			lua_gettable(L, 1);							//STACK: sockaddr '[' address
			lua_pushliteral(L, "]:");					//STACK: sockaddr '[' address ']:'
			lua_pushliteral(L, "port");					//STACK: sockaddr '[' address ']:' 'port'
			lua_gettable(L, 1);							//STACK: sockaddr '[' address ']:' port
			lua_concat(L, 4);							//STACK: sockaddr '[address]:port'
			break;
		default:
			lua_pushliteral(L, "{raw data: '");			//STACK: sockaddr '{raw data: ''
			lua_pushlstring(L, (const char *)sai->addr, sizeof(sai->addr));	//STACK: sockaddr '{raw data: '' data
			lua_pushliteral(L, "'}");					//STACK: sockaddr '{raw data: '' data ''}'
			lua_concat(L, 3);							//STACK: sockaddr '{raw data: 'data'}'
			break;
	}

	return 1;
}
LUA_CFUNCTION_F luasockets_sockaddr_mt___gc(lua_State *L)
{	//STACK: sockaddr
	struct lua_sockaddr_info *sai;
	void *allocud = NULL;

	sai = (struct lua_sockaddr_info *)lua_touserdata(L, 1);

	if (sai->parentref == LUA_NOREF)
	{
		(lua_getallocf(L, &allocud))(allocud, sai->addr, sizeof(struct sockaddr), 0);
	}
	else
	{
		lua_pushliteral(L, "luasockets_sockaddr_parentref");	//STACK: sockaddr 'luasockets_sockaddr_parentref'
		lua_gettable(L, LUA_REGISTRYINDEX);						//STACK: sockaddr luasockets_sockaddr_parentref
		luaL_unref(L, -1, sai->parentref);
	}

	return 0;
}

LUA_CFUNCTION_F luasockets_addrinfo_mt___gc(lua_State *L)
{	//STACK: addrinfo ?
	freeaddrinfo(*((struct addrinfo **)lua_touserdata(L, 1)));

	return 0;
}

LUA_CFUNCTION_F luasockets_addrinfo_mt___index(lua_State *L)
{	//STACK: addrinfo k
	struct addrinfo *ai;
	const char *k;
	struct lua_sockaddr_info *sai;

	ai = *((struct addrinfo **)lua_touserdata(L, 1));
	if (lua_type(L, 2) == LUA_TSTRING)
	{
		k = lua_tostring(L, 2);
		if (strcmp(k, "endpoint") == 0)
		{
			sai = ((struct lua_sockaddr_info *)lua_newuserdata(L, sizeof(struct lua_sockaddr_info)));	//STACK: addrinfo k sockaddr
			sai->addr = (struct sockaddr_storage *)(ai->ai_addr);

			lua_pushliteral(L, "luasockets_sockaddr_parentref");	//STACK: addrinfo k sockaddr 'luasockets_sockaddr_parentref'
			lua_gettable(L, LUA_REGISTRYINDEX);						//STACK: addrinfo k sockaddr luasockets_sockaddr_parentref
			lua_pushvalue(L, 1);									//STACK: addrinfo k sockaddr luasockets_sockaddr_parentref addrinfo
			sai->parentref = luaL_ref(L, -2);						//STACK: addrinfo k sockaddr luasockets_sockaddr_parentref
			lua_pop(L, 1);											//STACK: addrinfo k sockaddr
			lua_pushliteral(L, "luasockets_sockaddr_mt");			//STACK: addrinfo k sockaddr 'luasockets_sockaddr_mt'
			lua_gettable(L, LUA_REGISTRYINDEX);						//STACK: addrinfo k sockaddr luasockets_sockaddr_mt
			lua_setmetatable(L, -2);								//STACK: addrinfo k sockaddr
		}
		else if (strcmp(k, "protocol") == 0)
		{
			if (ai->ai_protocol == 0)
			{
				lua_pushliteral(L, "default");									//STACK: addrinfo k 'default'
			}
			else
			{
				lua_pushstring(L, getprotobynumber(ai->ai_protocol)->p_name);	//STACK: addrinfo k protocol
			}
		}
		else if (strcmp(k, "family") == 0)
		{
			switch(ai->ai_family)								//STACK: addrinfo k family
			{
				case AF_INET:
					lua_pushliteral(L, "ipv4");
					break;
				case AF_INET6:
					lua_pushliteral(L, "ipv6");
					break;
				default:
					lua_pushnumber(L, ai->ai_family);
					break;
			}
		}
		else if (strcmp(k, "sockettype") == 0)					//STACK: addrinfo k sockettype
		{
			switch(ai->ai_socktype)
			{
				case SOCK_DGRAM:
					lua_pushliteral(L, "dgram");
					break;
				case SOCK_RAW:
					lua_pushliteral(L, "raw");
					break;
				case SOCK_STREAM:
					lua_pushliteral(L, "stream");
					break;
				case 0:
					lua_pushliteral(L, "any");
					break;
				default:
					lua_pushnumber(L, ai->ai_socktype);
					break;
			}
		}
		else
		{
			lua_pushnil(L);										//STACK: addrinfo k nil
		}
	}
	else
	{
		lua_pushnil(L);											//STACK: addrinfo k nil
	}

	return 1;													//RETURN: result
}

LUA_CFUNCTION_F luasockets_addrinfo_mt___newindex(lua_State *L)
{	//STACK: addrinfo k v
	struct addrinfo *ai;
	const char *k;
	const char *v;
	struct protoent *vproto;

	ai = (struct addrinfo *)lua_touserdata(L, 1);
	
	if (lua_type(L, 2) == LUA_TSTRING)
	{
		k = lua_tostring(L, 2);

		if (strcmp(k, "endpoint") == 0)
		{
			luaL_error(L, "cannot set readonly field 'endpoint'");
		}
		else if (strcmp(k, "protocol") == 0)
		{
			if (lua_isnil(L, 3))
			{
				ai->ai_protocol = 0;
			}
			else
			{
				v = luaL_checkstring(L, 3);
				if ((strcmp(v, "any") == 0) || (strcmp(v, "default") == 0))
				{
					ai->ai_protocol = 0;
				}
				else
				{
					vproto = getprotobyname(luaL_checkstring(L, 3));
					if (vproto == NULL)
					{
						luaerrorec(L, sockerr);
					}
					ai->ai_protocol = vproto->p_proto;
				}
			}		
		}
		else if (strcmp(k, "family") == 0)
		{
			luaL_error(L, "cannot set readonly field 'family'");
		}
		else if (strcmp(k, "sockettype") == 0)
		{
			if (lua_isnil(L, 3))
			{
				ai->ai_socktype = 0;
			}
			else
			{
				v = luaL_checkstring(L, 3);
				if (strcmp(v, "stream") == 0)
				{
					ai->ai_socktype = SOCK_STREAM;
				}
				else if (strcmp(v, "dgram") == 0)
				{
					ai->ai_socktype = SOCK_DGRAM;
				}
				else if (strcmp(v, "raw") == 0)
				{
					ai->ai_socktype = SOCK_RAW;
				}
				else if ((strcmp(v, "any") == 0) || (strcmp(v, "default") == 0))
				{
					ai->ai_socktype = 0;
				}
				else
				{
					luaL_error(L, "invalid socket type; expected 'stream', 'dgram', 'raw', or 'default'/'any'");
				}
			}
		}
		else
		{
			luaL_error(L, "the specified key does not exist");
		}
	}
	else
	{
		luaL_error(L, "the specified key does not exist");
	}

	return 0;
}

LUA_CFUNCTION_F luasockets_addrinfo_mt___tostring(lua_State *L)
{	//STACK: addrinfo
	lua_pushliteral(L, "endpoint");		//STACK: addrinfo 'endpoint'
	lua_gettable(L, 1);					//STACK: addrinfo endpoint
	luaL_tolstring(L, -1, NULL);
	lua_pushliteral(L, " (");			//STACK: addrinfo address ' ('
	lua_pushliteral(L, "sockettype");	//STACK: addrinfo address ' (' 'sockettype'
	lua_gettable(L, 1);					//STACK: addrinfo address ' (' sockettype
	lua_pushliteral(L, ": ");			//STACK: addrinfo address ' (' sockettype ': '
	lua_pushliteral(L, "protocol");		//STACK: addrinfo address ' (' sockettype ': ' 'protocol'
	lua_gettable(L, 1);					//STACK: addrinfo address ' (' sockettype ': ' protocol
	lua_pushliteral(L, ")");			//STACK: addrinfo address ' (' sockettype ': ' protocol ')'
	lua_concat(L, 6);					//STACK: addrinfo 'address (sockettype: protocol)'

	return 1;							//RETURN: 'address (sockettype: protocol)'
}

LUA_CFUNCTION_F luasockets_socket_mt___gc(lua_State *L)
{
	//ignore any errors, just in case the socket has already been closed or something
	close(*((SOCKET *)lua_touserdata(L, 1)));

	return 0;
}

LUA_CFUNCTION_F lua_sockets_getaddrinfo(lua_State *L)
{	//STACK: node service protocol? family? socktype? canonical? ?
	const char *node;
	const char *service;
	const char *param;
	struct addrinfo hints;
	struct addrinfo *res;
	int i;
	struct addrinfo **resud;
	struct protoent *protocol;

	memset(&hints, 0, sizeof(struct addrinfo));
	if (lua_isnil(L, 1))
	{
		node = NULL;
		hints.ai_flags = AI_PASSIVE;
	}
	else
	{
		node = luaL_checkstring(L, 1);
	}

	switch(lua_type(L, 2))
	{
		case LUA_TNUMBER:
		case LUA_TSTRING:
			service = lua_tostring(L, 2);
			break;
		case LUA_TNIL:
			service = NULL;
			break;
		default:
			luaL_error(L, "expected a service string containing an IANA recognized port or a valid port number, an integer representing a valid port number, or nil representing the default port");
	}

	if (lua_gettop(L) > 2 && !lua_isnil(L, 3))
	{
		param = lua_tostring(L, 3);
		if ((strcmp(param, "any") != 0) && (strcmp(param, "default") != 0))
		{
			protocol = getprotobyname(param);
			if (protocol == NULL)
			{
				luaerrorec(L, sockerr);
			}
			hints.ai_protocol = protocol->p_proto;
		}
	}

	if (lua_gettop(L) > 3 && !lua_isnil(L, 4))	//conveniently, AF_UNSPEC is 0
	{
		param = luaL_checkstring(L, 4);
		if (strcmp(param, "ipv4") == 0)
		{
			hints.ai_family = AF_INET;
		}
		else if (strcmp(param, "ipv6") == 0)
		{
			hints.ai_family = AF_INET6;
		}
		else if ((strcmp(param, "any") != 0) && (strcmp(param, "default") != 0))
		{
			luaL_error(L, "invalid address family; expected 'ipv4', 'ipv6', or 'default'/'any'");
		}
	}

	if (lua_gettop(L) > 4 && !lua_isnil(L, 5))
	{
		param = luaL_checkstring(L, 5);
		if (strcmp(param, "stream") == 0)
		{
			hints.ai_socktype = SOCK_STREAM;
		}
		else if (strcmp(param, "dgram") == 0)
		{
			hints.ai_socktype = SOCK_DGRAM;
		}
		else if (strcmp(param, "raw") == 0)
		{
			hints.ai_socktype = SOCK_RAW;
		}
		else if ((strcmp(param, "any") != 0) && (strcmp(param, "default") != 0))
		{
			luaL_error(L, "invalid socket type; expected 'stream', 'dgram', 'raw', or 'default'/'any'");
		}
	}

	if (lua_gettop(L) > 5)
	{
		luaL_checktype(L, 6, LUA_TBOOLEAN);
		if (lua_toboolean(L, 6))
		{
			hints.ai_flags |= AI_CANONNAME;
		}
	}

	if (getaddrinfo(node, service, &hints, &res) != 0)
	{
		luaL_error(L, "socket error: (gai %d) %s", sockerr, gai_strerror(sockerr));
	}

	lua_newtable(L);									//STACK: node service protocol? family? socktype? canonical? ? {~0}
	for (i = 1; res != NULL; ++i)
	{
		if (res->ai_canonname != NULL)
		{
			lua_pushliteral(L, "canonname");			//STACK: node service protocol? family? socktype? canonical? ? {~0} 'canonname'
			lua_pushstring(L, res->ai_canonname);		//STACK: node service protocol? family? socktype? canonical? ? {~0} 'canonname' canonname
			lua_settable(L, -3);						//STACK: node service protocol? family? socktype? canonical? ? {~0}
		}

		lua_pushinteger(L, i);							//STACK: node service protocol? family? socktype? canonical? ? {~0} i

		resud = (struct addrinfo **)lua_newuserdata(L, sizeof(struct addrinfo *));	//STACK: node service protocol? family? socktype? canonical? ? {~0} i addrinfo
		*resud = res;
		lua_pushliteral(L, "luasockets_addrinfo_mt");		//STACK: node service protocol? family? socktype? canonical? ? {~0} i addrinfo 'luasockets_addrinfo_mt'
		lua_gettable(L, LUA_REGISTRYINDEX);					//STACK: node service protocol? family? socktype? canonical? ? {~0} i addrinfo luasockets_addrinfo_mt
		lua_setmetatable(L, -2);							//STACK: node service protocol? family? socktype? canonical? ? {~0} i addrinfo
		lua_settable(L, -3);								//STACK: node service protocol? family? socktype? canonical? ? {~0}

		res = res->ai_next;
		(*resud)->ai_next = NULL;
	}

	return 1;	//RETURN: {~0}
}

LUA_CFUNCTION_F lua_sockets_newsockaddr(lua_State *L)
{	//STACK: family address port
	const char *family;
	int ifamily;
	const char *address;
	unsigned short port;
	int result;
	struct lua_sockaddr_info* sai;
	void *allocud = NULL;

	family = luaL_checkstring(L, 1);
	address = luaL_checkstring(L, 2);
	port = luaL_checkunsigned(L, 3);

	if (strcmp(family, "ipv4") == 0)
	{
		ifamily = AF_INET;
	}
	else if (strcmp(family, "ipv6") == 0)
	{
		ifamily = AF_INET6;
	}
	else
	{
		luaL_error(L, "the specified address family is not supported or does not exist");
	}

	sai = (struct lua_sockaddr_info *)lua_newuserdata(L, sizeof(struct lua_sockaddr_info));			//STACK: family address port sockaddr
	sai->parentref = LUA_NOREF;
	sai->addr = (struct sockaddr_storage *)((lua_getallocf(L, &allocud))(allocud, NULL, 0, sizeof(struct sockaddr_storage)));

	switch(ifamily)
	{
		case AF_INET:
			((struct sockaddr_in *)sai->addr)->sin_family = AF_INET;
			((struct sockaddr_in *)sai->addr)->sin_port = htons(port);
			result = inet_pton(AF_INET, address, &((struct sockaddr_in *)sai->addr)->sin_addr);
			break;
		case AF_INET6:
			((struct sockaddr_in6 *)sai->addr)->sin6_family = AF_INET6;
			((struct sockaddr_in6 *)sai->addr)->sin6_port = htons(port);
			result = inet_pton(AF_INET6, address, &((struct sockaddr_in6 *)sai->addr)->sin6_addr);
			break;
	}

	switch(result)
	{
		case 1:
			break;
		case 0:
			luaL_error(L, "address is not properly formatted");
			break;
		default:
			luaerrorec(L, sockerr);
			break;
	}

	lua_pushliteral(L, "luasockets_sockaddr_mt");				//STACK: family address port sockaddr 'luasockets_sockaddr_mt'
	lua_gettable(L, LUA_REGISTRYINDEX);							//STACK: family address port sockaddr luasockets_sockaddr_mt
	lua_setmetatable(L, -2);									//STACK: family address port sockaddr

	return 1;													//RETURN: sockaddr
}

LUA_CFUNCTION_F lua_sockets_newsocket(lua_State *L)
{	//STACK: addrinfo ?
	struct addrinfo *ep;
	SOCKET sock;
	SOCKET *resud;

	ep = *((struct addrinfo **)luaL_checkudata(L, 1, "luasockets_addrinfo_mt"));
	
	sock = socket(ep->ai_family, ep->ai_socktype, ep->ai_protocol);
	if (sock == INVALID_SOCKET)
	{
		luaerrorec(L, sockerr);
	}

	resud = (SOCKET *)lua_newuserdata(L, sizeof(SOCKET));	//STACK: addrinfo ? sockud
	*resud = sock;
	lua_pushliteral(L, "luasockets_socket_mt");				//STACK: addrinfo ? sockud 'luasockets_socket_mt'
	lua_gettable(L, LUA_REGISTRYINDEX);						//STACK: addrinfo ? sockud luasockets_socket_mt
	lua_setmetatable(L, -2);								//STACK: addrinfo ? sockud

	return 1;												//RETURN: sockud
}

LUA_CFUNCTION_F lua_sockets_bind(lua_State *L)
{	//STACK: sockud addrinfo ?
	SOCKET sock;
	struct addrinfo *ep;

	sock = *((SOCKET *)luaL_checkudata(L, 1, "luasockets_socket_mt"));
	ep = *((struct addrinfo **)luaL_checkudata(L, 2, "luasockets_addrinfo_mt"));

	if (bind(sock, ep->ai_addr, ep->ai_addrlen) != 0)
	{
		luaerrorec(L, sockerr);
	}

	return 0;
}

LUA_CFUNCTION_F lua_sockets_connect(lua_State *L)
{	//STACK: sockud addrinfo ?
	SOCKET sock;
	struct addrinfo *ep;

	sock = *((SOCKET *)luaL_checkudata(L, 1, "luasockets_socket_mt"));
	ep = *((struct addrinfo **)luaL_checkudata(L, 2, "luasockets_addrinfo_mt"));

	if (connect(sock, ep->ai_addr, ep->ai_addrlen) != 0)
	{
		luaerrorec(L, sockerr);
	}

	return 0;
}

LUA_CFUNCTION_F lua_sockets_listen(lua_State *L)
{	//STACK: sockud backlog? ?
	SOCKET sock;
	int backlog = DEFAULTBACKLOG;

	sock = *((SOCKET *)luaL_checkudata(L, 1, "luasockets_socket_mt"));
	if (lua_gettop(L) > 1)
	{
		backlog = luaL_checkint(L, 2);
	}

	if (listen(sock, backlog) != 0)
	{
		luaerrorec(L, sockerr);
	}

	return 0;
}

LUA_CFUNCTION_F lua_sockets_accept(lua_State *L)
{	//STACK: sockud ?
	SOCKET sock;
	SOCKET remote;
	SOCKET *remoteud;
	struct lua_sockaddr_info *ep;
	void *allocud = NULL;
	socklen_t addrsz;
	ep = (struct lua_sockaddr_info *)lua_newuserdata(L, sizeof(struct lua_sockaddr_info));	//STACK: sockud ? ep
	ep->addr = (struct sockaddr_storage *)((lua_getallocf(L, &allocud))(allocud, NULL, 0, sizeof(struct sockaddr_storage)));
	ep->parentref = LUA_NOREF;

	sock = *((SOCKET *)luaL_checkudata(L, 1, "luasockets_socket_mt"));

	addrsz = sizeof(*(ep->addr));
	remote = accept(sock, (struct sockaddr *)(ep->addr), &addrsz);	//INTELLISENSE LIES; "struct sockaddr_storage *" is compatible with "struct sockaddr *"
	if (remote == INVALID_SOCKET)
	{
		luaerrorec(L, sockerr);
	}

	remoteud = (SOCKET *)lua_newuserdata(L, sizeof(SOCKET));	//STACK: sockud ? ep remoteud
	*remoteud = remote;
	lua_pushliteral(L, "luasockets_socket_mt");					//STACK: sockud ? ep remoteud 'luasockets_socket_mt'
	lua_gettable(L, LUA_REGISTRYINDEX);							//STACK: sockud ? ep remoteud luasockets_socket_mt
	lua_setmetatable(L, -2);									//STACK: sockud ? ep remoteud

	lua_insert(L, -2);											//STACK: sockud ? remoteud ep

	return 2;													//RETURN: remoteud ep
}

void lua_sockets_send_gmb(lua_State *L, SOCKET sock, struct sockaddr_storage *dest, int flags, GenericMemoryBlob *msggmb, int start, int count)
{
	int sent;
	int totalsent = 0;
	const char *datastart;

	if (count < 0)
	{
		count = (msggmb->usedsize - start);
	}
	if (start < 0 || ((unsigned int)(count + start)) > msggmb->usedsize)
	{
		luaL_error(L, "access to luablob was out of bounds");
	}

	if (count > 0)
	{
		datastart = (const char *)ptradd(msggmb->data, start);
		do
		{
			if (dest == NULL)
			{
				sent = send(sock, (datastart + totalsent), (count - totalsent), flags);
			}
			else
			{
				sent = sendto(sock, (datastart + totalsent), (count - totalsent), flags, (const struct sockaddr *)dest, sizeof(struct sockaddr_storage));
			}
			if (sent <= 0)
			{
				luaerrorec(L, sockerr);
			}
			totalsent += sent;
		} while(totalsent < count);
	}
}
void lua_sockets_send_table(lua_State *L, SOCKET sock, struct sockaddr_storage *dest, int flags)
{	//STACK: ? tbl
	int i;
	int udstart = 0;
	int udcount = -1;
	int count = luaL_len(L, -1);

	for (i = 1; i <= count; ++i)
	{
		lua_pushinteger(L, i);								//STACK: ? tbl i
		lua_gettable(L, -2);								//STACK: ? tbl val
		switch(lua_type(L, -1))
		{
			case LUA_TUSERDATA:
				lua_sockets_send_gmb(L, sock, dest, flags, luablob_checkgmb(L, -1), 0, -1);
				lua_pop(L, 1);
				break;
			case LUA_TTABLE:
				lua_pushliteral(L, "value");				//STACK: ? tbl val 'value'
				lua_gettable(L, -2);						//STACK: ? tbl val val.value
				switch (lua_type(L, -1))
				{
					case LUA_TTABLE:
						lua_sockets_send_table(L, sock, dest, flags);
						break;
					case LUA_TUSERDATA:
						lua_pushliteral(L, "start");		//STACK: ? tbl val val.value 'start'
						lua_gettable(L, -3);				//STACK: ? tbl val val.value val.start
						switch(lua_type(L, -1))
						{
							case LUA_TNUMBER:
								udstart = lua_tointeger(L, -1);
								if (udstart < 0)
								{
									luaL_error(L, "invalid count; expected a positive integer size");
								}
								break;
							case LUA_TNIL:
								break;
							default:
								luaL_error(L, "invalid start offset; expected default value (nil) or a non-negative integer offset");
						}
						lua_pop(L, 1);						//STACK: ? tbl val val.value

						lua_pushliteral(L, "count");		//STACK: ? tbl val val.value 'count'
						lua_gettable(L, -3);				//STACK: ? tbl val val.value val.count
						switch(lua_type(L, -1))
						{
							case LUA_TNUMBER:
								udcount = lua_tointeger(L, -1);
								if (udcount <= 0)
								{
									luaL_error(L, "invalid count; expected a positive integer size");
								}
								break;
							case LUA_TNIL:
								break;
							default:
								luaL_error(L, "invalid count; expected default value (nil) or a positive integer size");
						}
						lua_pop(L, 1);						//STACK: ? tbl val val.value
						
						lua_sockets_send_gmb(L, sock, dest, flags, luablob_checkgmb(L, -1), udstart, udcount);
						lua_pop(L, 3);						//STACK: ?
						break;
					default:
						luaL_error(L, "invalid message component value; expected a table or luablob");
				}
				break;
			default:
				luaL_error(L, "invalid message component; expected a table or luablob");
		}
	}
}

LUA_CFUNCTION_F lua_sockets_send(lua_State *L)
{   //STACK: sock msg oob? ?
	SOCKET sock;
	int flags = 0;

	sock = *((SOCKET *)luaL_checkudata(L, 1, "luasockets_socket_mt"));

	if (lua_gettop(L) > 2)
	{
		luaL_checktype(L, 3, LUA_TBOOLEAN);
		if (lua_toboolean(L, 3))
		{
			flags |= MSG_OOB;
		}
	}

	luaL_checkany(L, 2);
	switch(lua_type(L, 2))
	{
		case LUA_TTABLE:
			lua_pushvalue(L, 2);	//STACK: sock msg oob? ? msg

			lua_sockets_send_table(L, sock, NULL, flags);
			
			break;
		case LUA_TUSERDATA:
			lua_sockets_send_gmb(L, sock, NULL, flags, luablob_checkgmb(L, 2), 0, -1);
			break;
		default:
			luaL_error(L, "invalid message; expected a table or luablob");
	}

	return 0;
}
LUA_CFUNCTION_F lua_sockets_sendto(lua_State *L)
{	//STACK: sock msg dest ?
	SOCKET sock;
	struct lua_sockaddr_info *dest;

	sock = *((SOCKET *)luaL_checkudata(L, 1, "luasockets_socket_mt"));
	dest = (struct lua_sockaddr_info *)luaL_checkudata(L, 3, "luasockets_sockaddr_mt");

	luaL_checkany(L, 2);
	switch(lua_type(L, 2))
	{
		case LUA_TTABLE:
			lua_pushvalue(L, 2);	//STACK: sock msg oob? ? msg
			lua_sockets_send_table(L, sock, dest->addr, 0);
			break;
		case LUA_TUSERDATA:
			lua_sockets_send_gmb(L, sock, dest->addr, 0, luablob_checkgmb(L, 2), 0, -1);
			break;
		default:
			luaL_error(L, "invalid message; expected a table or luablob");
	}

	return 0;
}

int lua_sockets_recv_gmbdata(lua_State *L, SOCKET sock, struct sockaddr_storage *src, int flags, GenericMemoryBlob *gmbresult, int start, int count, int complete)
{	//STACK: ?
	char *datastart;
	int read;
	int totalread = 0;
	size_t gmbsz;
	int fromlen = sizeof(struct sockaddr_storage);

	if (start < 0 || ((unsigned int)start) > gmbresult->usedsize)
	{
		luaL_error(L, "access to luablob was out of bounds");
	}
	if (count == -1)
	{
		count = (gmbresult->usedsize - start);
	}
	else
	{
		if (count <= 0)
		{
			luaL_error(L, "count to read must be greater than 0");
		}
		gmbsz = (size_t)(start + count);

		if (gmbsz > gmbresult->usedsize)
		{
			gmb_resize(gmbresult, gmbsz, 0);
		}
	}
	
	datastart = (char *)ptradd(gmbresult->data, start);
	if (complete)
	{
		do
		{
			if (src == NULL)
			{
				read = recv(sock, (datastart + totalread), (count - totalread), flags);
			}
			else
			{
				read = recvfrom(sock, (datastart + totalread), (count - totalread), flags, (struct sockaddr *)src, &fromlen);
			}
			if (read < 0)
			{
				gmb_free(gmbresult);
				luaerrorec(L, sockerr);
			}
			if (read == 0)
			{
				gmb_resize(gmbresult, (size_t)totalread, 1);
				lua_pushboolean(L, 0);								//STACK: ? false
				luablob_pushgmb(L, *gmbresult);						//STACK: ? false gmbresult
				return 2;											//RETURN: false gmbresult
			}
			totalread += read;
		}
		while(totalread < count);
	}
	else
	{
		if (src == NULL)
		{
			read = recv(sock, datastart, count, flags);
		}
		else
		{
			read = recvfrom(sock, datastart, count, flags, (struct sockaddr *)src, &fromlen);
		}
		if (read < 0)
		{
			gmb_free(gmbresult);
			luaerrorec(L, sockerr);
		}
		if (read == 0)
		{
			gmb_free(gmbresult);
			lua_pushboolean(L, 0);								//STACK: ? false
			lua_pushnil(L);										//STACK: ? false nil
			return 2;											//RETURN: false nil
		}
		else if (read < count)
		{
			gmb_resize(gmbresult, (size_t)read, 1);
		}
	}

	lua_pushboolean(L, 1);										//STACK: ? true
	luablob_pushgmb(L, *gmbresult);								//STACK: ? true gmbresult
	return 2;													//RETURN: true gmbresult
}

int lua_sockets_recv_generic(lua_State *L, int recvfrom)
{	//STACK: sock count|blob|tblinfo partial? oob? ?
	SOCKET sock;
	int flags = 0;
	int start = 0;
	int count = -1;
	int rescount;
	int complete = 1;
	GenericMemoryBlob gmbresult;
	struct lua_sockaddr_info *src;
	void *allocud = NULL;

	sock = *((SOCKET *)luaL_checkudata(L, 1, "luasockets_socket_mt"));

	if (lua_gettop(L) > 2)
	{
		luaL_checktype(L, 3, LUA_TBOOLEAN);
		complete = !(lua_toboolean(L, 3));
		
		if (lua_gettop(L) > 3 && !recvfrom)
		{
			luaL_checktype(L, 4, LUA_TBOOLEAN);
			if (lua_toboolean(L, 4))
			{
				flags |= MSG_OOB;
			}
		}
	}

	luaL_checkany(L, 2);

	if (recvfrom)
	{
		src = (struct lua_sockaddr_info *)lua_newuserdata(L, sizeof(struct lua_sockaddr_info));			//STACK: sock count|blob|tblinfo oob? ? src
		src->parentref = LUA_NOREF;
		src->addr = (struct sockaddr_storage *)((lua_getallocf(L, &allocud))(allocud, NULL, 0, sizeof(struct sockaddr_storage)));
	}

	switch(lua_type(L, 2))
	{
		case LUA_TNUMBER:
			count = lua_tointeger(L, 2);
			luablob_newgmb(L, &gmbresult, (size_t)count, "tight");
			if (recvfrom)
			{
				rescount = lua_sockets_recv_gmbdata(L, sock, src->addr, flags, &gmbresult, 0, count, complete);	//STACK: sock count ? src? [rescount]	
			}
			else
			{
				rescount = lua_sockets_recv_gmbdata(L, sock, NULL, flags, &gmbresult, 0, count, complete);	//STACK: sock count oob? ? src? [rescount]	
			}
			break;
		case LUA_TTABLE:
			lua_pushliteral(L, "start");		//STACK: sock tblinfo oob? ? 'start'
			lua_gettable(L, 2);					//STACK: sock tblinfo oob? ? tblinfo.start
			switch(lua_type(L, -1))
			{
				case LUA_TNUMBER:
					start = lua_tointeger(L, -1);
					if (start < 0)
					{
						luaL_error(L, "invalid buffer start offset; expected a non-negative integer offset");
					}
					break;
				case LUA_TNIL:
					break;
				default:
					luaL_error(L, "invalid buffer start offset; expected default value (nil) or a non-negative integer offset");
			}
			lua_pop(L, 1);						//STACK: sock tblinfo oob? ? src?

			lua_pushliteral(L, "count");		//STACK: sock tblinfo oob? ? src? 'count'
			lua_gettable(L, 2);					//STACK: sock tblinfo oob? ? src? tblinfo.count
			switch(lua_type(L, -1))
			{
				case LUA_TNUMBER:
					count = lua_tointeger(L, -1);
					if (count <= 0)
					{
						luaL_error(L, "invalid buffer count; expected a positive integer size");
					}
					break;
				case LUA_TNIL:
					break;
				default:
					luaL_error(L, "invalid buffer count; expected default value (nil) or a positive integer size");
			}
			lua_pop(L, 1);						//STACK: sock tblinfo oob? ? src?

			lua_pushliteral(L, "buffer");		//STACK: sock tblinfo oob? ? src? 'buffer'
			lua_gettable(L, 2);					//STACK: sock tblinfo oob? ? src? tblinfo.buffer
			if (recvfrom)
			{
				rescount = lua_sockets_recv_gmbdata(L, sock, src->addr, flags, luablob_checkgmb(L, -1), start, count, complete);	//STACK: sock count ? src? [rescount]
			}
			else
			{
				rescount = lua_sockets_recv_gmbdata(L, sock, NULL, flags, luablob_checkgmb(L, -1), start, count, complete);			//STACK: sock count oob? ? src? [rescount]
			}
			break;
		case LUA_TUSERDATA:
			if (recvfrom)
			{
				rescount = lua_sockets_recv_gmbdata(L, sock, src->addr, flags, luablob_checkgmb(L, 2), 0, -1, complete);			//STACK: sock count ? src? [rescount]
			}
			else
			{
				rescount = lua_sockets_recv_gmbdata(L, sock, NULL, flags, luablob_checkgmb(L, 2), 0, -1, complete);					//STACK: sock count oob? ? src? [rescount]
			}
			break;
		default:
			luaL_error(L, "invalid receive buffer; expected an integer count, luablob, or table");
	}

	if (recvfrom)
	{
		lua_pushvalue(L, -(++rescount));	//STACK: sock count oob? ? src [rescount] src
	}
	
	return rescount;		//RETURN: [rescount]
}

LUA_CFUNCTION_F lua_sockets_recv(lua_State *L)
{
	return lua_sockets_recv_generic(L, 0 /* false */);
}

LUA_CFUNCTION_F lua_sockets_recvfrom(lua_State *L)
{
	return lua_sockets_recv_generic(L, 1 /* true */);
}

LUA_CFUNCTION_F lua_sockets_close(lua_State *L)
{	//STACK: sock ?
	if (close(*((SOCKET *)luaL_checkudata(L, 1, "luasockets_socket_mt"))) != 0)
	{
		luaerrorec(L, sockerr);
	}
	return 0;
}

LUA_CFUNCTION_F lua_sockets_getpeername(lua_State *L)
{	//STACK: sock ?
	SOCKET sock;
	struct lua_sockaddr_info *peer;
	int peerlen = sizeof(struct sockaddr_storage);
	void *allocud = NULL;

	sock = *((SOCKET *)luaL_checkudata(L, 1, "luasockets_socket_mt"));

	peer = (struct lua_sockaddr_info *)lua_newuserdata(L, sizeof(struct lua_sockaddr_info));			//STACK: sock ? peer
	peer->parentref = LUA_NOREF;
	peer->addr = (struct sockaddr_storage *)((lua_getallocf(L, &allocud))(allocud, NULL, 0, sizeof(struct sockaddr_storage)));

	if (getpeername(sock, (struct sockaddr *)(peer->addr), &peerlen) != 0)
	{
		luaerrorec(L, sockerr);
	}

	return 1;		//RETURN: peer
}

LUA_CFUNCTION_F lua_sockets_gethostname(lua_State *L)
{	//STACK: ?
	char buff[256];

	memset(buff, 0, sizeof(buff));

	if (gethostname(buff, sizeof(buff)) != 0)
	{
		luaerrorec(L, sockerr);
	}

	lua_pushstring(L, buff);		//STACK: ? name
	return 1;						//RETURN: name
}

LUA_CFUNCTION_F lua_sockets_select(lua_State *L)
{	//STACK: rdtbl wttbl? extbl? timems?
	fd_set rd;
	fd_set wt;
	fd_set ex;
	fd_set *ptrwt = NULL;
	fd_set *ptrex = NULL;
	struct timeval tv;
	struct timeval *ptrtv = NULL;
	int i;
	int count;
	int resindx;
	int result;
#if defined(LUA_NUMBER_DOUBLE) || defined(LUA_NUMBER_FLOAT)
	lua_Number time;
#else
	int time;
#endif
	SOCKET sock;
#ifdef _WIN32
# define fdmax 0
#else
	SOCKET fdmax = 0;
#endif

	luaL_checktype(L, 1, LUA_TTABLE);
	count = luaL_len(L, 1);
	FD_ZERO(&rd);
	for (i = 1; i <= count; ++i)
	{
		lua_pushinteger(L, i);						//STACK: rdtbl wttbl? extbl? timems? i
		lua_gettable(L, 1);							//STACK: rdtbl wttbl? extbl? timems? rdtbl[i]
		sock = *((SOCKET *)luaL_checkudata(L, -1, "luasockets_socket_mt"));
		lua_pop(L, 1);								//STACK: rdtbl wttbl? extbl? timems?

#ifndef _WIN32
		fdmax = max(fdmax, sock);
#endif

		FD_SET(sock, &rd);
	}

	if (lua_gettop(L) > 1)
	{
		if (!lua_isnil(L, 2))
		{
			luaL_checktype(L, 2, LUA_TTABLE);
			count = luaL_len(L, 2);
			FD_ZERO(&wt);
			ptrwt = &wt;
			for (i = 1; i <= count; ++i)
			{
				lua_pushinteger(L, i);				//STACK: rdtbl wttbl? extbl? timems? i
				lua_gettable(L, 2);					//STACK: rdtbl wttbl? extbl? timems? wttbl[i]
				sock = *((SOCKET *)luaL_checkudata(L, -1, "luasockets_socket_mt"));
				lua_pop(L, 1);						//STACK: rdtbl wttbl? extbl? timems?

#ifndef _WIN32
				fdmax = max(fdmax, sock);
#endif

				FD_SET(sock, &wt);
			}
		}

		if (lua_gettop(L) > 2)
		{
			if (!lua_isnil(L, 3))
			{
				luaL_checktype(L, 3, LUA_TTABLE);
				count = luaL_len(L, 3);
				FD_ZERO(&ex);
				ptrex = &ex;
				for (i = 1; i <= count; ++i)
				{
					lua_pushinteger(L, i);			//STACK: rdtbl wttbl? extbl? timems? i
					lua_gettable(L, 3);				//STACK: rdtbl wttbl? extbl? timems? extbl[i]
					sock = *((SOCKET *)luaL_checkudata(L, -1, "luasockets_socket_mt"));
					lua_pop(L, 1);					//STACK: rdtbl wttbl? extbl? timems?

#ifndef _WIN32
					fdmax = max(fdmax, sock);
#endif

					FD_SET(sock, &ex);
				}
			}

			if (lua_gettop(L) > 3)
			{
				time = luaL_checknumber(L, 4);
				if (time < 0)
				{
					luaL_error(L, "timeout must be non-negative");
				}

#if defined(LUA_NUMBER_DOUBLE) || defined(LUA_NUMBER_FLOAT)
				time /= (lua_Number)1000;
				tv.tv_sec = (long)floor(time);
				tv.tv_usec = (long)ceil((time - ((lua_Number)tv.tv_sec)) * ((lua_Number)1000000));
#else
				tv.tv_sec = (time / 1000);
				tv.tv_usec = (time - (tv.tv_sec * 1000)) * 1000);
				if (tv.tv_usec < 0)
				{
					tv.tv_usec = 0;
				}
#endif
				ptrtv = &tv;
			}
		}
	}

	result = select(fdmax, &rd, ptrwt, ptrex, ptrtv);

	if (result == 0)
	{
		lua_pushnil(L);								//STACK: rdtbl wttbl? extbl? timems? nil
		return 1;									//RETURN: nil
	}
	else if (result < 0)
	{
		luaerrorec(L, sockerr);
	}

	lua_newtable(L);								//STACK: rdtbl wttbl? extbl? timems? {~0}
	lua_pushliteral(L, "read");						//STACK: rdtbl wttbl? extbl? timems? {~0} 'read'
	lua_newtable(L);								//STACK: rdtbl wttbl? extbl? timems? {~0} 'read' {~1}

	count = luaL_len(L, 1);
	resindx = 1;
	for (i = 1; i <= count; ++i)
	{
		lua_pushinteger(L, i);						//STACK: rdtbl wttbl? extbl? timems? {~0} 'read' {~1} i
		lua_gettable(L, 1);							//STACK: rdtbl wttbl? extbl? timems? {~0} 'read' {~1} rdtbl[i]
		if (FD_ISSET(*((SOCKET *)lua_touserdata(L, -1)), &rd))
		{
			lua_pushinteger(L, resindx++);			//STACK: rdtbl wttbl? extbl? timems? {~0} 'read' {~1} rdtbl[i] resindx
			lua_pushvalue(L, -2);					//STACK: rdtbl wttbl? extbl? timems? {~0} 'read' {~1} rdtbl[i] resindx rdtbl[i]
			lua_settable(L, -4);					//STACK: rdtbl wttbl? extbl? timems? {~0} 'read' {~1} rdtbl[i]
		}
		lua_pop(L, 1);								//STACK: rdtbl wttbl? extbl? timems? {~0} 'read' {~1}
	}
	lua_settable(L, -3);							//STACK: rdtbl wttbl? extbl? timems? {~0}

	if (ptrwt != NULL)
	{
		lua_pushliteral(L, "write");				//STACK: rdtbl wttbl? extbl? timems? {~0} 'write'
		lua_newtable(L);							//STACK: rdtbl wttbl? extbl? timems? {~0} 'write' {~2}

		count = luaL_len(L, 1);
		resindx = 1;
		for (i = 1; i <= count; ++i)
		{
			lua_pushinteger(L, i);					//STACK: rdtbl wttbl? extbl? timems? {~0} 'write' {~2} i
			lua_gettable(L, 2);						//STACK: rdtbl wttbl? extbl? timems? {~0} 'write' {~2} wttbl[i]
			if (FD_ISSET(*((SOCKET *)lua_touserdata(L, -1)), &wt))
			{
				lua_pushinteger(L, resindx++);		//STACK: rdtbl wttbl? extbl? timems? {~0} 'write' {~2} wttbl[i] resindx
				lua_pushvalue(L, -2);				//STACK: rdtbl wttbl? extbl? timems? {~0} 'write' {~2} wttbl[i] resindx wttbl[i]
				lua_settable(L, -4);				//STACK: rdtbl wttbl? extbl? timems? {~0} 'write' {~2} wttbl[i]
			}
			lua_pop(L, 1);							//STACK: rdtbl wttbl? extbl? timems? {~0} 'write' {~2}
		}
		lua_settable(L, -3);						//STACK: rdtbl wttbl? extbl? timems? {~0}
	}

	if (ptrex != NULL)
	{
		lua_pushliteral(L, "ex");					//STACK: rdtbl wttbl? extbl? timems? {~0} 'ex'
		lua_newtable(L);							//STACK: rdtbl wttbl? extbl? timems? {~0} 'ex' {~3}

		count = luaL_len(L, 1);
		resindx = 1;
		for (i = 1; i <= count; ++i)
		{
			lua_pushinteger(L, i);					//STACK: rdtbl wttbl? extbl? timems? {~0} 'ex' {~3} i
			lua_gettable(L, 3);						//STACK: rdtbl wttbl? extbl? timems? {~0} 'ex' {~3} extbl[i]
			if (FD_ISSET(*((SOCKET *)lua_touserdata(L, -1)), &ex))
			{
				lua_pushinteger(L, resindx++);		//STACK: rdtbl wttbl? extbl? timems? {~0} 'ex' {~3} extbl[i] resindx
				lua_pushvalue(L, -2);				//STACK: rdtbl wttbl? extbl? timems? {~0} 'ex' {~3} extbl[i] resindx extbl[i]
				lua_settable(L, -4);				//STACK: rdtbl wttbl? extbl? timems? {~0} 'ex' {~3} extbl[i]
			}
			lua_pop(L, 1);							//STACK: rdtbl wttbl? extbl? timems? {~0} 'ex' {~3}
		}
		lua_settable(L, -3);						//STACK: rdtbl wttbl? extbl? timems? {~0}
	}

	return 1;										//RETURN: {~0}
}

//NOTICE: If you get garbage or access violations, check your definitions of u_long, s_long, u_short, and s_short.
LUA_CFUNCTION_F lua_sockets_ton(lua_State *L)
{	//STACK: num format ?
	lua_Number num;
	const char *format;
	char val[sizeof(u_long) + sizeof(char)];

	num = luaL_checknumber(L, 1);
	format = luaL_checkstring(L, 2);

	memset(val, 0, sizeof(val));

	if (strcmp(format, "us") == 0)
	{
		*((u_short *)val) = (u_short)num;
		*((u_short *)val) = htons(*((u_short *)val));
		lua_pushlstring(L, val, sizeof(u_short));		//STACK: num format ? val
	}
	else if (strcmp(format, "ss") == 0)
	{
		*((s_short *)val) = (s_short)num;
		*((s_short *)val) = htons(*((u_short *)val));
		lua_pushlstring(L, val, sizeof(u_short));		//STACK: num format ? val
	}
	else if (strcmp(format, "ul") == 0)
	{
		*((u_long *)val) = (u_long)num;
		*((u_long *)val) = htonl(*((u_long *)val));
		lua_pushlstring(L, val, sizeof(u_long));		//STACK: num format ? val
	}
	else if (strcmp(format, "sl") == 0)
	{
		*((s_long *)val) = (s_long)num;
		*((s_long *)val) = htonl(*((u_long *)val));
		lua_pushlstring(L, val, sizeof(u_long));		//STACK: num format ? val
	}
	else
	{
		luaL_error(L, "invalid format; expected 'us', 'ss', 'ul', or 'sl'");
	}

	return 1;					//RETURN: val
}

//NOTICE: If you get garbage or access violations, check your definitions of u_long, s_long, u_short, and s_short.
LUA_CFUNCTION_F lua_sockets_toh(lua_State *L)
{	//STACK: val unsigned ?
	const char *val;
	size_t valsz;
	u_long lval;
	u_short sval;
	int islong;

	val = luaL_checklstring(L, 1, &valsz);

	if (valsz == sizeof(u_long))
	{
		islong = 1;
		lval = ntohl(*((u_long *)val));
	}
	else if (valsz == sizeof(u_short))
	{
		islong = 0;
		sval = ntohs(*((u_short *)val));
	}
	else
	{
		luaL_error(L, "invalid value; unable to infer storage format from string length");
	}

	if (lua_gettop(L) == 2 && lua_toboolean(L, 2))
	{
		if (islong)
		{
			lua_pushnumber(L, lval);						//STACK: val unsigned ? res
		}
		else
		{
			lua_pushnumber(L, sval);						//STACK: val unsigned ? res
		}
	}
	else
	{
		if (islong)
		{
			lua_pushnumber(L, *((s_long *)(&lval)));		//STACK: val unsigned ? res
		}
		else
		{
			lua_pushnumber(L, *((s_short *)(&sval)));		//STACK: val unsigned ? res
		}
	}

	return 1;												//RETURN: res
}

const luaL_Reg luasockets_sockaddr_mt_funcs[] =
{
	{"__gc", &luasockets_sockaddr_mt___gc},
	{"__index", &luasockets_sockaddr_mt___index},
	{"__newindex", &luasockets_sockaddr_mt___newindex},
	{"__tostring", &luasockets_sockaddr_mt___tostring},
	{NULL, NULL}
};

const luaL_Reg luasockets_addrinfo_mt_funcs[] =
{
	{"__gc", &luasockets_addrinfo_mt___gc},
	{"__index", &luasockets_addrinfo_mt___index},
	{"__newindex", &luasockets_addrinfo_mt___newindex},
	{"__tostring", &luasockets_addrinfo_mt___tostring},
	{NULL, NULL}
};

const luaL_Reg luasockts_socket_mt_funcs[] =
{
	{"__gc", &luasockets_socket_mt___gc},
	{NULL, NULL}
};

const luaL_Reg luasockets_socket_mt____index_funcs[] =
{
	{"bind", &lua_sockets_bind},
	{"connect", &lua_sockets_connect},
	{"listen", &lua_sockets_listen},
	{"accept", &lua_sockets_accept},
	{"send", &lua_sockets_send},
	{"sendto", &lua_sockets_sendto},
	{"receive", &lua_sockets_recv},
	{"receivefrom", &lua_sockets_recvfrom},
	{"close", &lua_sockets_close},
	{"getpeername", &lua_sockets_getpeername},
	{NULL, NULL}
};

const luaL_Reg funcs[] =
{
	{"getaddrinfo", &lua_sockets_getaddrinfo},
	{"newendpoint", &lua_sockets_newsockaddr},
	{"newsocket", &lua_sockets_newsocket},
	{"gethostname", &lua_sockets_gethostname},
	{"select", &lua_sockets_select},
	{"tonet", &lua_sockets_ton},
	{"fromnet", &lua_sockets_toh},
	{NULL, NULL}
};

LUA_MODLOADER_F luaopen_sockets(lua_State *L)
{	//STACK: modname ?

#ifdef _WIN32
	struct WSAData wsaData;
	int wsastartupresult;
#endif

	luaL_requiref(L, "blob-lua", luaopen_blob, 0 /* false */);

#ifdef _WIN32
	//WARNING: WSACleanup() is never called if WSAStartup() succeeded.
	//         This shouldn't be a problem, as Windows should cleanup for us when the process terminates.
	//         (considering this uses Winsock 2, we don't really need to worry about pre-NT versions of Windows anyways)
	wsastartupresult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (wsastartupresult != 0)
	{
		WSACleanup();

		luaerrorec(L, wsastartupresult);
	}
#endif
	lua_pushliteral(L, "luasockets_sockaddr_mt");			//STACK: modname ? 'luasockets_sockaddr_mt'
	lua_createtable(L, 0, 4);								//STACK: modname ? 'luasockets_sockaddr_mt' {~0}
	luaL_setfuncs(L, luasockets_sockaddr_mt_funcs, 0);
	lua_pushliteral(L, "__metatable");						//STACK: modname ? 'luasockets_addrinfo_mt' {~0} '__metatable'
	lua_pushliteral(L, "protected (luasockets sockaddr)");	//STACK: modname ? 'luasockets_addrinfo_mt' {~0} '__metatable' 'protected...'
	lua_settable(L, -3);									//STACK: modname ? 'luasockets_addrinfo_mt' {~0}
	lua_settable(L, LUA_REGISTRYINDEX);						//STACK: modname ?

	lua_pushliteral(L, "luasockets_addrinfo_mt");			//STACK: modname ? 'luasockets_addrinfo_mt'
	lua_createtable(L, 0, 4);								//STACK: modname ? 'luasockets_addrinfo_mt' {~1}
	luaL_setfuncs(L, luasockets_addrinfo_mt_funcs, 0);
	lua_pushliteral(L, "__metatable");						//STACK: modname ? 'luasockets_addrinfo_mt' {~1} '__metatable'
	lua_pushliteral(L, "protected (luasockets addrinfo)");	//STACK: modname ? 'luasockets_addrinfo_mt' {~1} '__metatable' 'protected...'
	lua_settable(L, -3);									//STACK: modname ? 'luasockets_addrinfo_mt' {~1}
	lua_settable(L, LUA_REGISTRYINDEX);						//STACK: modname ?

	lua_pushliteral(L, "luasockets_sockaddr_parentref");	//STACK: modname ? 'luasockets_sockaddr_parentref' {~2}
	lua_newtable(L);										//STACK: modname ? 'luasockets_sockaddr_parentref' {~2}
	lua_settable(L, LUA_REGISTRYINDEX);						//STACK: modname ?

	lua_pushliteral(L, "luasockets_socket_mt");				//STACK: modname ? 'luasockets_socket_mt'
	lua_createtable(L, 0, 3);								//STACK: modname ? 'luasockets_socket_mt' {~3}
	luaL_setfuncs(L, luasockts_socket_mt_funcs, 0);
	lua_pushliteral(L, "__metatable");						//STACK: modname ? 'luasockets_socket_mt' {~3} '__metatable'
	lua_pushliteral(L, "protected (luasockets socket)");	//STACK: modname ? 'luasockets_socket_mt' {~3} '__metatable' 'protected...'
	lua_settable(L, -3);									//STACK: modname ? 'luasockets_socket_mt' {~3}
	lua_pushliteral(L, "__index");							//STACK: modname ? 'luasockets_socket_mt' {~3} '__index'
	lua_createtable(L, 0, 10);								//STACK: modname ? 'luasockets_socket_mt' {~3} '__index' {~4}
	luaL_setfuncs(L, luasockets_socket_mt____index_funcs, 0);
	lua_settable(L, -3);									//STACK: modname ? 'luasockets_socket_mt' {~3}
	lua_settable(L, LUA_REGISTRYINDEX);						//STACK: modname ?

	lua_createtable(L, 0, 5);								//STACK: modname ? {~5}
	luaL_setfuncs(L, funcs, 0);

	return 1;					//RETURN: {~5}
}