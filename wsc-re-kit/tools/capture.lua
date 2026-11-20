-- Minimal Mesen2 WonderSwan coverage capture. The Python runner prepends CONFIG.
local coverage, held, finished = {}, {}, false
local buttons = {"up", "down", "left", "right", "up2", "down2", "left2", "right2", "start", "a", "b", "sound"}
local function mark(address, flag)
  local abs = emu.convertAddress(address, emu.memType.wsMemory, emu.cpuType.ws)
  if abs and abs.memType == emu.memType.wsPrgRom then
    local off = abs.address - CONFIG.padding
    if off >= 0 then coverage[off] = (coverage[off] or 0) | flag end
  end
end
emu.addMemoryCallback(function(a) mark(a, 1) end, emu.callbackType.exec,
  0x20000, 0xFFFFF, emu.cpuType.ws, emu.memType.wsMemory)
emu.addMemoryCallback(function(a) mark(a, 2) end, emu.callbackType.read,
  0x20000, 0xFFFFF, emu.cpuType.ws, emu.memType.wsMemory)
emu.addEventCallback(function()
  local frame = emu.getState().frameCount + 1
  local latest = -1
  for f in pairs(CONFIG.inputs) do if f <= frame and f > latest then latest = f end end
  held = {}
  if latest >= 0 then for b in CONFIG.inputs[latest]:gmatch("[%w]+") do held[b] = true end end
  local state = {}
  for _, b in ipairs(buttons) do state[b] = held[b] == true end
  emu.setInput(state, 0, 0)
end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  if finished or emu.getState().frameCount + 1 < CONFIG.frames then return end
  finished = true
  local offsets = {}
  for off in pairs(coverage) do offsets[#offsets + 1] = off end
  table.sort(offsets)
  local f = assert(io.open(CONFIG.out .. "/cdl.txt", "w"))
  for _, off in ipairs(offsets) do f:write(string.format("%x %d\n", off, coverage[off])) end
  f:close()
  local ram = {}
  for i = 0, emu.getMemorySize(emu.memType.wsWorkRam) - 1 do
    ram[#ram + 1] = string.char(emu.read(i, emu.memType.wsWorkRam))
  end
  f = assert(io.open(CONFIG.out .. "/ram.bin", "wb")); f:write(table.concat(ram)); f:close()
  emu.stop(0)
end, emu.eventType.endFrame)
