-- This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
-- Nonzero fourth lanes expose payload/tag overlap and incomplete TValue copies.
local function arithmetic(a: vector, b: vector)
    assert(is_native_if_supported())
    return a + b, a - b, a * b, a / b, vector.dot(a, b), vector.normalize(a)
end

local a = vector.create(0, 0, 0, 6)
local b = vector.create(1, 2, 3, 2)
local sum, difference, product, quotient, dot, unit = arithmetic(a, b)
assert(sum == vector.create(1, 2, 3, 8))
assert(difference == vector.create(-1, -2, -3, 4))
assert(product == vector.create(0, 0, 0, 12))
assert(quotient == vector.create(0, 0, 0, 3))
assert(dot == 12)
assert(unit == vector.create(0, 0, 0, 1))
assert(sum.W == 8 and sum.w == 8)

local function maps(v: vector)
    assert(is_native_if_supported())
    return vector.floor(v), vector.ceil(v), vector.abs(v), vector.sign(v),
        vector.min(v, vector.create(0, 0, 0, 0)), vector.max(v, vector.create(0, 0, 0, 0)),
        vector.clamp(v, vector.create(-1, -1, -1, -1), vector.create(1, 1, 1, 1)),
        vector.lerp(v, -v, 0.25), vector.cross(v, vector.create(1, 0, 0, 99))
end
local f, c, abs, sign, lo, hi, clamp, lerp, cross = maps(vector.create(0, 0, 0, -2.5))
assert(f.w == -3 and c.w == -2 and abs.w == 2.5 and sign.w == -1)
assert(lo.w == -2.5 and hi.w == 0 and clamp.w == -1 and lerp.w == -1.25 and cross.w == 0)

local function passthrough(...)
    assert(is_native_if_supported())
    return ...
end

local function copy(v)
    assert(is_native_if_supported())
    local array = {v}
    local hash = {payload = v}
    return array[1], table.clone(hash).payload
end
for _, value in {sum, {}} do
    local array, field = copy(value)
    assert(array == value and field == value)
end
local array, field = copy(vector.create(-0.0, math.huge, -math.huge, 0 / 0))
for _, special in {array, field} do
    assert(1 / special.x == -math.huge and special.y == math.huge and special.z == -math.huge)
    assert(special.w ~= special.w)
end
local zero, clonedZero = copy(vector.create(1, 2, 3, -0.0))
assert(1 / zero.w == -math.huge and 1 / clonedZero.w == -math.huge)

local ok, first, second = pcall(passthrough, sum, b)
assert(ok and first == sum and second == b)

local keys = {[sum] = a, [vector.create(1, 2, 3, 9)] = b}
assert(keys[vector.create(1, 2, 3, 8)] == a)
assert(keys[vector.create(1, 2, 3, 9)] == b)

local co = coroutine.create(function(v)
    local resumed = coroutine.yield(v)
    return passthrough(resumed, v)
end)
local ok, yielded = coroutine.resume(co, sum)
assert(ok and yielded == sum)
local resumed, first, second = coroutine.resume(co, b)
assert(resumed and first == b and second == sum)

-- Reduction order matters when large components cancel.
local function dot4(a: vector, b: vector)
    assert(is_native_if_supported())
    return vector.dot(a, b)
end
assert(dot4(vector.create(1e20, 0, -1e20, 1), vector.create(1, 1, 1, 1)) == 1)

local function scalar(v: vector, n: number)
    assert(is_native_if_supported())
    return v * n, n * v, v / n, -v, v == vector.create(1, 2, 3, 4), v == vector.create(1, 2, 3, 5)
end
local left, right, divided, negated, equal, different = scalar(vector.create(1, 2, 3, 4), 2)
assert(left == vector.create(2, 4, 6, 8) and right == left)
assert(divided == vector.create(0.5, 1, 1.5, 2) and negated == vector.create(-1, -2, -3, -4))
assert(equal and not different)

local function constructor(x, y, z, w)
    assert(is_native_if_supported())
    return vector.create(x, y, z, w, "ignored"), vector.create(x, y), vector.create(x, y, z)
end
local full, two, three = constructor(1, 2, 3, 4)
assert(full == vector.create(1, 2, 3, 4) and two == vector.create(1, 2, 0, 0) and three == vector.create(1, 2, 3, 0))
assert(not pcall(constructor, 1, 2, 3, "invalid fourth component"))

-- Selection must preserve both payload and tag when the destination aliases either input.
local function selectValues(x, y)
    assert(is_native_if_supported())
    local both, either = x and y, x or y
    x = x or y
    y = both and y
    return both, either, x, y
end
local both, either, overwrittenX, overwrittenY = selectValues(nil, b)
assert(both == nil and either == b and overwrittenX == b and overwrittenY == nil)
both, either, overwrittenX, overwrittenY = selectValues(false, b)
assert(both == false and either == b and overwrittenX == b and overwrittenY == false)
both, either, overwrittenX, overwrittenY = selectValues(a, false)
assert(both == false and either == a and overwrittenX == a and overwrittenY == false)
both, either, overwrittenX, overwrittenY = selectValues(a, b)
assert(both == b and either == a and overwrittenX == a and overwrittenY == b)

-- Component forwarding must retain float rounding and invalidate earlier W reads.
local function replaceVector(x: number, w: number)
    assert(is_native_if_supported())
    local v = vector.create(x, 2, 3, w)
    local oldX, oldW, whole = v.x, v.w, v
    v = vector.create(w, 5, 6, x)
    return oldX, oldW, whole, v.x, v.w
end
-- Runtime inputs keep bytecode constant folding out of the forwarding check.
local x, w = unpack({16777217, -0.0})
local rounded = vector.create(x, 0, 0, w)
local oldX, oldW, whole, newX, newW = replaceVector(x, w)
assert(oldX == rounded.x and newW == rounded.x and whole.x == rounded.x)
assert(1 / oldW == -math.huge and 1 / newX == -math.huge and 1 / whole.w == -math.huge)

-- Open upvalues can move when a call grows the stack, and become closed on return.
local function openCapture(value)
    local function set(nextValue) value = nextValue end
    local function readAcrossCall(nextValue)
        assert(is_native_if_supported())
        local before = value
        passthrough(unpack(table.create(400, nextValue)))
        set(nextValue)
        collectgarbage("collect")
        return before, value
    end
    local before, after = readAcrossCall(b)
    assert(before == a)
    assert(after == b)
    return readAcrossCall
end
local readClosed = openCapture(a)
local before, after = readClosed("closed")
assert(before == b and after == "closed")
before, after = readClosed(sum)
assert(before == "closed" and after == sum)

-- Large multret ranges exercise TValue pointer differences and copying loops.
local values = table.create(700, vector.create(1, 2, 3, 17))
values[512], values[699] = false, nil
local function many(...)
    assert(is_native_if_supported())
    assert(select("#", ...) == 700)
    return ...
end
local packed = table.pack(many(unpack(values, 1, 700)))
assert(packed.n == 700)
for i = 1, 700 do
    assert(packed[i] == values[i])
end
assert(select("#", passthrough()) == 0)

-- Native buffer.readvectorf32x4 / writevectorf32x4 (including special floats, forwarding, and OOB fallback).
if buffer.readvectorf32x4 then
    local function bufferVec4Roundtrip(buf: buffer, offset: number, v: vector)
        assert(is_native_if_supported())
        buffer.writevectorf32x4(buf, offset, v)
        local r = buffer.readvectorf32x4(buf, offset)
        buffer.writevectorf32x4(buf, 0, v)
        local forwarded = buffer.readvectorf32x4(buf, 0)
        return r, forwarded
    end
    local vbuf = buffer.create(32)
    local vorig = vector.create(1.5, -2.25, 3.75, -0.0)
    local vread, vfwd = bufferVec4Roundtrip(vbuf, 16, vorig)
    assert(vread == vorig and vfwd == vorig)
    assert(1 / vread.w == -math.huge and 1 / vfwd.w == -math.huge)
    local vspec = vector.create(math.huge, -math.huge, 0 / 0, 42.5)
    local vr2, vf2 = bufferVec4Roundtrip(vbuf, 16, vspec)
    assert(vr2.x == math.huge and vr2.y == -math.huge and vr2.z ~= vr2.z and vr2.w == 42.5)
    assert(vf2.x == math.huge and vf2.y == -math.huge and vf2.z ~= vf2.z and vf2.w == 42.5)
    assert(not pcall(bufferVec4Roundtrip, vbuf, 20, vorig))
end

-- Constant-RHS ANDK / ORK and dynamic GC-carrying and/or selections.
local function selectWithConstants(x)
    assert(is_native_if_supported())
    local a = x or 42
    local b = x and "ok"
    return a, b
end
local sa, sb = selectWithConstants(nil)
assert(sa == 42 and sb == nil)
sa, sb = selectWithConstants(false)
assert(sa == 42 and sb == false)
sa, sb = selectWithConstants(true)
assert(sa == true and sb == "ok")
sa, sb = selectWithConstants(sum)
assert(sa == sum and sb == "ok")

-- Dynamic SET_UPVALUE GC barrier must keep newly allocated objects alive inside an old closed upvalue.
local function makeBox()
    local cell: any = nil
    local function put(v)
        assert(is_native_if_supported())
        local tmp = v
        cell = tmp
        tmp = 0
        return tmp
    end
    local function get()
        assert(is_native_if_supported())
        return cell
    end
    return put, get
end
local putCell, getCell = makeBox()
collectgarbage("collect")
for i = 1, 200 do
    local fresh = {tag = i, vec = vector.create(i, 2, 3, i * 4)}
    putCell(fresh)
    collectgarbage("step", 5)
    local cur = getCell()
    assert(cur.tag == i and cur.vec.w == i * 4)
end

-- High-register (> 170) frexp and modf exercise A64 4-byte offset limits on 24-byte frames.
local function highRegBuiltins(x: number)
    assert(is_native_if_supported())
    local r000, r001, r002, r003, r004, r005, r006, r007, r008, r009 = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
    local r010, r011, r012, r013, r014, r015, r016, r017, r018, r019 = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
    local r020, r021, r022, r023, r024, r025, r026, r027, r028, r029 = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
    local r030, r031, r032, r033, r034, r035, r036, r037, r038, r039 = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
    local r040, r041, r042, r043, r044, r045, r046, r047, r048, r049 = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
    local r050, r051, r052, r053, r054, r055, r056, r057, r058, r059 = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
    local r060, r061, r062, r063, r064, r065, r066, r067, r068, r069 = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
    local r070, r071, r072, r073, r074, r075, r076, r077, r078, r079 = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
    local r080, r081, r082, r083, r084, r085, r086, r087, r088, r089 = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
    local r090, r091, r092, r093, r094, r095, r096, r097, r098, r099 = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
    local r100, r101, r102, r103, r104, r105, r106, r107, r108, r109 = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
    local r110, r111, r112, r113, r114, r115, r116, r117, r118, r119 = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
    local r120, r121, r122, r123, r124, r125, r126, r127, r128, r129 = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
    local r130, r131, r132, r133, r134, r135, r136, r137, r138, r139 = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
    local r140, r141, r142, r143, r144, r145, r146, r147, r148, r149 = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
    local r150, r151, r152, r153, r154, r155, r156, r157, r158, r159 = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
    local r160, r161, r162, r163, r164, r165, r166, r167, r168, r169 = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
    local r170, r171, r172, r173, r174, r175, r176, r177, r178, r179 = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
    local m, e = math.frexp(x)
    local ip, fp = math.modf(x)
    local keep = r000 + r010 + r020 + r030 + r040 + r050 + r060 + r070 + r080 + r090
        + r100 + r110 + r120 + r130 + r140 + r150 + r160 + r170
        + r009 + r019 + r029 + r039 + r049 + r059 + r069 + r079 + r089 + r099
        + r109 + r119 + r129 + r139 + r149 + r159 + r169 + r179
    return m, e, ip, fp, keep
end
local fm, fe, mip, mfp, keep = highRegBuiltins(6.25)
assert(fm == 0.78125 and fe == 3 and mip == 6 and mfp == 0.25 and keep == 198)

return "OK"
