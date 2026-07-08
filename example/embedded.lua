-- ============================================================================
--  embedded.lua — the Lua half of "The Feeder Node".
--  Same luaaa API as the desktop story, just the embedded (no-stdlib) subset:
--  C strings, C arrays and a single raw-function-pointer callback.
-- ============================================================================

print("== Feeder firmware " .. device.firmware .. " ==\n")

-- 1) An exported C++ class, driven from Lua.
local f = Feeder.new("F-01")
f.portions = 2                      -- property backed by setPortions()
print("loaded portions:", f.portions)

f:dispense()
f:dispense()
f:dispense()                        -- hopper now empty

-- 2) A method that reads a Lua array straight from the stack (lua_State*).
print("\n-- morning round --")
f:feedAll{ "Bingo", "Felix", "Luna" }

-- 3) A C array exported from C++ shows up as an ordinary Lua table.
print("\n-- schedule --")
for _, h in ipairs(device.hours) do
    print("  scheduled: " .. h .. ":00")
end

-- 4) Hand a Lua function to C++ as a raw callback.
device.runSchedule(function(hour)
    print("  lua: it's " .. hour .. ":00, topping up")
    return 3                        -- pretend we fed 3 cats
end)

f:beep("F-01")                      -- static method — called on an instance
print("\n== node going to sleep ==")
