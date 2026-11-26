# Experimental 16-Byte NaN-Tagged 4-Wide Float Vectors (`LUAU_T16_NANTAG_VEC4`)

Luau's opt-in `LUAU_T16_NANTAG_VEC4=ON` mode (combined with `LUAU_VECTOR_FOUR_WIDE=ON`, `LUA_VECTOR_SIZE == 4`, `LUA_VECTOR_DOUBLE == 0`) supports 4-wide `float` vectors while keeping the upstream 16-byte `TValue`, 32-byte `LuaNode`, and 48-byte `LuaTable` ABI unchanged by encoding non-vector tags inside the upper 32-bit word (`tt`) as high quiet-NaN bit patterns.

`LUAU_T16_NANTAG_VEC4` defaults to `OFF`, so enabling `LUAU_VECTOR_FOUR_WIDE=ON` alone preserves upstream's existing 24-byte `TValue` / 48-byte `LuaNode` four-wide layout untouched.

## Value Encoding

`TValue` remains 16 bytes with `value` (8 bytes), `extra` (4 bytes), and `tt` (4 bytes, overlapping lane `w` of a 4-wide `float` vector):

| `tt.bits` range (uint32) | Meaning |
| --- | --- |
| `< 0xffffffe0u` | Inline 4-wide `float` vector (`x`, `y`, `z`, `w` stored directly in the 16-byte `TValue`; non-GC). Covers all finite floats, ±0, ±Inf, subnormals, positive NaNs, and negative NaNs up to `0xffffffdfu`. |
| `0xffffffe0u .. 0xfffffffeu` | Non-vector Luau tag `t = tt.bits & 0x1fu` (`LUA_TNIL = 0` → `0xffffffe0u`, `LUA_TNUMBER = 3` → `0xffffffe3u`, GC types `t >= LUA_TSTRING` → `>= 0xffffffe5u`). |
| `0xffffffffu` | Escaped 4-wide `float` vector: `value.gc` points to a heap-allocated `LuauVector` GC object holding the exact 4 `float` lanes when `bit_cast<uint32_t>(w) >= 0xffffffe0u`. |

Key properties:

- **Zero widening:** `sizeof(TValue) == 16`, `sizeof(LuaNode) == 32`, `sizeof(LuaTable) == 48`.
- **Single-instruction tag checks:**
  - Inline vector test: `uint32_t(tt.bits) < 0xffffffe0u` (x86-64 `cmp <imm>; jb`, AArch64 `cmn w, #32; b.cc`).
  - Specific non-vector tag test: `tt.bits == (0xffffffe0u | tag)` (x86-64 `cmp <imm>; je`, AArch64 `cmn w, #(0 - encoded); b.eq`).
  - Collectable test (including escaped vectors): `uint32_t(tt.bits) >= (0xffffffe0u | LUA_TSTRING)`.
- **Exact IEEE-754 semantics:** Every 32-bit float bit pattern is preserved in `w`. Finite arithmetic and normal NaNs stay inline; negative NaN `w` payloads with bits `>= 0xffffffe0u` transparently escape to `LuauVector` in `setvvalue` / `CHECK_INLINE_VEC` VM fallback.

## Hash Table Vector Keys

`LuaNode` stays 32 bytes (`TValue val` + `TKey key`, both 16 bytes). Because `luaH_newkey` rejects any vector key containing `NaN`, escaped `LuauVector` values can never be table keys; all vector keys are valid inline float4s stored exclusively in a **lazy 16B/slot inline vector-key sidecar**:

- Tables without vector keys allocate `count * 32` bytes (zero overhead for string/number/object hash tables).
- On the first vector key insertion, the node array allocates/upgrades in-place with an appended `LuauVectorKeySlot[count]` tail (16 bytes per slot), tracked via a single unused upper bit of `LuaTable::marked` (bit 4, leaving `LuaTable::safeenv` untouched).
- `node->key.value.p` points to the slot in the sidecar (non-collectable), and `node->key.extra[0]` caches the full 32-bit vector hash.
- Lookups reject non-matching vector keys with a single 32-bit compare `uint32_t(node->key.extra[0]) == h` before dereferencing the sidecar.
- `LOP_GETTABLE` includes an inline-vector direct fast path (`luaH_getvectorinline`) when `!h->metatable`.

## Native CodeGen (x86-64 and AArch64)

Both x86-64 and AArch64 backends support 4-wide NaN-tagged vectors directly with zero backend exclusions:

- **Raw-tag IR contract:** `LOAD_TAG`, `CHECK_TAG`, `JUMP_EQ_TAG`, `CMP_TAG`, `CMP_ANY`, and GC barriers operate on encoded NaN-tag words directly.
- **4-lane vector operations & builtins:** Native 2/3/4-arg `vector.create` (with SSE2 and AArch64 NEON interpreter `FASTCALL` paths), 4-lane arithmetic/math builtins, buffer `f32x4` loads/stores, and left-to-right 4-lane `DOT_VEC` reduction matching VM floating-point associativity.
- **Parallel SIMD vector packing & register-only `BUILD_VEC` fusion:** `vector.create`, `vector.cross`, `vector.sign`, and `vector.clamp` emit `BUILD_VEC` + `STORE_TVALUE`, sharing a parallel 2-level SIMD packer (`vinsertps` + `vpshufps` on x86-64; `zip1.4s` + `zip1.2d` on AArch64) with `STORE_VECTOR`. `OptimizeConstProp` forwards `BUILD_VEC` directly into same-block vector ops and lane reads (`LOAD_FLOAT`/`EXTRACT_VEC`), allowing dead-store elimination to remove temporary stack stores completely.
- **Zero-STLF tag stores and loads:** x86-64 emits 16-byte `vmovups` for `STORE_TAG(..., LUA_TNIL)` and heap `STORE_SPLIT_TVALUE`; AArch64 uses 8-byte `movn + str x` tag stores and `ldr x [+8] + lsr #32` tag loads across tag loads/stores, truthy/falsy jumps, node checks, `FORGLOOP`, and spills to prevent store-to-load forwarding stalls on Cortex-A76.
- **SafeW / DSE `CHECK_INLINE_VEC` elision:** Whole-function dead-store and intra-block + whole-function IEEE-754 SafeW / SafeAllLanes / SafeScalar fixed-point analysis eliminates redundant `CHECK_INLINE_VEC` guards in closed vector loops while skipping functions without `CHECK_INLINE_VEC` in O(1).

## Build and Test

Opt-in 4-wide NaN-tagged build (16B `TValue`, 32B `LuaNode`, native x86-64 and AArch64 CodeGen enabled):

```bash
cmake -S . -B <build-dir> -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DLUAU_BUILD_TESTS=ON -DLUAU_VECTOR_FOUR_WIDE=ON -DLUAU_T16_NANTAG_VEC4=ON
cmake --build <build-dir> --target Luau.Repl.CLI Luau.Conformance Luau.UnitTest
<build-dir>/Luau.Conformance
<build-dir>/Luau.Conformance --codegen
<build-dir>/Luau.UnitTest
```

Upstream configurations remain unchanged:

- Default 3-wide 16B: omit both flags (`-DLUAU_VECTOR_FOUR_WIDE=OFF`).
- Upstream 4-wide 24B: `-DLUAU_VECTOR_FOUR_WIDE=ON` (with default `-DLUAU_T16_NANTAG_VEC4=OFF`).
- Double vectors: `-DLUAU_VECTOR_DOUBLE=ON` (incompatible with `LUAU_T16_NANTAG_VEC4=ON`).
