/*
 * Copyright (c) 2013 - 2020 Micro Systems Marc Balmer, CH-5073 Gipf-Oberfrick
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Micro Systems Marc Balmer nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL MICRO SYSTEMS MARC BALMER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* FastCGI interface for Lua */

#include <fcgiapp.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <stdlib.h>
#include <string.h>

#define FCGX_REQUEST_METATABLE	"FastCGI request"

static int
fcgi_open_socket(lua_State *L)
{
	int sock;

	sock = FCGX_OpenSocket(luaL_checkstring(L, 1), luaL_checkinteger(L, 2));
	lua_pushinteger(L, sock);
	return 1;
}

static int
fcgi_init_request(lua_State *L)
{
	FCGX_Request *req;
	int socket;

	socket = luaL_checkinteger(L, 1);

	req = lua_newuserdata(L, sizeof(FCGX_Request));
	luaL_setmetatable(L, FCGX_REQUEST_METATABLE);
	if (FCGX_InitRequest(req, socket, 0))
		lua_pushnil(L);
	return 1;
}

/* Request methods */
static int
fcgi_accept(lua_State *L)
{
	FCGX_Request *req = luaL_checkudata(L, 1, FCGX_REQUEST_METATABLE);

	lua_pushinteger(L, FCGX_Accept_r(req));
	return 1;
}

static int
fcgi_finish(lua_State *L)
{
	FCGX_Request *req = luaL_checkudata(L, 1, FCGX_REQUEST_METATABLE);

	FCGX_Finish_r(req);
	return 0;
}

static int
fcgi_fflush(lua_State *L)
{
	FCGX_Request *req = luaL_checkudata(L, 1, FCGX_REQUEST_METATABLE);

	lua_pushinteger(L, FCGX_FFlush(req->out));
	return 1;
}

static int
fcgi_get_line(lua_State *L)
{
	FCGX_Request *req = luaL_checkudata(L, 1, FCGX_REQUEST_METATABLE);
	int len;
	char *str, *p;

	len = luaL_checkinteger(L, 2);
	str = malloc(len + 1);
	if (str == NULL)
		return luaL_error(L, "memory error");
	p = FCGX_GetLine(str, len, req->in);
	if (p == NULL)
		lua_pushnil(L);
	else
		lua_pushstring(L, str);
	free(str);
	return 1;
}

static int
fcgi_get_param(lua_State *L)
{
	FCGX_Request *req = luaL_checkudata(L, 1, FCGX_REQUEST_METATABLE);
	char *p;

	p = FCGX_GetParam(luaL_checkstring(L, 2), req->envp);
	if (p == NULL)
		lua_pushnil(L);
	else
		lua_pushstring(L, p);
	return 1;
}

static int
fcgi_get_env(lua_State *L)
{
	FCGX_Request *req = luaL_checkudata(L, 1, FCGX_REQUEST_METATABLE);

	char **p = req->envp;
	char *v;

	if (p == NULL)
		lua_pushnil(L);
	else {
		lua_newtable(L);
		for (; *p; p++) {
			v = strchr(*p, '=');
			if (v != NULL) {
				lua_pushlstring(L, *p, v - *p);
				lua_pushstring(L, ++v);
				lua_settable(L, -3);
			}
		}
	}
	return 1;
}

static int
fcgi_get_str(lua_State *L)
{
	FCGX_Request *req = luaL_checkudata(L, 1, FCGX_REQUEST_METATABLE);
	int len, rc;
	char *str;

	len = luaL_checkinteger(L, 2);
	str = malloc(len + 1);
	if (str == NULL)
		return luaL_error(L, "memory error");
	rc = FCGX_GetStr(str, len, req->in);
	if (rc == 0)
		lua_pushnil(L);
	else
		lua_pushstring(L, str);
	lua_pushinteger(L, rc);
	free(str);
	return 2;
}

static int
fcgi_put_str(lua_State *L)
{
	FCGX_Request *req = luaL_checkudata(L, 1, FCGX_REQUEST_METATABLE);
	const char *str;

	str = luaL_checkstring(L, 2);
	lua_pushinteger(L, FCGX_PutStr(str, strlen(str), req->out));
	return 1;
}

/* Convenience functions */
/* decode query string */
static void
lua_url_decode(lua_State *L, char *s)
{
	char *v, *p, *val, *q;
	char buf[3];
	int c;

	v = strchr(s, '=');
	if (v == NULL)
		return;
	*v++ = '\0';
	val = malloc(strlen(v) + 1);
	if (val == NULL)
		return;

	for (p = v, q = val; *p; p++) {
		switch (*p) {
		case '%':
			if (*(p + 1) == '\0' || *(p + 2) == '\0') {
				free(val);
				return;
			}
			buf[0] = *++p;
			buf[1] = *++p;
			buf[2] = '\0';
			sscanf(buf, "%2x", &c);
			*q++ = (char)c;
			break;
		case '+':
			*q++ = ' ';
			break;
		default:
			*q++ = *p;
		}
	}
	*q = '\0';
	lua_pushstring(L, val);
	lua_setfield(L, -2, s);
	free(val);
}

static void
lua_decode_query(lua_State *L, char *query)
{
	char *s;

	s = strtok(query, "&");
	while (s) {
		lua_url_decode(L, s);
		s = strtok(NULL, "&");
	}
}

static int
fcgi_parse(lua_State *L)
{
	FCGX_Request *req = luaL_checkudata(L, 1, FCGX_REQUEST_METATABLE);

	char *query, *content;
	int clen, rc;

	query =	FCGX_GetParam("QUERY_STRING", req->envp);
	clen = atoi(FCGX_GetParam("CONTENT_LENGTH", req->envp));

	lua_newtable(L);
	if ((query && *query) || clen > 0) {
		if (query && *query)
			lua_decode_query(L, query);

		if (clen > 0) {
			content = malloc(clen + 1);
			rc = FCGX_GetStr(content, clen, req->in);
			content[rc] = '\0';
			lua_decode_query(L, content);
			free(content);
		}
	}
	return 1;
}

int
luaopen_fcgi(lua_State* L)
{
	static const struct luaL_Reg methods[] = {
		{ "openSocket",		fcgi_open_socket },
		{ "initRequest",	fcgi_init_request },
		{ NULL,			NULL }
	};
	static const struct luaL_Reg request_methods[] = {
		{ "accept",		fcgi_accept },
		{ "finish",		fcgi_finish },
		{ "fflush",		fcgi_fflush },
		{ "getLine",		fcgi_get_line },
		{ "getParam",		fcgi_get_param },
		{ "getEnv",		fcgi_get_env },
		{ "getStr",		fcgi_get_str },
		{ "putStr",		fcgi_put_str },

		/* Convenience functions */
		{ "parse",		fcgi_parse },
		{ NULL,		NULL }
	};

	if (luaL_newmetatable(L, FCGX_REQUEST_METATABLE)) {
		luaL_setfuncs(L, request_methods, 0);

		lua_pushliteral(L, "__index");
		lua_pushvalue(L, -2);
		lua_settable(L, -3);

		lua_pushliteral(L, "__metatable");
		lua_pushliteral(L, "must not access this metatable");
		lua_settable(L, -3);
	}
	lua_pop(L, 1);

	luaL_newlib(L, methods);

	lua_pushliteral(L, "_COPYRIGHT");
	lua_pushliteral(L, "Copyright (C) 2013 - 2020 "
		"micro systems marc balmer");
	lua_settable(L, -3);
	lua_pushliteral(L, "_DESCRIPTION");
	lua_pushliteral(L, "FastCGI for Lua");
	lua_settable(L, -3);
	lua_pushliteral(L, "_VERSION");
	lua_pushliteral(L, "fcgi 1.2.0");
	lua_settable(L, -3);

	if (FCGX_Init())
		lua_pushnil(L);

	return 1;
}
