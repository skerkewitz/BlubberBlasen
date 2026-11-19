-- Mesen 2 script for the compiler benchmark (see bench.c): records the master
-- clocks between the start and end markers of every tick and the checksum,
-- writes bench.txt ("tick state clocks hashes") and stops when bench.c is done.
local ok, err = pcall(function()
  local out = io.open("bench.txt", "w")
  local tick, state, t0, clocks, lo, hashes = 0, 0, 0, 0, 0, ""
  local function clock() return emu.getState()["masterClock"] end
  local function since(t)   -- the clock comes as a 32-bit signed number
    local d = clock() - t
    if d < 0 then d = d + 4294967296 end
    return d
  end
  emu.addMemoryCallback(function(addr, value)
    if addr == 0x21FC then
      state = value; t0 = clock()
    elseif addr == 0x21FF then
      clocks = since(t0)
    elseif addr == 0x21FD then
      lo = value
    elseif addr == 0x21FE then
      hashes = hashes .. string.format("%04X", lo + 256 * value)
    elseif addr == 0x21FA then
      out:write(string.format("%d %d %d %s\n", tick, state, clocks, hashes))
      hashes = ""
      tick = tick + 1
    elseif addr == 0x21FB then
      out:close(); emu.stop(0)
    end
  end, emu.callbackType.write, 0x21FA, 0x21FF)
end)
if not ok then local f = io.open("bench.txt", "w"); f:write("lua error: " .. tostring(err) .. "\n"); f:close(); emu.stop(1) end
