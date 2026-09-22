// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/AssemblyBuilderA64.h"

#include "EmitCommon.h"

#include "lobject.h"
#include "ltm.h"
#include "lstate.h"

#include <limits.h>

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
inline constexpr unsigned kSpillSlots = sizeof(TValue) == 24 ? 24 : 22; // slots for spilling temporary registers

inline constexpr unsigned kStackSize = (kStashSlots + kTempSlots + kSpillSlots) * 8;

inline constexpr AddressA64 sSpillArea = mem(sp, (kStashSlots + kTempSlots) * 8);
inline constexpr AddressA64 sTemporary = mem(sp, kStashSlots * 8);

// Add a nonnegative byte offset without truncating the 12-bit immediate.
inline void emitAddOffset(AssemblyBuilderA64& build, RegisterA64 dst, RegisterA64 src, size_t offset)
{
    CODEGEN_ASSERT(offset <= INT_MAX);
    if (offset <= AssemblyBuilderA64::kMaxImmediate)
        build.add(dst, src, uint16_t(offset));
    else if (dst != src)
    {
        build.mov(dst, int(offset));
        build.add(dst, dst, src);
    }
    else
    {
        // In-place adjustments here are bounded by the VM's maximum frame size.
        while (offset > AssemblyBuilderA64::kMaxImmediate)
        {
            build.add(dst, src, uint16_t(AssemblyBuilderA64::kMaxImmediate));
            src = dst;
            offset -= AssemblyBuilderA64::kMaxImmediate;
        }
        build.add(dst, src, uint16_t(offset));
    }
}

inline void emitAddTValueIndex(AssemblyBuilderA64& build, RegisterA64 dst, RegisterA64 base, RegisterA64 index, RegisterA64 temp)
{
    if constexpr (sizeof(TValue) == 16)
        build.add(dst, base, index, kTValueSizeLog2);
    else
    {
        // Explicitly zero extend the 32-bit element index before scaling by 24.
        build.mov(castReg(KindA64::w, temp), index);
        build.add(temp, temp, temp, 1);
        build.add(dst, base, temp, 3);
    }
}

inline void emitCopyTValue(AssemblyBuilderA64& build, RegisterA64 temp, RegisterA64 dst, RegisterA64 src)
{
    build.ldr(temp, mem(src));
    build.str(temp, mem(dst));
    if constexpr (sizeof(TValue) > 16)
    {
        RegisterA64 tag = castReg(KindA64::s, temp);
        build.ldr(tag, mem(src, offsetof(TValue, tt)));
        build.str(tag, mem(dst, offsetof(TValue, tt)));
    }
}

inline void emitUpdateBase(AssemblyBuilderA64& build)
{
    build.ldr(rBase, mem(rState, offsetof(lua_State, base)));
}

} // namespace A64
} // namespace CodeGen
} // namespace Luau
