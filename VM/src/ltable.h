// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#pragma once

#include "lobject.h"

#define gnode(t, i) (&(t)->node[i])
#if defined(LUAU_T24_PACKED_NODE)
#define gkey(n) luauKeyView(n)
#else
#define gkey(n) (&(n)->key)
#endif
#define gval(n) (&(n)->val)
#if defined(LUAU_T24_PACKED_NODE)
#define gnext(n) ((n)->val.nodekey.next)
#else
#define gnext(n) ((n)->key.next)
#endif

#if LUAU_T24_FROZEN_HASH_SKIP_ACTIVE
// No metadata is read until the owner publishes the frozen hash index.
inline int luaH_frozenhashskip(const LuaTable* t, int i)
{
    unsigned size = 1u << t->lsizenode;
    if (LUAU_UNLIKELY(t->readonly & kFrozenHashIndexMask) && unsigned(i) < size)
#if LUAU_T24_COMPACT_VECTOR_CACHE_ACTIVE
        return int(luauCompactNodeCache(t->node, size)[i]);
#else
        return int(t->node[i].key.iterNext);
#endif
    return i;
}
#endif

#define gval2slot(t, v) int(cast_to(LuaNode*, static_cast<const TValue*>(v)) - t->node)

// A private writer must revoke the position-owned index before a key-owned
// cache marker can be stored. Existing marker-clear nodes remain safe misses.
inline void invalidateFrozenHashIndex(LuaTable* t)
{
#if LUAU_T24_BALANCED_VECTOR_CACHE_ACTIVE || (LUAU_T24_COMPACT_VECTOR_CACHE_ACTIVE && LUAU_T24_FROZEN_HASH_SKIP_ACTIVE)
    if (LUAU_UNLIKELY(t->readonly & kFrozenHashIndexMask))
        t->readonly &= uint8_t(~kFrozenHashIndexMask);
#else
    (void)t;
#endif
}

// reset cache of absent metamethods, cache is updated in luaT_gettm
inline uint8_t invalidateTMcache(LuaTable* t)
{
    invalidateFrozenHashIndex(t);
    return t->tmcache = 0;
}

LUAI_FUNC const TValue* luaH_getnum(LuaTable* t, int key);
LUAI_FUNC TValue* luaH_setnum(lua_State* L, LuaTable* t, int key);
LUAI_FUNC const TValue* luaH_getstr(LuaTable* t, TString* key);
LUAI_FUNC TValue* luaH_setstr(lua_State* L, LuaTable* t, TString* key);
LUAI_FUNC const TValue* luaH_getp(LuaTable* t, void* key, int tag);
LUAI_FUNC TValue* luaH_setp(lua_State* L, LuaTable* t, void* key, int tag);
LUAI_FUNC const TValue* luaH_get(LuaTable* t, const TValue* key);
LUAI_FUNC TValue* luaH_set(lua_State* L, LuaTable* t, const TValue* key);
LUAI_FUNC TValue* luaH_newkey(lua_State* L, LuaTable* t, const TValue* key);
#if LUAU_T24_NUMERIC_KEY_HASH
// Valid for only the primitive lookup/insertion attempt; discarded on rehash.
LUAI_FUNC const TValue* luaH_getnumberwithhash(LuaTable* t, const TValue* key, uint32_t* hash);
LUAI_FUNC TValue* luaH_newnumberwithhash(lua_State* L, LuaTable* t, const TValue* key, uint32_t hash);
LUAI_FUNC const TValue* luaH_getvectorwithhash(LuaTable* t, const TValue* key, uint32_t* hash);
LUAI_FUNC TValue* luaH_newvectorwithhash(lua_State* L, LuaTable* t, const TValue* key, uint32_t hash);
#endif
LUAI_FUNC LuaTable* luaH_new(lua_State* L, int narray, int lnhash);
LUAI_FUNC void luaH_resizearray(lua_State* L, LuaTable* t, int nasize);
LUAI_FUNC void luaH_resizehash(lua_State* L, LuaTable* t, int nhsize);
LUAI_FUNC void luaH_free(lua_State* L, LuaTable* t, struct lua_Page* page);
LUAI_FUNC int luaH_next(lua_State* L, LuaTable* t, StkId key);
LUAI_FUNC int luaH_rawfind(LuaTable* t, int startpos, StkId value);
LUAI_FUNC int luaH_getn(LuaTable* t);
LUAI_FUNC LuaTable* luaH_clone(lua_State* L, LuaTable* tt);
LUAI_FUNC void luaH_clear(LuaTable* tt);
LUAI_FUNC void luaH_setreadonly(lua_State* L, LuaTable* t, bool readonly);
LUAI_FUNC int luaH_getreadonly(LuaTable* t);

#define luaH_setslot(L, t, slot, key) (invalidateTMcache(t), (slot == luaO_nilobject ? luaH_newkey(L, t, key) : cast_to(TValue*, slot)))

extern const LuaNode luaH_dummynode;
