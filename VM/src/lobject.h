// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#pragma once

#include "lua.h"
#include "lcommon.h"
#include "lbytecode.h"

#if LUA_T16_NANTAG_VEC4
#include <string.h>
#endif

/*
** Union of all collectible objects
*/
typedef union GCObject GCObject;

/*
** Common Header for all collectible objects (in macro form, to be included in other objects)
*/
// clang-format off
#define CommonHeader \
     uint8_t tt; uint8_t marked; uint8_t memcat
// clang-format on

/*
** Common header in struct form
*/
typedef struct GCheader
{
    CommonHeader;
} GCheader;

/*
** Union of all Lua values
*/
typedef union
{
    GCObject* gc;
    void* p;
    double n;
    int b;
    float v[2]; // v[0], v[1] live here; v[2] lives in TValue::extra
} Value;

/*
** Tagged Values
*/

#if LUA_T16_NANTAG_VEC4
// Top-of-uint32 NaN-tag encoding for 16-byte four-wide float TValues:
//   0x00000000 .. 0xffffffdf : raw IEEE-754 float32 'w' lane of an inline float4 vector (non-collectable)
//   0xffffffe0 .. 0xffffffef : non-vector tags (kNaNTagBase | logicalTag), fitting in x64 imm8 & ARM64 CMN
//   0xffffffff               : escaped GC-boxed float4 vector (when raw w bits >= 0xffffffe0u; collectable)
// This gives single-instruction scalar tag checks, single-instruction iscollectable (bits >= 0xffffffe5u),
// single-instruction inline-vector checks (bits < 0xffffffe0u), and branchless ttisvector ((bits + 1u) <= 0xffffffe0u).
inline constexpr uint32_t kNaNTagBase = 0xffffffe0u;
inline constexpr uint32_t kNaNTagEscapedVector = 0xffffffffu;
inline constexpr uint32_t kNaNTagCollectableMin = kNaNTagBase | uint32_t(LUA_TSTRING);

static_assert(LUA_T_ALL <= 31 && LUA_TVECTOR < LUA_TSTRING, "NaN-tag layout assumes <= 31 tags and LUA_TVECTOR < LUA_TSTRING");

inline constexpr uint32_t luauEncodeNaNTag(int tag)
{
    return tag == LUA_TVECTOR ? kNaNTagEscapedVector : (kNaNTagBase | uint32_t(tag));
}

inline constexpr int luauDecodeNaNTag(uint32_t bits)
{
    return uint32_t(bits + 1u) <= kNaNTagBase ? int(LUA_TVECTOR) : int(bits & 0x1fu);
}

struct NaNTag
{
    uint32_t bits;

    inline operator int() const
    {
        return luauDecodeNaNTag(bits);
    }

    inline NaNTag& operator=(int tag)
    {
        bits = luauEncodeNaNTag(tag);
        return *this;
    }

    inline bool operator==(int tag) const
    {
        return tag == LUA_TVECTOR ? (uint32_t(bits + 1u) <= kNaNTagBase) : (bits == (kNaNTagBase | uint32_t(tag)));
    }

    inline bool operator!=(int tag) const
    {
        return !(*this == tag);
    }

    inline bool operator==(NaNTag other) const
    {
        if (bits == other.bits)
            return true;
        return uint32_t(bits + 1u) <= kNaNTagBase && uint32_t(other.bits + 1u) <= kNaNTagBase;
    }

    inline bool operator!=(NaNTag other) const
    {
        return !(*this == other);
    }
};

inline bool operator==(int tag, NaNTag t)
{
    return t == tag;
}

inline bool operator!=(int tag, NaNTag t)
{
    return t != tag;
}

inline bool operator==(unsigned int tag, NaNTag t)
{
    return t == int(tag);
}

inline bool operator!=(unsigned int tag, NaNTag t)
{
    return t != int(tag);
}
#endif

typedef struct lua_TValue
{
    Value value;
    int extra[LUA_EXTRA_SIZE];
#if LUA_T16_NANTAG_VEC4
    NaNTag tt;
#else
    int tt;
#endif
} TValue;

#if LUA_T16_NANTAG_VEC4
static_assert(sizeof(TValue) == 16, "NaN-tagged float4 TValue must remain 16 bytes");
#endif

#if LUA_VECTOR_SIZE == 4
#define condvector4(vec4expr, vec3expr) vec4expr
#else
#define condvector4(vec4expr, vec3expr) vec3expr
#endif

#if LUA_VECTOR_DOUBLE == 1
#define condvectordouble(vecdoubleexpr, vecfloatexpr) vecdoubleexpr
#else
#define condvectordouble(vecdoubleexpr, vecfloatexpr) vecfloatexpr
#endif

// Macros to test type
#define ttisnil(o) (ttype(o) == LUA_TNIL)
#define ttisnumber(o) (ttype(o) == LUA_TNUMBER)
#define ttisstring(o) (ttype(o) == LUA_TSTRING)
#define ttistable(o) (ttype(o) == LUA_TTABLE)
#define ttisfunction(o) (ttype(o) == LUA_TFUNCTION)
#define ttisboolean(o) (ttype(o) == LUA_TBOOLEAN)
#define ttisuserdata(o) (ttype(o) == LUA_TUSERDATA)
#define ttisthread(o) (ttype(o) == LUA_TTHREAD)
#define ttisbuffer(o) (ttype(o) == LUA_TBUFFER)
#define ttislightuserdata(o) (ttype(o) == LUA_TLIGHTUSERDATA)
#define ttisvector(o) (ttype(o) == LUA_TVECTOR)
#define ttisupval(o) (ttype(o) == LUA_TUPVAL)
#define ttisclass(o) (ttype(o) == LUA_TCLASS)
#define ttisobject(o) (ttype(o) == LUA_TOBJECT)

// Macros to access values
#define ttype(o) ((o)->tt)
#define gcvalue(o) check_exp(iscollectable(o), (o)->value.gc)
#define pvalue(o) check_exp(ttislightuserdata(o), (o)->value.p)
#define nvalue(o) check_exp(ttisnumber(o), (o)->value.n)
#if LUA_T16_NANTAG_VEC4
struct TKey;
inline bool luauIsInlineVector(const TValue* o)
{
    return o->tt.bits < kNaNTagBase;
}
inline bool luauIsCollectable(const TValue* o)
{
    return o->tt.bits >= kNaNTagCollectableMin;
}
inline bool luauIsCollectable(const TKey* k);
template<typename T>
inline bool luauIsCollectable(const T* o)
{
    return o->tt >= LUA_TSTRING || o->tt == LUA_TVECTOR;
}
inline float* luauVectorPtr(TValue* o);
inline const float* luauVectorPtr(const TValue* o);
inline float* luauVectorPtr(TKey* k);
inline const float* luauVectorPtr(const TKey* k);
#define vvalue(o) check_exp(ttisvector(o), luauVectorPtr(o))
#else
#define vvalue(o) check_exp(ttisvector(o), condvectordouble((o)->value.gc->vec.v, (o)->value.v))
#endif
#define tsvalue(o) check_exp(ttisstring(o), &(o)->value.gc->ts)
#define uvalue(o) check_exp(ttisuserdata(o), &(o)->value.gc->u)
#define clvalue(o) check_exp(ttisfunction(o), &(o)->value.gc->cl)
#define hvalue(o) check_exp(ttistable(o), &(o)->value.gc->h)
#define bvalue(o) check_exp(ttisboolean(o), (o)->value.b)
#define thvalue(o) check_exp(ttisthread(o), &(o)->value.gc->th)
#define bufvalue(o) check_exp(ttisbuffer(o), &(o)->value.gc->buf)
#define upvalue(o) check_exp(ttisupval(o), &(o)->value.gc->uv)
#define classvalue(o) check_exp(ttisclass(o), &(o)->value.gc->lclass)
#define objectvalue(o) check_exp(ttisobject(o), &(o)->value.gc->lobject)

#define l_isfalse(o) (ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))

#define lightuserdatatag(o) check_exp(ttislightuserdata(o), (o)->extra[0])

// Internal tags used by the VM
#define LU_TAG_ITERATOR LUA_UTAG_LIMIT

/*
** for internal debug only
*/
#define checkconsistency(obj) LUAU_ASSERT(!iscollectable(obj) || (ttype(obj) == (obj)->value.gc->gch.tt))

#define checkliveness(g, obj) LUAU_ASSERT(!iscollectable(obj) || ((ttype(obj) == (obj)->value.gc->gch.tt) && !isdead(g, (obj)->value.gc)))

// Macros to set values
#define setnilvalue(obj) ((obj)->tt = LUA_TNIL)

#define setnvalue(obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.n = (x); \
        i_o->tt = LUA_TNUMBER; \
    }

#if LUA_VECTOR_DOUBLE == 1
#define setvvalue(L, obj, x, y, z, w) \
    { \
        TValue* i_o = (obj); \
        LuauVector* i_vec = luaVec_newvector((L), double(x), double(y), double(z), condvector4(double(w), 0)); \
        i_o->value.gc = cast_to(GCObject*, i_vec); \
        i_o->tt = LUA_TVECTOR; \
        checkliveness((L)->global, i_o); \
    }
#elif LUA_T16_NANTAG_VEC4
LUAI_FUNC void luaV_setnanvec_slow(struct lua_State* L, TValue* dest, float x, float y, float z, float w);
inline void luauSetNanVec(struct lua_State* L, TValue* dest, float x, float y, float z, float w)
{
    uint32_t wbits;
    memcpy(&wbits, &w, sizeof(wbits));
    if (LUAU_LIKELY(wbits < kNaNTagBase))
    {
        float lanes[4] = {x, y, z, w};
        memcpy(dest, lanes, sizeof(lanes));
    }
    else
    {
        luaV_setnanvec_slow(L, dest, x, y, z, w);
    }
}
#define setvvalue(L, obj, x, y, z, w) luauSetNanVec((L), (obj), float(x), float(y), float(z), float(w))
#else
#define setvvalue(L, obj, x, y, z, w) \
    { \
        TValue* i_o = (obj); \
        float* i_v = i_o->value.v; \
        i_v[0] = float(x); \
        i_v[1] = float(y); \
        i_v[2] = float(z); \
        condvector4(i_v[3] = float(w), (void)(w)); \
        i_o->tt = LUA_TVECTOR; \
    }
#endif

#define setpvalue(obj, x, tag) \
    { \
        TValue* i_o = (obj); \
        i_o->value.p = (x); \
        i_o->extra[0] = (tag); \
        i_o->tt = LUA_TLIGHTUSERDATA; \
    }

#define setbvalue(obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.b = (x); \
        i_o->tt = LUA_TBOOLEAN; \
    }

#define setsvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TSTRING; \
        checkliveness(L->global, i_o); \
    }

#define setuvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TUSERDATA; \
        checkliveness(L->global, i_o); \
    }

#define setthvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TTHREAD; \
        checkliveness(L->global, i_o); \
    }

#define setbufvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TBUFFER; \
        checkliveness(L->global, i_o); \
    }

#define setclvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TFUNCTION; \
        checkliveness(L->global, i_o); \
    }

#define sethvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TTABLE; \
        checkliveness(L->global, i_o); \
    }

#define setptvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TPROTO; \
        checkliveness(L->global, i_o); \
    }

#define setupvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TUPVAL; \
        checkliveness(L->global, i_o); \
    }

#define setobj(L, obj1, obj2) \
    { \
        const TValue* o2 = (obj2); \
        TValue* o1 = (obj1); \
        *o1 = *o2; \
        checkliveness(L->global, o1); \
    }

#define setclassvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TCLASS; \
        checkliveness(L->global, i_o); \
    }


#define setobjectvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TOBJECT; \
        checkliveness(L->global, i_o); \
    }

/*
** different types of sets, according to destination
*/

// to stack
#define setobj2s setobj
// from table to same table (no barrier)
#define setobjt2t setobj
// to table (needs barrier)
#define setobj2t setobj
// to new object (no barrier)
#define setobj2n setobj
// to class instance or static member (needs barrier)
#define setobj2class setobj

#define setttype(obj, tt) (ttype(obj) = (tt))

#if LUA_T16_NANTAG_VEC4
#define iscollectable(o) luauIsCollectable(o)
#else
#define iscollectable(o) (ttype(o) >= LUA_TSTRING)
#endif

// wrapper for returning userdata properties directly
struct DirectFieldResult
{
    lua_State* L;
    TValue* slot;
};

typedef TValue* StkId; // index to stack elements

/*
** String headers for string table
*/
typedef struct TString
{
    CommonHeader;
    // 1 byte padding

    int16_t atom;

    // 2 byte padding

    TString* next; // next string in the hash table bucket

    unsigned int hash;
    unsigned int len;

    char data[1]; // string data is allocated right after the header
} TString;

#define getstr(ts) (ts)->data
#define svalue(o) getstr(tsvalue(o))

typedef struct Udata
{
    CommonHeader;

    uint8_t tag;

    int len;

    struct LuaTable* metatable;

    // userdata is allocated right after the header
    // while the alignment is only 8 here, for sizes starting at 16 bytes, 16 byte alignment is provided
    alignas(8) char data[1];
} Udata;

typedef struct LuauBuffer
{
    CommonHeader;

    unsigned int len;

    alignas(8) char data[1];
} Buffer;

typedef struct LuauVector
{
    CommonHeader;
    // 1 byte padding

    LUA_VECTOR_TYPE v[LUA_VECTOR_SIZE];
} LuauVector;

enum FeedbackVectorSlotKind
{
    CALL_TARGET,
    CONSTRUCT,
};

enum LuauClassConstructMatch
{
    LCM_MISMATCH,
    LCM_MATCH,
    LCM_MATCH_ZEROED,
};

struct FeedbackVectorSlot
{
    FeedbackVectorSlotKind kind;

    union
    {
        struct
        {
            uint32_t pc;
            uint32_t proto;
            uint32_t hits;
        } call_target;

        struct
        {
            uint32_t shape;
            uint32_t classid;
            LuauClassConstructMatch match;
        } construct;
    };
};

enum ProtoExecFlag
{
    PEF_EXECLOCKED = 1 << 0,  // function will no longer be examined by custom execution
    PEF_DEBUGLOCKED = 1 << 1, // bytecode can have breakpoints in it
};

/*
** Function Prototypes
*/
// clang-format off
typedef struct Proto
{
    CommonHeader;

    uint8_t nups; // number of upvalues
    uint8_t numparams;
    uint8_t is_vararg;
    uint8_t maxstacksize;
    uint8_t flags;

    TValue* k;              // constants used by the function
    Instruction* code;      // function bytecode
    struct Proto** p;       // functions defined inside the function
    const Instruction* codeentry;

    void* execdata;
    uintptr_t exectarget;

    uint32_t execcount;
    uint8_t execflags;

    uint8_t* lineinfo;      // for each instruction, line number as a delta from baseline
    int* abslineinfo;       // baseline line info, one entry for each 1<<linegaplog2 instructions; allocated after lineinfo
    struct LocVar* locvars; // information about local variables
    TString** upvalues;     // upvalue names
    TString* source;

    TString* debugname;
    uint8_t* debuginsn; // a copy of code[] array with just opcodes

    uint8_t* typeinfo;

    void* userdata;

    GCObject* gclist;

    int sizecode;
    int sizep;
    int sizelocvars;
    int sizeupvalues;
    int sizek;
    int sizelineinfo;
    int linegaplog2;
    int linedefined;
    int bytecodeid;
    int sizetypeinfo;

    FeedbackVectorSlot* feedbackvec;
    uint32_t feedbackvecsize;
    uint32_t funid;
    Proto* optimized;
    Proto* deoptimized;
    uint64_t cost;

    // Set while the proto is a lazy-load stub (luau_loadlazy)
    struct LuauLazyChunk* lazychunk;
} Proto;
// clang-format on

static const uint32_t userdataTypeLimit = LBC_TYPE_TAGGED_USERDATA_END - LBC_TYPE_TAGGED_USERDATA_BASE;

// The bytecode of a luau_loadlazy chunk, shared by all of its stubs
typedef struct LuauLazyChunk
{
    const char* data;
    size_t size;
    void* owner;
    void (*release)(void* owner);

    uint32_t refs; // the load, every stub and every build in progress

    struct LuaTable* envt;
    struct LuaTable* importenv;

    uint8_t memcat;
    uint8_t version;
    uint8_t typesversion;
    uint8_t userdataRemapping[userdataTypeLimit];

    uint32_t* stringOffsets; // offset of each string's length
    uint32_t stringCount;
    uint32_t* protoOffsets; // offset of each function's maxstacksize byte
    uint32_t protoCount;
} LuauLazyChunk;

typedef struct LocVar
{
    TString* varname;
    int startpc; // first point where variable is active
    int endpc;   // first point where variable is dead
    uint8_t reg; // register slot, relative to base, where variable is stored
} LocVar;

/*
** Upvalues
*/

typedef struct UpVal
{
    CommonHeader;
    uint8_t markedopen; // set if reachable from an alive thread (only valid during atomic)

    // 4 byte padding (x64)

    TValue* v; // points to stack or to its own value
    union
    {
        TValue value; // the value (when closed)
        struct
        {
            // global double linked list (when open)
            struct UpVal* prev;
            struct UpVal* next;

            // thread linked list (when open)
            struct UpVal* threadnext;
        } open;
    } u;
} UpVal;

#define upisopen(up) ((up)->v != &(up)->u.value)

/*
** Closures
*/

typedef struct Closure
{
    CommonHeader;

    uint8_t isC;
    uint8_t nupvalues;
    uint8_t stacksize;
    uint8_t preload;

    GCObject* gclist;
    struct LuaTable* env;

    union
    {
        struct
        {
            lua_CFunction f;
            lua_Continuation cont;
            TString* debugname;
            TValue upvals[1];
        } c;

        struct
        {
            struct Proto* p;
            TValue uprefs[1];
        } l;
    };
} Closure;

#define iscfunction(o) (ttype(o) == LUA_TFUNCTION && clvalue(o)->isC)
#define isLfunction(o) (ttype(o) == LUA_TFUNCTION && !clvalue(o)->isC)

/*
** Tables
*/

typedef struct TKey
{
    ::Value value;
    int extra[LUA_EXTRA_SIZE];
    unsigned tt : 4;
    int next : 28; // for chaining
} TKey;

typedef struct LuaNode
{
    TValue val;
    TKey key;
} LuaNode;

#if LUA_T16_NANTAG_VEC4
struct LuauVectorKeySlot
{
    float v[4];
};
static_assert(sizeof(LuauVectorKeySlot) == 16, "LuauVectorKeySlot must be 16 bytes");
static_assert(sizeof(TKey) == 16 && sizeof(LuaNode) == 32, "NaN-tagged float4 LuaNode must remain 32 bytes");

// Table-only metadata bit stored in the upper unused bits of LuaTable::marked
// (GC only uses bits 0..3: WHITE0BIT, WHITE1BIT, BLACKBIT, FIXEDBIT).
constexpr uint8_t kVectorKeySidecarOwnedMask = 1u << 4;

inline LuauVectorKeySlot* luauVectorKeySidecar(LuaNode* node, size_t count)
{
    return reinterpret_cast<LuauVectorKeySlot*>(node + count);
}
inline const LuauVectorKeySlot* luauVectorKeySidecar(const LuaNode* node, size_t count)
{
    return reinterpret_cast<const LuauVectorKeySlot*>(node + count);
}

inline bool luauIsCollectable(const TKey* k)
{
    return k->tt >= LUA_TSTRING;
}
inline float* luauVectorPtr(TValue* o)
{
    return LUAU_LIKELY(o->tt.bits < kNaNTagBase) ? reinterpret_cast<float*>(o) : reinterpret_cast<LuauVector*>(o->value.gc)->v;
}
inline const float* luauVectorPtr(const TValue* o)
{
    return LUAU_LIKELY(o->tt.bits < kNaNTagBase) ? reinterpret_cast<const float*>(o) : reinterpret_cast<const LuauVector*>(o->value.gc)->v;
}
inline float* luauVectorPtr(TKey* k)
{
    return static_cast<float*>(k->value.p);
}
inline const float* luauVectorPtr(const TKey* k)
{
    return static_cast<const float*>(k->value.p);
}
#endif

inline constexpr size_t luauNodeStorageBytes(size_t count, bool sidecar = false)
{
#if LUA_T16_NANTAG_VEC4
    return count * (sizeof(LuaNode) + (sidecar ? sizeof(LuauVectorKeySlot) : 0));
#else
    (void)sidecar;
    return count * sizeof(LuaNode);
#endif
}

// copy a value into a key
#if LUA_T16_NANTAG_VEC4
// Under LUA_T16_NANTAG_VEC4, vector keys can never contain NaN (so escaped LuauVectors are
// always rejected by luaH_newkey) and live exclusively in the table's lazy inline sidecar
// (value.p -> float[4], extra[0] = cached 32-bit hash, non-collectable), keeping
// sizeof(TKey) == 16 and sizeof(LuaNode) == 32. Vector key insertions are handled directly
// in ltable.cpp; non-vector keys copy value, extra, and decoded 4-bit tag.
#define setnodekey(L, node, obj) \
    { \
        LuaNode* n_ = (node); \
        const TValue* i_o = (obj); \
        LUAU_ASSERT(!ttisvector(i_o)); \
        n_->key.value = i_o->value; \
        memcpy(n_->key.extra, i_o->extra, sizeof(n_->key.extra)); \
        n_->key.tt = unsigned(int(i_o->tt)); \
        checkliveness(L->global, i_o); \
    }

// copy a value from a key (never allocates: all vector keys are valid inline float4s in the sidecar)
#define getnodekey(L, obj, node) \
    { \
        TValue* i_o = (obj); \
        const LuaNode* n_ = (node); \
        uint32_t i_tt = n_->key.tt; \
        if (LUAU_UNLIKELY(i_tt == LUA_TVECTOR)) \
            memcpy(i_o, n_->key.value.p, 4 * sizeof(float)); \
        else \
        { \
            i_o->value = n_->key.value; \
            i_o->extra[0] = n_->key.extra[0]; \
            i_o->tt.bits = kNaNTagBase | i_tt; \
        } \
        checkliveness(L->global, i_o); \
    }
#else
#define setnodekey(L, node, obj) \
    { \
        LuaNode* n_ = (node); \
        const TValue* i_o = (obj); \
        n_->key.value = i_o->value; \
        memcpy(n_->key.extra, i_o->extra, sizeof(n_->key.extra)); \
        n_->key.tt = i_o->tt; \
        checkliveness(L->global, i_o); \
    }

// copy a value from a key
#define getnodekey(L, obj, node) \
    { \
        TValue* i_o = (obj); \
        const LuaNode* n_ = (node); \
        i_o->value = n_->key.value; \
        memcpy(i_o->extra, n_->key.extra, sizeof(i_o->extra)); \
        i_o->tt = n_->key.tt; \
        checkliveness(L->global, i_o); \
    }
#endif

// clang-format off
typedef struct LuaTable
{
    CommonHeader;

    uint8_t tmcache;    // 1<<p means tagmethod(p) is not present
    uint8_t readonly;   // bit 0 - prohibit writes to table, bit 1 - array contains a metamethod cache
    uint8_t safeenv;    // environment doesn't share globals with other scripts
    uint8_t lsizenode;  // log2 of size of `node' array
    uint8_t nodemask8;  // (1<<lsizenode)-1, truncated to 8 bits

    int sizearray; // size of `array' array
    union
    {
        int lastfree;  // any free position is before this position
        int aboundary; // negated 'boundary' of `array' array; iff aboundary < 0
    };

    struct LuaTable* metatable;
    TValue* array;  // array part
    LuaNode* node;
    GCObject* gclist;
} LuaTable;
// clang-format on

inline bool luauHasVectorKeySidecar(const LuaTable* t)
{
#if LUA_T16_NANTAG_VEC4
    return (t->marked & kVectorKeySidecarOwnedMask) != 0;
#else
    (void)t;
    return false;
#endif
}

inline void luauSetVectorKeySidecarOwned(LuaTable* t, bool owned)
{
#if LUA_T16_NANTAG_VEC4
    t->marked = uint8_t((t->marked & ~kVectorKeySidecarOwnedMask) | (owned ? kVectorKeySidecarOwnedMask : 0));
#else
    (void)t;
    (void)owned;
#endif
}

typedef struct LuauClass
{
    CommonHeader;

    GCObject* gclist;

    TString* name;

    // Used to match feedback vector construction slots
    uint32_t id;

    // The superclass of this class. NULL if this class doesn't inherit.
    LuauClass* super;

    // Mapping from offset to static members (only methods for now). Private members at the end.
    TValue* staticmembers;
    // We store a pointer to the private static members separately as an optimization to avoid computing an overall offset into static members.
    TValue* privatestaticmembers;

    // Mapping from public member name to offset.
    // For static members, subtracting numberofinstancemembers from the offset gives the actual index into staticmembers.
    LuaTable* memberstooffset;

    // Mapping from offset to member name. Instance member offsets are stored before static member offsets. Offsets corresponding to private
    // fields are NULL, and there are no entries for private static members.
    TString** offsettomember;

    // Metatable for this *class object*. At time of writing this only contains __call
    LuaTable* metatable;

    // Metatable for instances of this class. NULL until the first metamethod
    // is added via luaR_addclassmember.
    LuaTable* instancemetatable;

    // Number of public and private instance members that we expect instances of this class object to have.
    uint32_t numberofinstancemembers;

    // Total number of members that we expect this class object to have between instance and static members.
    //
    // We store this number as an optimization. It's pretty rare that we need
    // to reference the specific number of static members, but it's very common
    // to reference the total number of members (for validating hot paths in
    // the interpreter) and the number of instance members (branching on
    // instance or static members, creating class instances).
    uint32_t numberofmembers;

    uint32_t numberofprivatefields;
    // If this class inherits, its ancestors' private fields come before this class's, so we track where its own private fields start.
    uint32_t ownprivatefieldsstart;

    uint8_t numberofprivatestaticmembers;

    // Can this class be extended?
    bool isopen;

    // True if this class or any of its ancestors defines an __init method.
    // If a class's ancestors define an __init method, it must itself also define an __init method.
    // We cannot determine this statically, so we track it here to error at runtime if the invariant is violated.
    // The default constructor errors if this is true, which works because the default constructor is overridden if a class defines an __init method.
    bool hasuserinitinchain;
} LuauClass;

typedef struct LuauObject
{
    CommonHeader;

    GCObject* gclist;

    // The class object that this value is an instance of.
    LuauClass* lclass;

    // The number of members that this instance contains. We need these in order to free ourselves if we get swept in the same GC cycle as our class
    // pointer.
    uint32_t numberofmembers;

    // The fields of this instance. Members are ordered according to inheritance chain, with base classes coming first. For each class, public members
    // come first, then privates.
    TValue* members;
} LuauObject;

/*
** `module' operation for hashing (size is always a power of 2)
*/
#define lmod(s, size) (check_exp((size & (size - 1)) == 0, (cast_to(int, (s) & ((size) - 1)))))

#define twoto(x) ((int)(1 << (x)))
#define sizenode(t) (twoto((t)->lsizenode))
#define hasmetacache(t) (((t)->readonly & 2) != 0)
#define getmetacache(t, event) ((t)->array - (1 + (event)))

#define luaO_nilobject (&luaO_nilobject_)

LUAI_DATA const TValue luaO_nilobject_;

#define ceillog2(x) (luaO_log2((x) - 1) + 1)

LUAI_FUNC int luaO_log2(unsigned int x);
LUAI_FUNC int luaO_rawequalObj(const TValue* t1, const TValue* t2);
LUAI_FUNC int luaO_rawequalKey(const TKey* t1, const TValue* t2);
LUAI_FUNC int luaO_str2d(const char* s, double* result);
LUAI_FUNC const char* luaO_pushvfstring(lua_State* L, const char* fmt, va_list argp);
LUAI_FUNC const char* luaO_pushfstring(lua_State* L, const char* fmt, ...);
LUAI_FUNC const char* luaO_chunkid(char* buf, size_t buflen, const char* source, size_t srclen);
