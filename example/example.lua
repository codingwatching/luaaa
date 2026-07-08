-- ============================================================================
--  example.lua — the Lua half of "The Little Cat Shelter".
--  Everything below (Cat, shelter, luaaa:extend/base) is provided by C++
--  through luaaa.  Nothing here is hand-written glue.
-- ============================================================================

print("== Welcome to the " .. shelter.city .. " shelter (capacity "
      .. shelter.capacity .. ") ==\n")

-- 1) Create an object of an exported C++ class and call its methods.
local bingo = Cat.new("Bingo")
bingo:meow("Bingo")
bingo:eat{ "fish", "milk", "biscuit" }         -- a Lua array -> std::list

-- 2) Properties read and write like plain fields.
print("name:", bingo.name, " age:", bingo.age)
bingo.name = "Bingo the Brave"
bingo.age  = 3
print(bingo)                                    -- uses __tostring -> "... (3y)"

-- 3) A module function that takes a Lua function as a callback.
print("\n-- adoption --")
shelter.adopt(bingo.name, function(who)
    print("  " .. who .. " found a home! ")
end)

-- 4) A custom C++ type (Color) crossing the boundary as a plain table.
print("\n-- collar colors --")
local red   = { r = 255, g = 0,   b = 0 }
local blue  = { r = 0,   g = 0,   b = 255 }
local mixed = shelter.mix(red, blue)
print(string.format("  red + blue = (%d, %d, %d)", mixed.r, mixed.g, mixed.b))

-- 5) Subclass an exported class in pure Lua.
--    luaaa:extend / luaaa:base are injected automatically once any class is
--    bound — no helper code to paste.
print("\n-- a special cat --")
local SpecialCat = luaaa:extend(Cat, { tricks = 0 })

function SpecialCat:learn()
    self.tricks = self.tricks + 1
    print("  " .. self.name .. " learned a trick (" .. self.tricks .. " total)")
end

function SpecialCat:meow(who)           -- override, then call the base method
    print("  " .. self.name .. " purrs first...")
    luaaa:base(self):meow(who)
end

local felix = SpecialCat:new("Felix")
felix:learn()
felix:learn()
felix:meow("Felix")
print("  " .. tostring(felix) .. " knows " .. felix.tricks .. " tricks")

print("\n== the shelter closes for the day ==")
