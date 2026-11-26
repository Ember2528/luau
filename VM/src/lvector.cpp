// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "lvector.h"

#include "lgc.h"
#include "lmem.h"
#if LUA_T16_NANTAG_VEC4
#include "lstate.h"
#include <string.h>
#endif

LuauVector* luaVec_newvector(lua_State* L, double x, double y, double z, double w)
{
    LuauVector* v = luaM_newgcofixed(L, LuauVector, sizevector(), L->activememcat, LUA_TVECTOR);
    luaC_init(L, v, LUA_TVECTOR);
    v->v[0] = (LUA_VECTOR_TYPE)x;
    v->v[1] = (LUA_VECTOR_TYPE)y;
    v->v[2] = (LUA_VECTOR_TYPE)z;
    condvector4(v->v[3] = (LUA_VECTOR_TYPE)w, (void)w);
    return v;
}

#if LUA_T16_NANTAG_VEC4
LUAU_NOINLINE void luaV_setnanvec_slow(lua_State* L, TValue* dest, float x, float y, float z, float w)
{
    LUAU_ASSERT(L != nullptr);
    // Copy raw bits before allocation to preserve signaling NaNs and survive observer callbacks.
    float lanes[4] = {x, y, z, w};
    uintptr_t addr = reinterpret_cast<uintptr_t>(dest);
    uintptr_t start = reinterpret_cast<uintptr_t>(L->stack);
    size_t bytes = size_t(L->stacksize) * sizeof(TValue);
    bool onstack = L->stack && addr >= start && addr - start < bytes && (addr - start) % sizeof(TValue) == 0;
    size_t index = onstack ? (addr - start) / sizeof(TValue) : 0;

    LuauVector* vec = luaM_newgcofixed(L, LuauVector, sizevector(), L->activememcat, LUA_TVECTOR);
    luaC_init(L, vec, LUA_TVECTOR);
    memcpy(vec->v, lanes, sizeof(lanes));
    if (onstack)
    {
        dest = L->stack + index;
        luaC_threadbarrier(L);
    }
    dest->value.gc = cast_to(GCObject*, vec);
    dest->tt.bits = kNaNTagEscapedVector;
    checkliveness(L->global, dest);
}
#endif

void luaVec_freevector(lua_State* L, LuauVector* v, lua_Page* page)
{
    luaM_freegcofixed(L, v, sizevector(), v->memcat, page);
}
