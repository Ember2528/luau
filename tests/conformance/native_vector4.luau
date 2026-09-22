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

return "OK"
