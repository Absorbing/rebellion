-- Rebellion
--
-- File: rpc.lua
-- Author: (C) Björn Kalkbrenner <terminar@cyberphoria.org> 2020-2023
-- License: LGPLv3

local niproto = require 'niproto'
local log = require 'log'
local json = require 'cjson'
local RebellionTypes = require "RebellionTypes"
local App = require "App"

local rpcfuncs = {}

local function addRpc(name)
    return function(func)
        rpcfuncs[name] = func
    end
end

addRpc 'rebellion.activateDevice' (function(name)
    log.error("DUMMY")
    return -1
end)

addRpc 'rebellion.getDevices' (function()
    local devices = niproto.CONST_DEVICES
    local res = {}
    for k,v in pairs(devices) do
        table.insert(res,k)
    end
    return res
end)

addRpc 'rebellion.getInstances' (function()
    local instances = {}
    for iname, instance in pairs(require 'nidevices':getInstances()) do
        table.insert(instances, {
            name = iname,
            device = instance:getDevice():getName()
        })
    end
    return instances
end)


addRpc 'rebellion.sendDataToDisplay' (function(serial, display, data)
    local instances = require 'nidevices':getInstances{ serial = serial } or {}
    local _,self = next(instances)

    if not self then
        return nil, "no instance found"
    end
    if not data then
        return nil, "no display data given"
    end

    local warr = {}
    if type(data) == "table" and type(data[1]) == "table" then
        local devid = self:getDevice():getId()
        local height = niproto.CONST_DEVICES[devid].dheight
        local width = niproto.CONST_DEVICES[devid].dwidth
        if not height or not width then
            return nil, "No height or width given"
        end

        local dat = data
        --[[
        --create display data table, take the data
       local dat = {}
        for y = 1,height do
            for x = 1,width do
                if not dat[y] then dat[y] = {} end

                --set black if empty
                dat[y][x] = (data[y] and data[y][x]) or niproto.CONST_DISPLAY_COLORS.BLACK
            end
        end
        --]]

        --generate pixel stream
        local tinsert = table.insert
        for y = 1, height do
            for x = 1, width do
                tinsert(warr, dat[y][x])
            end
        end

    else
        warr = data
    end
--[[
    local img = _loadPngImage("rebellion-480x272.png")
    for y = 1, height do
        for x=1, width do
            local pixel = img:getPixel(x,y)
            dat[y][x] = _getR565Color(pixel.R,pixel.G,pixel.B)
        end
    end
--]]
--[[
        --test: draw diagonal line on display2
        if display == 1 then
            local x = 50
            local y = 50
            local len = 200
            for i=1,len do
                dat[y+i][y+i] = niproto.gwetR565Color(0,0,0)
            end
        end
--]]

    self:sendDataToDisplay(display, warr)
end)

-- Fast path: the caller has already built the full device command stream
-- (header + RLE pixel commands + blit + end) in `data`. We just frame and push
-- it — no per-pixel Lua work, unlike rebellion.sendDataToDisplay above.
addRpc 'rebellion.sendDisplayCmd' (function(serial, display, data)
    local instances = require 'nidevices':getInstances{ serial = serial } or {}
    local _,self = next(instances)

    if not self then
        return nil, "no instance found"
    end
    if not data or #data == 0 then
        return nil, "no display data given"
    end

    local reqport = self:getReqPort()
    niproto.PARSE_DISPLAY_RESULT(
        reqport:push(niproto.MSG_DISPLAY(display, data))
    )
    return true
end)

addRpc 'rebellion.sendLedData' (function(serial, led, color, intense)
    local instances = require 'nidevices':getInstances{ serial = serial } or {}
    local _,self = next(instances)

    if not self then
        return nil, "no instance found"
    end

    local data = self:getLedData()
    if not data then
        data = niproto.initLedData(self:getDevice():getId())
    end

    --[[ pad
    local idx = 88 + niproto._pad_num_to_code(event.data.padid)
    local color = 0
    if event.data.state == "PRESSED" then
        color = event.data.padid
    end

    local cpressure = event.data.cpressure or 1
    local intense = cpressure // 32 --128/4

    niproto.setLedColor(data,idx, color,intense)
    --]]

    if color < 0 then --off
        color=0
    end
    if color > 17 then --17 colors * 4 intensity
        color = 17
    end

    if intense < 0 then
        intense=0
    end
    if intense > 3 then
        intense = 3
    end

    --[[
    for i=1,#data do
        data[i] = 0
    end
    --]]
    niproto.setLedColor(data,led,color,intense)

    self:setLedData(data)
    self:sendLedData(data)

end)

local function rpcReturn(result)
    if type(result) ~= "table" then
        return -1
    end
    --log.debug("rpcReturn:",tostring(result))
    local s = json.encode(result)
    --log.debug("returning data: ", s)
    local mt = result.event and RebellionTypes.REBELLION_MT_EV
                            or RebellionTypes.REBELLION_MT_RES

    log.info("Result: ", s:sub(1,80) .. (s:len() > 80 and "..." or ""))
    App.rpc_callback(
            RebellionTypes.REBELLION_MF_JSON,
            mt,
            s, s:len()
        )
    log.debug("End of lua => rpc")
end

local function rpc(mf, mt, data, len)
    log.debug("RPC called: ", data, len)

    if mf ~= RebellionTypes.REBELLION_MF_JSON then
        log.error("Error, given message format unsupported. Please use REBELLION_MF_JSON")
        os.exit(1)
    end

    local stat, res = pcall(function()
        return json.decode(data)
    end)
    if not stat and res then
        return rpcReturn {
            error = {
                code = -32700,
                message = "Parse error: " .. (res or "<unknown error>")
            }
        }
    end
    local rpc = res
    local result = {
        id = rpc.id
    }
    if not rpc.method then
        result.error = {
            code = -32600,
            message = "Invalid Request: method not given"
        }
        return rpcReturn(result)
    end

    if not rpc.params or rpc.params == json.null then
        result.error = {
            code = -32600,
            message = "Invalid Request: no parameter given"
        }
        return rpcReturn(result)
    end

    if not rpcfuncs[rpc.method] then
        result.error = {
            code = -32601,
            message = "Method not found"
        }
        return rpcReturn(result)
    end

    if type(rpc.params) ~= "table" then
        result.error = {
            code = -32602,
            message = "Invalid params"
        }
        return rpcReturn(result)
    end

    log.info("rpc.method: ",rpc.method or "-")
    log.info("rpc.params: ", table.unpack(rpc.params or {}))
    log.info("rpc.id: ", rpc.id or "-")

    local res, err = rpcfuncs[rpc.method](table.unpack(rpc.params))
    if not res and err then
        log.error("Error calling rpc.method " .. rpc.method .. ": ", err or "-")
        result.error = {
            code = -32603,
            message = "Internal error",
            data = err
        }
        return rpcReturn(result)
    end

    result.result = res
    return rpcReturn( result )
end

-- ── Display RPC (lightweight commands, pixel work stays in Lua) ──

local _rpcDisplayData
local png = require "pnglua.png"

local function _rpcEnsureDisplayData(self)
    if not _rpcDisplayData then
        local devid = self:getDevice():getId()
        local h = niproto.CONST_DEVICES[devid].dheight
        local w = niproto.CONST_DEVICES[devid].dwidth
        _rpcDisplayData = { [0] = {}, [1] = {} }
        for i = 0, 1 do
            for y = 1, h do
                _rpcDisplayData[i][y] = {}
                for x = 1, w do
                    _rpcDisplayData[i][y][x] = 0
                end
            end
        end
    end
    return _rpcDisplayData
end

local function _rpcGetInstance(serial)
    local instances = require 'nidevices':getInstances{ serial = serial } or {}
    local _, self = next(instances)
    return self
end

addRpc 'rebellion.display.showImage' (function(serial, display)
    local self = _rpcGetInstance(serial)
    if not self then return nil, "no instance found" end

    local devid = self:getDevice():getId()
    local h = niproto.CONST_DEVICES[devid].dheight
    local w = niproto.CONST_DEVICES[devid].dwidth
    local dd = _rpcEnsureDisplayData(self)
    local dat = dd[display or 0]

    local img = png("rebellion-480x272.png")
    for y = 1, h do
        for x = 1, w do
            local pixel = img:getPixel(x, y)
            dat[y][x] = niproto.getRGB565Color(pixel.R, pixel.G, pixel.B)
        end
    end

    self:sendDataToDisplay(display or 0, dat)
    return true
end)

addRpc 'rebellion.display.clear' (function(serial, display)
    local self = _rpcGetInstance(serial)
    if not self then return nil, "no instance found" end

    local devid = self:getDevice():getId()
    local h = niproto.CONST_DEVICES[devid].dheight
    local w = niproto.CONST_DEVICES[devid].dwidth
    local dd = _rpcEnsureDisplayData(self)
    local dat = dd[display or 0]

    for y = 1, h do
        for x = 1, w do
            dat[y][x] = 0
        end
    end

    self:sendDataToDisplay(display or 0, dat)
    return true
end)

addRpc 'rebellion.display.fill' (function(serial, display, r, g, b)
    local self = _rpcGetInstance(serial)
    if not self then return nil, "no instance found" end

    local devid = self:getDevice():getId()
    local h = niproto.CONST_DEVICES[devid].dheight
    local w = niproto.CONST_DEVICES[devid].dwidth
    local dd = _rpcEnsureDisplayData(self)
    local dat = dd[display or 0]
    local col = niproto.getRGB565Color(r or 0, g or 0, b or 0)

    for y = 1, h do
        for x = 1, w do
            dat[y][x] = col
        end
    end

    self:sendDataToDisplay(display or 0, dat)
    return true
end)

addRpc 'rebellion.display.fillRect' (function(serial, display, rx, ry, rw, rh, r, g, b)
    local self = _rpcGetInstance(serial)
    if not self then return nil, "no instance found" end

    local devid = self:getDevice():getId()
    local h = niproto.CONST_DEVICES[devid].dheight
    local w = niproto.CONST_DEVICES[devid].dwidth
    local dd = _rpcEnsureDisplayData(self)
    local dat = dd[display or 0]
    local col = niproto.getRGB565Color(r or 0, g or 0, b or 0)

    local x2 = math.min(rx + rw - 1, w)
    local y2 = math.min(ry + rh - 1, h)
    for y = math.max(ry, 1), y2 do
        for x = math.max(rx, 1), x2 do
            dat[y][x] = col
        end
    end

    self:sendDataToDisplay(display or 0, dat)
    return true
end)

-- ── Bitmap font (5x7, stored as 7 row bytes per glyph) ──
local _font = {
    [" "]  = {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ["A"]  = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    ["B"]  = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    ["C"]  = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    ["D"]  = {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
    ["E"]  = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    ["F"]  = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    ["G"]  = {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E},
    ["H"]  = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    ["I"]  = {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    ["K"]  = {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    ["L"]  = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    ["M"]  = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    ["N"]  = {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    ["O"]  = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    ["P"]  = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    ["R"]  = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    ["S"]  = {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
    ["T"]  = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    ["b"]  = {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E},
    ["#"]  = {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A},
    ["0"]  = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    ["1"]  = {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    ["2"]  = {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F},
    ["3"]  = {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
    ["4"]  = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    ["5"]  = {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    ["6"]  = {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    ["7"]  = {0x1F,0x01,0x02,0x04,0x04,0x04,0x04},
    ["8"]  = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    ["9"]  = {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    ["a"]  = {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F},
    ["c"]  = {0x00,0x00,0x0E,0x10,0x10,0x11,0x0E},
    ["d"]  = {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F},
    ["e"]  = {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E},
    ["f"]  = {0x06,0x08,0x08,0x1E,0x08,0x08,0x08},
    ["g"]  = {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E},
    ["h"]  = {0x10,0x10,0x1E,0x11,0x11,0x11,0x11},
    ["i"]  = {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E},
    ["k"]  = {0x10,0x10,0x12,0x14,0x18,0x14,0x12},
    ["l"]  = {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E},
    ["m"]  = {0x00,0x00,0x1A,0x15,0x15,0x15,0x15},
    ["n"]  = {0x00,0x00,0x1E,0x11,0x11,0x11,0x11},
    ["o"]  = {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E},
    ["p"]  = {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10},
    ["r"]  = {0x00,0x00,0x16,0x19,0x10,0x10,0x10},
    ["s"]  = {0x00,0x00,0x0E,0x10,0x0E,0x01,0x1E},
    ["t"]  = {0x08,0x08,0x1E,0x08,0x08,0x08,0x06},
    ["u"]  = {0x00,0x00,0x11,0x11,0x11,0x11,0x0F},
    ["v"]  = {0x00,0x00,0x11,0x11,0x11,0x0A,0x04},
    ["w"]  = {0x00,0x00,0x11,0x11,0x15,0x15,0x0A},
    ["x"]  = {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11},
    ["y"]  = {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E},
    ["z"]  = {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F},
}

addRpc 'rebellion.display.batch' (function(serial, display, commands)
    local self = _rpcGetInstance(serial)
    if not self then return nil, "no instance found" end

    local devid = self:getDevice():getId()
    local h = niproto.CONST_DEVICES[devid].dheight
    local w = niproto.CONST_DEVICES[devid].dwidth
    local dd = _rpcEnsureDisplayData(self)
    local dat = dd[display or 0]

    for _, cmd in ipairs(commands) do
        local op = cmd[1]

        if op == "clear" then
            for y = 1, h do
                for x = 1, w do
                    dat[y][x] = 0
                end
            end

        elseif op == "fill" then
            local col = niproto.getRGB565Color(cmd[2] or 0, cmd[3] or 0, cmd[4] or 0)
            for y = 1, h do
                for x = 1, w do
                    dat[y][x] = col
                end
            end

        elseif op == "fillRect" then
            local rx, ry, rw, rh = cmd[2], cmd[3], cmd[4], cmd[5]
            local col = niproto.getRGB565Color(cmd[6] or 0, cmd[7] or 0, cmd[8] or 0)
            local x2 = math.min(rx + rw - 1, w)
            local y2 = math.min(ry + rh - 1, h)
            for y = math.max(ry, 1), y2 do
                for x = math.max(rx, 1), x2 do
                    dat[y][x] = col
                end
            end

        elseif op == "rect" then
            local rx, ry, rw, rh = cmd[2], cmd[3], cmd[4], cmd[5]
            local col = niproto.getRGB565Color(cmd[6] or 0, cmd[7] or 0, cmd[8] or 0)
            local x2 = math.min(rx + rw - 1, w)
            local y2 = math.min(ry + rh - 1, h)
            for x = math.max(rx, 1), x2 do
                if ry >= 1 and ry <= h then dat[ry][x] = col end
                if y2 >= 1 and y2 <= h then dat[y2][x] = col end
            end
            for y = math.max(ry, 1), y2 do
                if rx >= 1 and rx <= w then dat[y][rx] = col end
                if x2 >= 1 and x2 <= w then dat[y][x2] = col end
            end

        elseif op == "line" then
            local x0, y0, x1, y1 = cmd[2], cmd[3], cmd[4], cmd[5]
            local col = niproto.getRGB565Color(cmd[6] or 0, cmd[7] or 0, cmd[8] or 0)
            local dx = math.abs(x1 - x0)
            local dy = -math.abs(y1 - y0)
            local sx = x0 < x1 and 1 or -1
            local sy = y0 < y1 and 1 or -1
            local err = dx + dy
            while true do
                if x0 >= 1 and x0 <= w and y0 >= 1 and y0 <= h then
                    dat[y0][x0] = col
                end
                if x0 == x1 and y0 == y1 then break end
                local e2 = 2 * err
                if e2 >= dy then err = err + dy; x0 = x0 + sx end
                if e2 <= dx then err = err + dx; y0 = y0 + sy end
            end

        elseif op == "pixel" then
            local px, py = cmd[2], cmd[3]
            if px >= 1 and px <= w and py >= 1 and py <= h then
                dat[py][px] = niproto.getRGB565Color(cmd[4] or 0, cmd[5] or 0, cmd[6] or 0)
            end

        elseif op == "image" then
            local img = png("rebellion-480x272.png")
            for y = 1, h do
                for x = 1, w do
                    local pixel = img:getPixel(x, y)
                    dat[y][x] = niproto.getRGB565Color(pixel.R, pixel.G, pixel.B)
                end
            end
        
elseif op == "circle" then
            local cx, cy, radius = cmd[2], cmd[3], cmd[4]
            local col = niproto.getRGB565Color(cmd[5] or 0, cmd[6] or 0, cmd[7] or 0)
            local ox, oy, err = radius, 0, 1 - radius
            while ox >= oy do
                local pts = {
                    {cx+ox,cy+oy},{cx-ox,cy+oy},{cx+ox,cy-oy},{cx-ox,cy-oy},
                    {cx+oy,cy+ox},{cx-oy,cy+ox},{cx+oy,cy-ox},{cx-oy,cy-ox}
                }
                for _, p in ipairs(pts) do
                    if p[1] >= 1 and p[1] <= w and p[2] >= 1 and p[2] <= h then
                        dat[p[2]][p[1]] = col
                    end
                end
                oy = oy + 1
                if err < 0 then
                    err = err + 2 * oy + 1
                else
                    ox = ox - 1
                    err = err + 2 * (oy - ox) + 1
                end
            end

        elseif op == "fillCircle" then
            local cx, cy, radius = cmd[2], cmd[3], cmd[4]
            local col = niproto.getRGB565Color(cmd[5] or 0, cmd[6] or 0, cmd[7] or 0)
            local ox, oy, err = radius, 0, 1 - radius
            while ox >= oy do
                for _, span in ipairs({
                    {cy+oy, cx-ox, cx+ox}, {cy-oy, cx-ox, cx+ox},
                    {cy+ox, cx-oy, cx+oy}, {cy-ox, cx-oy, cx+oy}
                }) do
                    local sy = span[1]
                    if sy >= 1 and sy <= h then
                        for sx = math.max(span[2], 1), math.min(span[3], w) do
                            dat[sy][sx] = col
                        end
                    end
                end
                oy = oy + 1
                if err < 0 then
                    err = err + 2 * oy + 1
                else
                    ox = ox - 1
                    err = err + 2 * (oy - ox) + 1
                end
            end

        elseif op == "text" then
            local tx, ty, str = cmd[2], cmd[3], cmd[4]
            local col = niproto.getRGB565Color(cmd[5] or 255, cmd[6] or 255, cmd[7] or 255)
            local scale = cmd[8] or 1
            local cx = tx
            for ci = 1, #str do
                local ch = str:sub(ci, ci)
                local glyph = _font[ch]
                if glyph then
                    for row = 0, 6 do
                        local bits = glyph[row + 1]
                        for col_idx = 0, 4 do
                            if bits & (0x10 >> col_idx) ~= 0 then
                                for sy = 0, scale - 1 do
                                    for sx = 0, scale - 1 do
                                        local px = cx + col_idx * scale + sx
                                        local py = ty + row * scale + sy
                                        if px >= 1 and px <= w and py >= 1 and py <= h then
                                            dat[py][px] = col
                                        end
                                    end
                                end
                            end
                        end
                    end
                end
                cx = cx + 6 * scale
            end
        end
    end

    self:sendDataToDisplay(display or 0, dat)
    return true
end)

return rpc