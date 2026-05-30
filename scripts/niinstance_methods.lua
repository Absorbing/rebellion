-- Rebellion
--
-- File: niinstance_methods.lua
-- Author: (C) Björn Kalkbrenner <terminar@cyberphoria.org> 2020-2023
-- License: LGPLv3

local App = require 'App'
local niproto = require 'niproto'
local log = require 'log'
local dispatcher = require 'dispatcher'()

local tinsert = table.insert

--=== NIDeviceInstance methods ===--
local _M = {}

-- state handling
function _M:resolveStateFunc(state)
    return self["on" .. state]
end

function _M:switchState(state, ...)
    if not state or state == "" then
        return nil,"Error, state not given"
    end

    local _state = state:upper()

    if not self:resolveStateFunc(_state) then
        return nil, "Error, instance state func for '" .. _state .. "' not found"
    end

    self:setState(_state)
    self:setStateParams({...} or {})
    return true
end

function _M:proceed()
    local state = self:getState()
    if not state then
        return false, "proceed error: no state active"
    end

    local f = self:resolveStateFunc(state)
    if f then
        --print("ok, state function for " .. state .. " defined")
        f(self, table.unpack(self:getStateParams() or {}))
        return true
    else
        return nil, "Error, state function for '" .. state .. "' undefined"
    end
end

-- state methods

--=== general ===
function _M:print()
    print("Name: " .. self:getName() .. " State: " .. self._state)
end

local function notificationPortCallback(self, data, len)
    --log.info(self:getDevice():getName().. ":" .. self:getSerial()  .. "> Serial Notification port callback called")
    if data then
--        log.error("CWD: " .. App.cwd())

        local res, err = niproto.parseEventMessage(data, len, self)
        if not res then
            if err then
                log.error("notificationPortCallback> no result, error: " .. (err or "-"))
            end

            niproto.writedump(self, "I", data, len)

            local dat = niproto.PARSE_INSTANCE_CALLBACK_DATA(data,len)
            if dat then
                log.info(self:getDevice():getName() .. ":" .. self:getSerial() .. "> result")

                if not self:getMainCalled() == true then
                    log.info("Calling main initialization")
                    self:switchState("main")
                end
            end

            return
        end

      log.info("Calling event handling code")
      local eq = self:getEventQueue() or {}

      -- Coalesce PAD_DATA: replace existing entry for same pad+state
      -- so the queue stays small (prevents backlog from pressure stream)
      local dominated = false
      if res.name == "PAD_DATA" and res.data then
          for i = #eq, 1, -1 do
              if eq[i].name == "PAD_DATA" and eq[i].data
                 and eq[i].data.padid == res.data.padid
                 and eq[i].data.state == res.data.state then
                  eq[i] = res
                  dominated = true
                  break
              end
          end
      end
      if not dominated then
          table.insert(eq, res)
      end

      self:setEventQueue(eq)
      self:switchState("event")
    end
end

--=== state methods ===
function _M:onINIT()
    log.info(self:getDevice():getName() .. ":" .. self:getSerial() .. "> onInit")

    --reset session variables
    self:setReqPortName()
    self:setNotifPortName()
    self:setNotifPort()
    self:setReqPort()
    self:setControlData {}

    local bport = niproto.openPort(self:getDevice():getBootstrapPort())
    if not bport then
        log.error(self:getDevice():getName() .. "> bootstrap port not opened")
        self:switchState("error")
        return
    end

    local reqportname, notifportname = niproto.PARSE_SERIAL_CONNECT_RESULT(
        bport:send( niproto.MSG_SERIAL_CONNECT(self:getDevice():getId(), self:getSerial() ) )
    )
    bport:close()

    --- connection ---
    if not reqportname or not notifportname then
        log.error(self:getDevice():getName() .. ":" .. self:getSerial() ..
            "> Error getting request or notification port")
        self:switchState("error")
        return
    end

    log.info(self:getDevice():getName() .. ":" .. self:getSerial() ..
        "> Serial-Connect result: ", reqportname, notifportname)
    self:setReqPortName(reqportname)
    self:setNotifPortName(notifportname)

    --notification port creation
    local notifport = niproto.createPort(notifportname, function(data, len)
        notificationPortCallback(self, data, len)
    end, 10)

    if not notifport then
        log.error(self:getDevice():getName() .. ":" .. self:getSerial() ..
            "> Error creating notification port")
        self:switchState("error")
        return
    end
    self:setNotifPort(notifport)

    local reqport = niproto.openPort(reqportname, 10)
    if not reqport then
        log.error((self:getName() or "unknown") .. "> Error opening request port")
        self:switchState("error")
        return
    end
    self:setReqPort(reqport)

    log.info(self:getDevice():getName() .. ":" .. self:getSerial() ..
        "> Sending serial notification port name: ", notifportname)
    local res = niproto.PARSE_ACK_NOT_PORT_RESULT(
        reqport:push(niproto.MSG_ACK_NOT_PORT(notifportname))
    )
    log.info(self:getDevice():getName() .. ":" .. self:getSerial() ..
        "> reqport MSG_ACK_NOT_PORT result: ", res)


    self:switchState("loop")
end

function _M:onRESET()
    log.info(self:getDevice():getName() .. ":" .. self:getSerial() .. "> onReset")
    self:getDevice():removeInstance(self:getSerial())
end

function _M:onMAIN(...) --luacheck: no unused
    self:setMainCalled(true)
    log.info(self:getDevice():getName() .. ":" .. self:getSerial() .. " main")
    local reqport = self:getReqPort()

    --TODO: may block forever and will never reach first setState("loop") - mainrun will block forever

    --we need to use :push() (which only writes data but doesn't try to read) here on windows,
    --otherwise we will get a PIPE_BUSY error
    --on macOS it's just the same platform command which is used for :send()
    local res = niproto.PARSE_KEYCOUNT_RESULT(
        --push
        reqport:push(niproto.MSG_KEYCOUNT())
    )
    log.info(self:getDevice():getName() .. ":" .. self:getSerial() .. "> reqport PARSE_KEYCOUNT_RESULT result: ", res)

    local res = niproto.PARSE_RKEYCOUNT_RESULT(
        --push
        reqport:push(niproto.MSG_RKEYCOUNT())
    )
    log.info(self:getDevice():getName() .. ":" .. self:getSerial() .. "> reqport PARSE_RKEYCOUNT_RESULT result: ", res)

    local res = niproto.PARSE_GETSERIAL_RESULT(
        --push
        reqport:push(niproto.MSG_GETSERIAL())
    )
    log.info(self:getDevice():getName() .. ":" .. self:getSerial() .. "> reqport PARSE_GETSERIAL_RESULT result: ", res)

    --[[
    local res = niproto.PARSE_NEWPROJECT_RESULT(
        reqport:push(niproto.MSG_NEWPROJECT())
    )
    log.info(self:getDevice():getName() .. ":" .. self:getSerial() .. "> reqport PARSE_NEWPROJECT_RESULT result: ", 
            niproto._hexdump(res))
    --]]
    local res = niproto.PARSE_PROJECTNAME_RESULT(
        --push
        reqport:push(niproto.MSG_PROJECTNAME("Rebellion - " .. os.date("%c")))
    )
    log.info(self:getDevice():getName() .. ":" .. self:getSerial() .. "> reqport PARSE_PROJECTNAME_RESULT result: ", res)


    local res = niproto.PARSE_MGD_RESULT(
        --push
        reqport:push(niproto.MSG_MGD(self:getDevice():getId()))
    )
    log.info(self:getDevice():getName() .. ":" .. self:getSerial() .. "> reqport PARSE_MGD_RESULT result: ", res)


    local res = niproto.PARSE_TRTS_RESULT(
        --push
        reqport:push(niproto.MSG_TRTS())
    )
    log.info(self:getDevice():getName() .. ":" .. self:getSerial() .. "> reqport PARSE_TRTS_RESULT result: ", res)

    local res = niproto.PARSE_TSI_RESULT(
        --push
        reqport:push(niproto.MSG_TSI())
    )
    log.info(self:getDevice():getName() .. ":" .. self:getSerial() .. "> reqport PARSE_TSI_RESULT result: ", res)

    local res = niproto.PARSE_FGC_RESULT(
        --push
        reqport:push(niproto.MSG_FGC())
    )
    log.info(self:getDevice():getName() .. ":" .. self:getSerial() .. "> reqport PARSE_FGV_RESULT result: ", res)

    local res = niproto.PARSE_TSI_TRUE_RESULT(
        --push
        reqport:push(niproto.MSG_TSI_TRUE())
    )
    log.info(self:getDevice():getName() .. ":" .. self:getSerial() .. "> reqport PARSE_TSI_TRUE_RESULT result: ", res)

    local res = niproto.PARSE_RD_RESULT(
        --push
        reqport:push(niproto.MSG_RD())
    )
    log.info(self:getDevice():getName() .. ":" .. self:getSerial() .. "> reqport PARSE_RD_RESULT result: ", res)

    local res = niproto.PARSE_AD_RESULT(
        --push
        reqport:push(niproto.MSG_AD())
    )
    log.info(self:getDevice():getName() .. ":" .. self:getSerial() .. "> reqport PARSE_AD_RESULT result: ", res)

    --TODO: temp hack here: we don't use this on KKM because it would block endless
    if self:getDevice():getId() ~= #niproto.CONST_DEVICES.KKM then

        local res = niproto.PARSE_DSD_RESULT(
            --push
            reqport:push(niproto.MSG_DSD_1())
        )
        log.info(self:getDevice():getName() .. ":" .. self:getSerial() .. "> reqport PARSE_DSD_RESULT result: ", res)

        local res = niproto.PARSE_DSD_RESULT(
            --push
            reqport:push(niproto.MSG_DSD_2())
        )
        log.info(self:getDevice():getName() .. ":" .. self:getSerial() .. "> reqport PARSE_DSD_RESULT result: ", res)
    end

    log.warn(self:getDevice():getName() .. ":" .. self:getSerial() .. "> ======== Switching state to 'loop' =========")
    self:switchState("loop")
end



function _M:onEVENT()
    local events = self:getEventQueue() or {}
    self:setEventQueue({})

    for _, event in ipairs(events) do
        event.device = self:getDevice():getName()
        event.serial = self:getSerial()
        event.self = self
        print("niinstance_methods::onEVENT: dispatching event: ", event.name)
        dispatcher:dispatch(event.name, event)
    end

    self:switchState("loop")
end

function _M:onLOOP(...)
    local notifport = self:getNotifPort()
    notifport:loop(0.005)
    --assert(self:switchState("halt", "no error"))
    --log.debug(self:getDevice():getName() .. ":" .. self:getSerial() .. " loop")
    --App.sleep(0.1)
end

function _M:onERROR(...)
    log.info("onError called: ", table.unpack({...}))

    local errcnt = self:getErrorCount() or 0
    errcnt = errcnt + 1

    if errcnt > 3 then
        log.error( (self:getName() or "unknown") .. "> Max error count of 3 reached. Bailing out")
        os.exit(1)
    end
    self:setErrorCount(errcnt)

    self:switchState("init")
end

function _M:onHALT()
    log.info(self:getDevice():getName() .. ":" .. self:getSerial() .. "> HALT")
    App.sleep(999999999)
end

--=== internal helper for control functions ===


local function _display_cmd_header(data, display)
    local _display = display or 0
    --==================================================
    --header p1
    tinsert(data,0x84)
    tinsert(data,0x00)
    tinsert(data,_display) --display number?
    tinsert(data,0x60)
    tinsert(data,0x00)
    tinsert(data,0x00)
    tinsert(data,0x00)
    tinsert(data,0x00)

    --header p2
    tinsert(data,0) --x-msb
    tinsert(data,0) --x-lsb
    tinsert(data,0) --y-msb
    tinsert(data,0) --y-lsb
    --TODO: remove hardcoded display data
    tinsert(data,0x01) --w-msb --480
    tinsert(data,0xe0) --w-lsb
    tinsert(data,0x01) --h-msb --272
    tinsert(data,0x10) --h-lsb
    --==================================================
end


local function _display_cmd_transmit_pixels(data, pixels)
    if #pixels % 2  ~= 0 then
        return nil, "Error, can't send odd number of pixel"
    end
    if #pixels == 0 then 
        return nil, "Error, no pixels given"
    end
    local cnt = #pixels / 2

    tinsert(data, 0x0)
    tinsert(data, 0x0)
    --TODO: fix cnt to 24bit - https://github.com/Drachenkaetzchen/cabl/blob/develop/doc/hardware/maschine-mk3/MaschineMK3-Display.md
    --24 bit integer MSB in Parameter 1, LSB in Parameter 3

    tinsert(data, (cnt & ~0xff) >> 8)
    tinsert(data, cnt & 0xff)

    for _,pixel in ipairs(pixels) do
        --swap
        tinsert(data, (pixel & ~0xff) >> 8)
        tinsert(data, pixel & 0xff)
    end

end

local function _display_cmd_blit(data)
    --blit?
    tinsert(data, 0x03)
    tinsert(data, 0x00)
    tinsert(data, 0x00)
    tinsert(data, 0x00)
end

local function _display_cmd_end(data, display)
    --end of data (commit). Byte 3 carries the display index: the device's DSD
    -- setup shows display N's end command is 0x40 0x00 0xNN 0x00. Hardcoding 0x00
    -- here made every commit target display 0, so display 1 never updated.
    local _display = display or 0
    tinsert(data, 0x40)
    tinsert(data, 0x00)
    tinsert(data, _display)
    tinsert(data, 0x00)
end

--speed-up function for same pixels
local function _display_cmd_repeat_pixels(data, count, pixel1, pixel2)
    --
    tinsert(data, 0x01)
    tinsert(data, 0x00)
    tinsert(data, (count & ~0xff) >> 8)
    tinsert(data, count & 0xff)

    tinsert(data, (pixel1 & ~0xff) >> 8)
    tinsert(data, pixel1 & 0xff)
    tinsert(data, (pixel2 & ~0xff) >> 8)
    tinsert(data, pixel2 & 0xff)
end

--[[
 int rgb565 = ...; // 16 bit value with rrrrrggggggbbbbb

  double r = ((rgb565 >> 11) & 0x1F) / 31.0; // red   0.0 .. 1.0
  double g = ((rgb565 >> 5) & 0x3F) / 63.0;  // green 0.0 .. 1.0
  double b = (rgb565 & 0x1F) / 31.0;         // blue  0.0 .. 1.0

  double cmax = max(r, max(g, b));
  double cmin = min(r, min(g, b));
  double delta = cmax - cmin;

  // hue (in °)
  double h_degrees = delta == 0.0 ? 0.0
                     : cmax == r ? 60 * (((g - b) / delta) % 6)
                     : cmax == g ? 60 * (((b - r) / delta + 2)
                     : /* cmax == b ? */ 60 * (((r - g) / delta + 4);

  // saturation
  double s = delta == 0.0 ? 0.0 : delta / (1.0 - abs(cmax + cmin - 1));

  // lightness
  double l = (cmax + cmin)/2;

RGB565 is a 16 packing of red-green-blue. The above is the RGB565 to HSL conversion.
With Hue in degrees 0° to 360°
    The red/green/blue components are extracted with bit shifting >> and then scaling to 0.0 - 1.0.
    The resulting lightness is an imperfect average, namely the average of minimal and maximal color component value.
    The hue, coloredness, is an angle in a color circle divided in the three RGB colors.
    The saturation, gray tendency, is as defined determined by smaller delta.
----------
/* some RGB color definitions                                                 */
#define Black           0x0000      /*   0,   0,   0 */
#define Navy            0x000F      /*   0,   0, 128 */
#define DarkGreen       0x03E0      /*   0, 128,   0 */
#define DarkCyan        0x03EF      /*   0, 128, 128 */
#define Maroon          0x7800      /* 128,   0,   0 */
#define Purple          0x780F      /* 128,   0, 128 */
#define Olive           0x7BE0      /* 128, 128,   0 */
#define LightGrey       0xC618      /* 192, 192, 192 */
#define DarkGrey        0x7BEF      /* 128, 128, 128 */
#define Blue            0x001F      /*   0,   0, 255 */
#define Green           0x07E0      /*   0, 255,   0 */
#define Cyan            0x07FF      /*   0, 255, 255 */
#define Red             0xF800      /* 255,   0,   0 */
#define Magenta         0xF81F      /* 255,   0, 255 */
#define Yellow          0xFFE0      /* 255, 255,   0 */
#define White           0xFFFF      /* 255, 255, 255 */
#define Orange          0xFD20      /* 255, 165,   0 */
#define GreenYellow     0xAFE5      /* 173, 255,  47 */
#define Pink                        0xF81F

#define Red             0xF800      /* 255,   0,   0 */  
#define Magenta         0xF81F      /* 255,   0, 255 */
#define Yellow          0xFFE0      /* 255, 255,   0 */

F800 has 5 MSB bits set and FFE0 has 5 LSB not set. 0xF81F has obviously both 5 LSB's and 5 MSB's set, which proves the format to be RGB565.

The formula to convert a value 173 to Red is not as straightforward as it may look -- you can't simply drop the 3 least significant bits, but have to linearly interpolate to make 255 to correspond to 31 (or green 255 to correspond to 63).

NewValue = (31 * old_value) / 255;

(And this is still just a truncating division -- proper rounding could be needed)

With proper rounding and scaling:

Uint16_value = (((31*(red+4))/255)<<11) | 
               (((63*(green+2))/255)<<5) | 
               ((31*(blue+4))/255);
---------
uint16_t rgb565_from_triplet(uint8_t red, uint8_t green, uint8_t blue)
{
  red   >>= 3;
  green >>= 2;
  blue  >>= 3;
  return (red << 11) | (green << 5) | blue;
}
--------------
uint16 color = ((red>>3)<<11) | ((green>>2)<<5) | (blue>>3);
--------------
uint16_t getColor(uint8_t red, uint8_t green, uint8_t blue)
{
  red   >>= 3;
  green >>= 2;
  blue  >>= 3;
  return (red << 11) | (green << 5) | blue;
}
----------
color_16_bit = (red << 11) + (green << 5) + blue
where red and blue has a range of 0…31 and green has a range of 0…63, the maximum value for red/green/blue refers to 100% color intensity. Shifting red left by 11, shifting green left by 5 and adding the shifted red and green value and blue together delivers the 16-bit color value.
--]]

--[[
--used in _getR565Color
local function _colortobytes(color)
    return ((color & ~0xff) >> 8), (color & 0xff)
end
--]]

--=== control functions ===

function _M:sendLedData(data)
    local reqport = self:getReqPort()
    local res = niproto.PARSE_LED_RESULT(
        --push
        reqport:push(niproto.MSG_LED(data))
    )
    log.info(self:getDevice():getName() .. ":" .. self:getSerial() .. "> reqport PARSE_LED_RESULT result: ", res)

end

function _M:sendDataToDisplay(display, data)
    local _data = {}

    local warr = {}
    if type(data) == "table" and type(data[1]) == "table" then
        local devid = self:getDevice():getId()
        local height = niproto.CONST_DEVICES[devid].dheight
        local width = niproto.CONST_DEVICES[devid].dwidth
        --generate pixel stream
        for y = 1, height do
            for x = 1, width do
                tinsert(warr, data[y][x])
            end
        end
    else
        warr = data
    end

    _display_cmd_header(_data, display)

    local ret, err = _display_cmd_transmit_pixels(_data, warr )

--[[
    for i=1,272,2 do
        if not clear then
            _display_cmd_repeat_pixels(data, 480/2, _display_colors.WHITE, _display_colors.WHITE)
        else
            _display_cmd_repeat_pixels(data, 480/2, _display_colors.BLACK, _display_colors.BLACK)
        end

        if not clear then
            _display_cmd_repeat_pixels(data, 480/2, _display_colors.RED, _display_colors.RED)
        else
            _display_cmd_repeat_pixels(data, 480/2, _display_colors.BLACK, _display_colors.BLACK)
        end
    end
--]]
    _display_cmd_blit(_data)
    _display_cmd_end(_data, display)

    local reqport = self:getReqPort()
    local res = niproto.PARSE_DISPLAY_RESULT(
        --push
        reqport:push(niproto.MSG_DISPLAY(display, _data))
    )
    --log.info(self:getDevice():getName() .. ":" .. self:getSerial() .. "> reqport PARSE_DISPLAY_RESULT result: ", res)
end



return _M
