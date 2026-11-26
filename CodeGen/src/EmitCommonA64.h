// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/AssemblyBuilderA64.h"

#include "EmitCommon.h"

#include "lobject.h"
#include "ltm.h"
#include "lstate.h"

// AArch64 ABI reminder:
// Arguments: x0-x7, v0-v7
// Return: x0, v0 (or x8 that points to the address of the resulting structure)
// Volatile: x9-x15, v16-v31 ("caller-saved", any call may change them)
// Intra-procedure-call temporary: x16-x17 (any call or relocated jump may change them, as linker may point branches to veneers to perform long jumps)
// Non-volatile: x19-x28, v8-v15 ("callee-saved", preserved after calls, only bottom half of SIMD registers is preserved!)
// Reserved: x18: reserved for platform use; x29: frame pointer (unless omitted); x30: link register; x31: stack pointer

namespace Luau
{
namespace CodeGen
{

namespace A64
{

// Data that is very common to access is placed in non-volatile registers:
// 1. Constant registers (only loaded during codegen entry)
inline constexpr RegisterA64 rState = x19;         // lua_State* L
inline constexpr RegisterA64 rNativeContext = x20; // NativeContext* context
inline constexpr RegisterA64 rGlobalState = x21;   // global_State* L->global

// 2. Frame registers (reloaded when call frame changes; rBase is also reloaded after all calls that may reallocate stack)
inline constexpr RegisterA64 rConstants = x22; // TValue* k
inline constexpr RegisterA64 rClosure = x23;   // Closure* cl
inline constexpr RegisterA64 rCode = x24;      // Instruction* code
inline constexpr RegisterA64 rBase = x25;      // StkId base

// Native code is as stackless as the interpreter, so we can place some data on the stack once and have it accessible at any point
// See CodeGenA64.cpp for layout
inline constexpr unsigned kStashSlots = 9;  // stashed non-volatile registers
inline constexpr unsigned kTempSlots = 1;   // 8 bytes of temporary space, such luxury!
inline constexpr unsigned kSpillSlots = 22; // slots for spilling temporary registers

inline constexpr unsigned kStackSize = (kStashSlots + kTempSlots + kSpillSlots) * 8;

inline constexpr AddressA64 sSpillArea = mem(sp, (kStashSlots + kTempSlots) * 8);
inline constexpr AddressA64 sTemporary = mem(sp, kStashSlots * 8);

inline void emitUpdateBase(AssemblyBuilderA64& build)
{
    build.ldr(rBase, mem(rState, offsetof(lua_State, base)));
}

#if LUA_T16_NANTAG_VEC4
inline constexpr int kTagLoadOffset = int(offsetof(TValue, tt)) - 4;
inline constexpr uint16_t kNaNTagNegBaseA64 = uint16_t(0u - kNaNTagBase);          // 32
inline constexpr uint16_t kNaNTagNonVecRangeA64 = uint16_t(0u - kNaNTagBase - 1u); // 31

inline void emitCompareTagA64(AssemblyBuilderA64& build, RegisterA64 reg, uint8_t tag)
{
    CODEGEN_ASSERT(tag != LUA_TVECTOR);
    uint32_t neg = uint32_t(0u - (kNaNTagBase | uint32_t(tag)));
    CODEGEN_ASSERT(neg <= AssemblyBuilderA64::kMaxImmediate);
    build.cmn(reg, uint16_t(neg));
}

inline void emitLoadTagImmA64(AssemblyBuilderA64& build, RegisterA64 reg, uint8_t tag)
{
    build.mov(reg, tag == LUA_TVECTOR ? 0 : encodeTagImm(tag));
}

// 8-byte aligned load at [offsetof(TValue, tt) - 4] + LSR #32 avoids Cortex-A76 STLF stalls after 16-byte STR Q writes.
inline void emitLoadTagA64(AssemblyBuilderA64& build, RegisterA64 dstX, AddressA64 addr)
{
    CODEGEN_ASSERT(dstX.kind == KindA64::x);
    build.ldr(dstX, addr);
    build.lsr(dstX, dstX, 32);
}

inline void emitJumpIfNilA64(AssemblyBuilderA64& build, RegisterA64 regW, Label& target)
{
    CODEGEN_ASSERT(regW.kind == KindA64::w);
    emitCompareTagA64(build, regW, LUA_TNIL);
    build.b(ConditionA64::Equal, target);
}

inline void emitJumpIfNotNilA64(AssemblyBuilderA64& build, RegisterA64 regW, Label& target)
{
    CODEGEN_ASSERT(regW.kind == KindA64::w);
    emitCompareTagA64(build, regW, LUA_TNIL);
    build.b(ConditionA64::NotEqual, target);
}

inline void emitCheckCollectableA64(AssemblyBuilderA64& build, RegisterA64 regW, Label& skip)
{
    CODEGEN_ASSERT(regW.kind == KindA64::w);
    build.cmn(regW, uint16_t(0u - kNaNTagCollectableMin));
    build.b(ConditionA64::CarryClear, skip);
}

// Sets CarrySet iff rawW >= kNaNTagBase (not an inline vector).
inline void emitTestInlineVectorTagA64(AssemblyBuilderA64& build, RegisterA64 rawW)
{
    CODEGEN_ASSERT(rawW.kind == KindA64::w);
    build.cmn(rawW, kNaNTagNegBaseA64);
}

// Sets CarryClear iff rawW represents LUA_TVECTOR (inline < kNaNTagBase or escaped 0xffffffffu).
// Adding 1 maps 0xffffffffu -> 0 and inline [0..kNaNTagBase-1] -> [1..kNaNTagBase], so (rawW + 1) + 31 carries iff non-vector.
inline void emitTestVectorTagA64(AssemblyBuilderA64& build, RegisterA64 tmpW, RegisterA64 rawW)
{
    CODEGEN_ASSERT(tmpW.kind == KindA64::w && rawW.kind == KindA64::w);
    build.add(tmpW, rawW, uint16_t(1));
    build.cmn(tmpW, kNaNTagNonVecRangeA64);
}

// Decodes an encoded NaN-tag word (including inline and escaped vectors) to a 0..31 lua_Type value in dstW.
// dstW may alias rawW, while tmpW must be distinct from both.
inline void emitDecodeTagA64(AssemblyBuilderA64& build, RegisterA64 dstW, RegisterA64 tmpW, RegisterA64 rawW)
{
    CODEGEN_ASSERT(dstW.kind == KindA64::w && tmpW.kind == KindA64::w && rawW.kind == KindA64::w);
    CODEGEN_ASSERT(tmpW != dstW && tmpW != rawW);
    emitTestVectorTagA64(build, tmpW, rawW);
    build.ubfx(dstW, rawW, 0, 5);
    build.mov(tmpW, LUA_TVECTOR);
    build.csel(dstW, tmpW, dstW, ConditionA64::CarryClear);
}

// Materializes a 64-bit value with encodeTagImm(tag) in bits [63:32] and 0xffffffff in the
// unused TValue padding [31:0]. Writing 8 bytes at [offsetof(TValue, tt) - 4] matches
// LOAD_TAG's 8-byte read and avoids Cortex-A76 4B->8B store-to-load forwarding stalls.
// Must never be used for LUA_TLIGHTUSERDATA (which stores lightuserdatatag in extra[0])
// or inline LUA_TVECTOR (where [31:0] holds vector lane z).
inline void emitLoadTagImm64A64(AssemblyBuilderA64& build, RegisterA64 regX, uint8_t tag)
{
    CODEGEN_ASSERT(regX.kind == KindA64::x);
    CODEGEN_ASSERT(tag != LUA_TVECTOR && tag != LUA_TLIGHTUSERDATA && tag < 32);
    build.movn(regX, uint16_t(31u - tag), 32);
}

inline void emitStoreTagRegA64(AssemblyBuilderA64& build, RegisterA64 regX, AddressA64 addr)
{
    CODEGEN_ASSERT(regX.kind == KindA64::x);
    build.str(regX, addr);
}

inline void emitStoreTagImmA64(AssemblyBuilderA64& build, RegisterA64 regX, AddressA64 addr, uint8_t tag)
{
    emitLoadTagImm64A64(build, regX, tag);
    build.str(regX, addr);
}
#else
inline constexpr int kTagLoadOffset = int(offsetof(TValue, tt));

inline void emitCompareTagA64(AssemblyBuilderA64& build, RegisterA64 reg, uint8_t tag)
{
    build.cmp(reg, uint16_t(tag));
}

inline void emitLoadTagImmA64(AssemblyBuilderA64& build, RegisterA64 reg, uint8_t tag)
{
    build.mov(reg, tag);
}

inline void emitLoadTagA64(AssemblyBuilderA64& build, RegisterA64 dstX, AddressA64 addr)
{
    CODEGEN_ASSERT(dstX.kind == KindA64::x);
    build.ldr(castReg(KindA64::w, dstX), addr);
}

inline void emitJumpIfNilA64(AssemblyBuilderA64& build, RegisterA64 regW, Label& target)
{
    CODEGEN_ASSERT(regW.kind == KindA64::w && LUA_TNIL == 0);
    build.cbz(regW, target);
}

inline void emitJumpIfNotNilA64(AssemblyBuilderA64& build, RegisterA64 regW, Label& target)
{
    CODEGEN_ASSERT(regW.kind == KindA64::w && LUA_TNIL == 0);
    build.cbnz(regW, target);
}

inline void emitCheckCollectableA64(AssemblyBuilderA64& build, RegisterA64 regW, Label& skip)
{
    CODEGEN_ASSERT(regW.kind == KindA64::w);
    build.cmp(regW, uint16_t(LUA_TSTRING));
    build.b(ConditionA64::Less, skip);
}

inline void emitLoadTagImm64A64(AssemblyBuilderA64& build, RegisterA64 regX, uint8_t tag)
{
    CODEGEN_ASSERT(regX.kind == KindA64::x);
    build.mov(castReg(KindA64::w, regX), tag);
}

inline void emitStoreTagRegA64(AssemblyBuilderA64& build, RegisterA64 regX, AddressA64 addr)
{
    CODEGEN_ASSERT(regX.kind == KindA64::x);
    build.str(castReg(KindA64::w, regX), addr);
}

inline void emitStoreTagImmA64(AssemblyBuilderA64& build, RegisterA64 regX, AddressA64 addr, uint8_t tag)
{
    CODEGEN_ASSERT(regX.kind == KindA64::x);
    if (tag == 0)
        build.str(wzr, addr);
    else
    {
        RegisterA64 regW = castReg(KindA64::w, regX);
        build.mov(regW, tag);
        build.str(regW, addr);
    }
}
#endif

} // namespace A64
} // namespace CodeGen
} // namespace Luau
