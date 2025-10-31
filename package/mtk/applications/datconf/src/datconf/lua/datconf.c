// SPDX-License-Identifier:	LGPL-2.1
/*
 * Lua plugin for datconf
 *
 * Copyright (C) 2019 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 */

#include <lauxlib.h>

#include <libdatconf.h>

#define MODNAME		"datconf"
#define TOPMETANAME	MODNAME ".meta"
#define OBJMETANAME	MODNAME ".obj.meta"

typedef struct kvc_context *(*get_context_t)(lua_State *L, int numArg);

#if !defined(LUA_VERSION_NUM) || (LUA_VERSION_NUM == 501)

/*
 * ** Adapted from Lua 5.2.0
 */
static void luaL_setfuncs (lua_State *L, const luaL_Reg *l, int nup)
{
	luaL_checkstack(L, nup + 1, "too many upvalues");

	/* fill the table with given functions */
	for (; l->name != NULL; l++) {
		int i;

		lua_pushstring(L, l->name);
		for (i = 0; i < nup; i++)  /* copy upvalues to the top */
			lua_pushvalue(L, -(nup + 1));
		/* closure with those upvalues */
		lua_pushcclosure(L, l->func, nup);
		lua_settable(L, -(nup + 3));
	}

	lua_pop(L, nup);  /* remove upvalues */
}

#define lua_rawlen(L, i) lua_objlen(L, i)

#endif

/* used by top table. should be unload after use */
static struct kvc_context *top_get_context(lua_State *L, int numArg)
{
	struct kvc_context *ctx = NULL;

	if (lua_isnumber(L, numArg))
		ctx = dat_load_by_index(luaL_checknumber(L, numArg));
	else if (lua_isstring(L, numArg))
		ctx = dat_load_by_name(luaL_checkstring(L, numArg));

	return ctx;
}

/* used by opened persist context. should be unload by calling obj:close() */
static struct kvc_context *obj_get_context(lua_State *L, int numArg)
{
	struct kvc_context *ctx = NULL, **pctx;

	if (lua_isuserdata(L, numArg)) {
		pctx = luaL_checkudata(L, numArg, OBJMETANAME);
		if (pctx)
			ctx = *pctx;
	}

	return ctx;
}

/* convert a string into context. should be unload after use */
static struct kvc_context *raw_get_context(lua_State *L, int numArg)
{
	struct kvc_context *ctx = NULL;
	const char *s;
	size_t len;

	if (lua_isstring(L, numArg)) {
		s = luaL_checklstring(L, numArg, &len);
		ctx = dat_load_raw(s, len);
	}

	return ctx;
}

static int datconf_get_all(lua_State *L, get_context_t get_context, int oneshot)
{
	struct kvc_context *ctx;
	struct kvc_enum_context *ectx;
	const union conf_item *item;

	ctx = get_context(L, 1);
	if (!ctx)
		goto error;

	lua_newtable(L);

	ectx = kvc_enum_init(ctx);

	while (!kvc_enum_next(ectx, &item)) {
		lua_pushstring(L, item->key);
		lua_pushstring(L, item->value);
		lua_settable(L, -3);
	}

	kvc_enum_end(ectx);

	if (oneshot)
		kvc_unload(ctx);

	return 1;

error:
	lua_pushnil(L);
	return 1;
}

static int datconf_count(lua_State *L, get_context_t get_context, int oneshot)
{
	struct kvc_context *ctx;

	ctx = get_context(L, 1);
	if (!ctx)
		goto error;

	lua_pushinteger(L, kvc_get_count(ctx));

	if (oneshot)
		kvc_unload(ctx);

	return 1;

error:
	lua_pushnil(L);
	return 1;
}

static int datconf_get(lua_State *L, get_context_t get_context, int oneshot)
{
	struct kvc_context *ctx;
	const char *value;

	if (!lua_isstring(L, 2))
		goto error;

	ctx = get_context(L, 1);
	if (!ctx)
		goto error;

	value = kvc_get(ctx, luaL_checkstring(L, 2));

	if (!value)
		lua_pushnil(L);
	else
		lua_pushstring(L, value);

	if (oneshot)
		kvc_unload(ctx);

	return 1;

error:
	lua_pushnil(L);
	return 1;
}

static int datconf_set(lua_State *L, get_context_t get_context, int oneshot)
{
	struct kvc_context *ctx;
	int ret;

	if (!lua_isstring(L, 2) || !lua_isstring(L, 3))
		goto error;

	ctx = get_context(L, 1);
	if (!ctx)
		goto error;

	ret = kvc_set(ctx, luaL_checkstring(L, 2), luaL_checkstring(L, 3));

	lua_pushinteger(L, !!ret);

	if (oneshot) {
		kvc_commit(ctx);
		kvc_unload(ctx);
	}

	return 1;

error:
	lua_pushinteger(L, 1);
	return 1;
}

static int datconf_unset(lua_State *L, get_context_t get_context, int oneshot)
{
	struct kvc_context *ctx;
	int ret;

	if (!lua_isstring(L, 2))
		goto error;

	ctx = get_context(L, 1);
	if (!ctx)
		goto error;

	ret = kvc_unset(ctx, luaL_checkstring(L, 2));

	lua_pushinteger(L, !!ret);

	if (oneshot) {
		kvc_commit(ctx);
		kvc_unload(ctx);
	}

	return 1;

error:
	lua_pushinteger(L, 1);
	return 1;
}

static int datconf_merge(lua_State *L, get_context_t get_context, int oneshot)
{
	struct kvc_context *ctx;

	if (lua_type(L, 2) != LUA_TTABLE)
		goto error;

	ctx = get_context(L, 1);
	if (!ctx)
		goto error;

	lua_pushnil(L);

	while (lua_next(L, 2)) {
		if (!lua_isstring(L, -1) || !lua_isstring(L, -2))
			continue;

		lua_pushvalue(L, -2);
		lua_pushvalue(L, -2);

		kvc_set(ctx, luaL_checkstring(L, -2), luaL_checkstring(L, -1));

		lua_pop(L, 3);
	}

	if (oneshot) {
		kvc_commit(ctx);
		kvc_unload(ctx);
	}

error:
	return 0;
}

static int datconf_top_parse(lua_State *L)
{
	return datconf_get_all(L, raw_get_context, 1);
}

static int datconf_top_get_all(lua_State *L)
{
	return datconf_get_all(L, top_get_context, 1);
}

static int datconf_top_count(lua_State *L)
{
	return datconf_count(L, top_get_context, 1);
}

static int datconf_top_get(lua_State *L)
{
	return datconf_get(L, top_get_context, 1);
}

static int datconf_top_set(lua_State *L)
{
	return datconf_set(L, top_get_context, 1);
}

static int datconf_top_unset(lua_State *L)
{
	return datconf_unset(L, top_get_context, 1);
}

static int datconf_top_merge(lua_State *L)
{
	return datconf_merge(L, top_get_context, 1);
}

static int datconf_top_open(lua_State *L)
{
	struct kvc_context **ctx = NULL;

	ctx = lua_newuserdata(L, sizeof(struct kvc_context *));
	if (!ctx)
		goto error;

	*ctx = top_get_context(L, 1);
	if (!*ctx)
		goto error;

	luaL_getmetatable(L, OBJMETANAME);
	lua_setmetatable(L, -2);

	return 1;

error:
	if (ctx)
		lua_pop(L, 1);

	lua_pushnil(L);
	return 1;
}

static int datconf_top_open_file(lua_State *L)
{
	struct kvc_context **ctx = NULL;

	if (!lua_isstring(L, 1))
		goto error;

	ctx = lua_newuserdata(L, sizeof(struct kvc_context *));
	if (!ctx)
		goto error;

	*ctx = dat_load(luaL_checkstring(L, 1));
	if (!*ctx)
		goto error;

	luaL_getmetatable(L, OBJMETANAME);
	lua_setmetatable(L, -2);

	return 1;

error:
	if (ctx)
		lua_pop(L, 1);

	lua_pushnil(L);
	return 1;
}

static int datconf_obj_get_all(lua_State *L)
{
	return datconf_get_all(L, obj_get_context, 0);
}

static int datconf_obj_get_count(lua_State *L)
{
	return datconf_count(L, obj_get_context, 0);
}

static int datconf_obj_get(lua_State *L)
{
	return datconf_get(L, obj_get_context, 0);
}

static int datconf_obj_set(lua_State *L)
{

	return datconf_set(L, obj_get_context, 0);
}

static int datconf_obj_unset(lua_State *L)
{
	return datconf_unset(L, obj_get_context, 0);
}

static int datconf_obj_merge(lua_State *L)
{
	return datconf_merge(L, obj_get_context, 0);
}

static int datconf_obj_commit(lua_State *L)
{
	struct kvc_context *ctx;

	ctx = obj_get_context(L, 1);
	if (!ctx)
		goto error;

	lua_pushinteger(L, !!kvc_commit(ctx));
	return 1;

error:
	lua_pushnil(L);
	return 1;
}

static int datconf_close(lua_State *L, int commit, int retval)
{
	struct kvc_context **pctx;
	int ret = 1;

	if (lua_isuserdata(L, 1)) {
		pctx = luaL_checkudata(L, 1, OBJMETANAME);
		if (pctx) {
			if (commit)
				kvc_commit(*pctx);
			kvc_unload(*pctx);
			*pctx = NULL;
			ret = 0;
		}
	}

	if (retval) {
		lua_pushinteger(L, ret);
		return 1;
	}

	return 0;
}

static int datconf_obj_gc(lua_State *L)
{
	return datconf_close(L, 0, 0);
}

static int datconf_obj_close(lua_State *L)
{
	int commit = 0;

	if (lua_isboolean(L, 2))
		commit = lua_toboolean(L, 2);

	return datconf_close(L, commit, 1);
}

static const luaL_Reg datconf_obj[]= {
	{ "__gc", datconf_obj_gc },
	{ "getall", datconf_obj_get_all },
	{ "count", datconf_obj_get_count },
	{ "get", datconf_obj_get },
	{ "set", datconf_obj_set },
	{ "unset", datconf_obj_unset },
	{ "merge", datconf_obj_merge },
	{ "commit", datconf_obj_commit },
	{ "close", datconf_obj_close },
	{ NULL, NULL }
};

static const luaL_Reg datconf_top[]= {
	{ "parse", datconf_top_parse },
	{ "getall", datconf_top_get_all },
	{ "getcount", datconf_top_count },
	{ "get", datconf_top_get },
	{ "set", datconf_top_set },
	{ "unset", datconf_top_unset },
	{ "merge", datconf_top_merge },
	{ "open", datconf_top_open },
	{ "openfile", datconf_top_open_file },
	{ NULL, NULL }
};

static void create_metatable(lua_State *L, const char *name, const luaL_Reg *l)
{
	/* create top metatable */
	luaL_newmetatable(L, name);

	/* metatable.__index = metatable */
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");

	/* fill metatable */
	luaL_setfuncs(L, l, 0);
	lua_pop(L, 1);
}

int luaopen_datconf(lua_State *L)
{
	/* create top metatable */
	create_metatable(L, TOPMETANAME, datconf_top);

	/* create object metatable */
	create_metatable(L, OBJMETANAME, datconf_obj);

	/* create module */
	lua_newtable(L);
	lua_pushvalue(L, -1);
	luaL_setfuncs(L, datconf_top, 0);
	lua_setglobal(L, MODNAME);

	return 1;
}
