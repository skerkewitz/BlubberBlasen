-- Mesen 2 script: instructions executed per ROM address during ticks
-- PROFILE_FROM..PROFILE_TO of the benchmark (bench.c), written to profile.txt
-- as "rom offset, count". profile.sh fills in the two numbers.
local ok, err = pcall(function()
  local from, to = PROFILE_FROM, PROFILE_TO
  local tick = 0
  emu.addMemoryCallback(function()
    tick = tick + 1
    if tick == from then emu.resetAccessCounters() end
    if tick == to then
      local c = emu.getAccessCounters(emu.memType.snesPrgRom, emu.counterType.execCount)
      local out = io.open("profile.txt", "w")
      for i = 0, #c do if c[i] and c[i] > 0 then out:write(i, " ", c[i], "\n") end end
      out:close()
      emu.stop(0)
    end
  end, emu.callbackType.write, 0x21FA, 0x21FA)
end)
if not ok then local f = io.open("profile.txt", "w"); f:write("lua error: " .. tostring(err) .. "\n"); f:close(); emu.stop(1) end
