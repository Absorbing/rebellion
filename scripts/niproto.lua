-- Rebellion
--
-- File: niproto.lua
-- Author: (C) Björn Kalkbrenner <terminar@cyberphoria.org> 2020-2023
-- License: LGPLv3

local struct = require 'struct'
local NIIPC = require 'NIIPC'
local App = require 'App'
local log = require 'log'
--local plpretty = require 'pl.pretty'

local spack, sunpack = struct.pack, struct.unpack
local tunpack, tconcat, tinsert, tremove = table.unpack, table.concat, table.insert, table.remove

local CONST_META = {
    __index = function(t,k)
        if rawget(t,k) then
            return rawget(t,k)
        end
        log.error("CONST_META:__index> k:" .. string.format("0x%x",k))
        if type(k) == "number" then
            log.error("Error, unknown CONST index: " .. string.format("0x%x", k))
        else
            log.error("Error, unknown CONST index: " .. k)
        end
    end,
    __newindex = function(t,k,v) --luacheck: ignore unused t v
        log.error("Error, table is not writeable with index: " .. k)
    end
}

-- local function setDefaultTableValue(t,default)
--     return setmetatable(t,{
--         __index = function(t,k)
--             if rawget(t,k) then
--                 return rawget(t,k)
--             else
--                 return default
--             end
--         end
--     })
-- end

local niproto = {
    _structpref = "<!4",
    CONST_NIM2 = 0x4e694d32, --2MiN ? Maschine2
    CONST_NIKK = 0x4e694b4b, --KKiN ? KompleteKontrol
    CONST_PRMY = 0x70726d79, --ymrp ?
    CONST_TRUE = 0x74727565, --eurt ?
    CONST_STRT = 0x73747274, --trtS ?
    CONST_PORT_MAIN = "NIHWMainHandler",
    CONST_PORT_HOST = "com.native-instruments.NIHostIntegrationAgent",
}
niproto.CONST_NISW = niproto.CONST_NIM2

local function importDevices(idevs)
    local devices = {}
    local idxtable = {}

    for _,dev in ipairs(idevs) do
        local ndev = {
            id = dev[1],
            name = dev[2],
            sname = dev[3],
            hname = dev[4],
            port = dev[5],
            ledcnt = dev[6],
            dcnt = dev[7],
            dheight = dev[8],
            dwidth = dev[9]
        }

        ndev = setmetatable(ndev, {
            __len = function()
                return ndev.id
            end,
            --[[
            __bnot = function(t,k) -- ~
                print("T:",t,"K:",k)
                return t
            end,
            __unm = function() -- -
                log.error("UNM MT CALLED")
                return "unm"
            end,
            --]]
            __tostring = function()
                return ndev.hname
            end,
            __name = "CONST_DEVICES." .. ndev.name
        })

        table.insert(devices,ndev)
        idxtable[ndev.id] = ndev
        idxtable[ndev.name] = ndev
        idxtable[ndev.sname] = ndev
    end

    return setmetatable({}, {
        __index = function(_,k)
            if idxtable[k] then
                return idxtable[k]
            end

            if type(k) == "number" then
                if devices[k] then
                    return devices[k]
                end
                log.error("Error, unknown CONST index: " .. string.format("0x%x", k))
            else
                log.error("Error, unknown CONST index: " .. k)
            end
        end,
        __newindex = function(_,k)
            log.error("Error, table is not writeable with index: " .. k)
        end,
        __pairs = function() --luacheck: ignore unused t
            local t = {}
            for _,v in ipairs(devices) do
                t[v.name] = v
            end
            return next, t
        end
    })

end


local function importControls(ditems)
    local devices = {}
    local idxtable = {}

    for _,item in ipairs(ditems) do
        local name, itype = item[1], item[2]
        for i=3,#item do
            local t = item[i]
            if type(t) == "table" then
                local deviceid, id, ledid, ledcolors = table.unpack(t)
                assert(deviceid,"deviceid not set")

                if not devices[deviceid] then
                    devices[deviceid] = {}
                end
                if not idxtable[deviceid] then
                    idxtable[deviceid] = {}
                end

                local nitem = {
                    name = name,
                    type = itype,
                    id = id,
                    ledid = ledid,
                    ledcolors = ledcolors
                }

                nitem = setmetatable(nitem, {
                    __len = function()
                        return nitem.id
                    end,
                    __bnot = function() -- ~
                        return nitem.ledid
                    end,
                    --[[
                    __unm = function() -- -
                        log.error("UNM MT CALLED")
                        return "unm"
                    end,
                    --]]
                    __tostring = function()
                        return nitem.name
                    end,
                    __name = "CONST_CONTROLS." .. nitem.name
                })

                table.insert(devices[deviceid],nitem)
                if name then
                    idxtable[deviceid][name] = nitem
                end
                if id then
                    idxtable[deviceid][id] = nitem
                end

            else
                log.error("Wrong parameter in importItems")
            end
        end
    end

    local function getDeviceMT(deviceid)
        return setmetatable({},{
            __index = function(_,k)
                log.info("Not found, trying to search via __index")
                if idxtable[deviceid][k] then
                    log.info("Found via indextable, returning")
                    return idxtable[deviceid][k]
                end

                log.warn("TODO: -- logical bug here - normally, the idx is used for direct btn id mapping")
--[[
                -- logical bug here - normally, the idx is used for direct btn id mapping
                -- this code tries to also allow indexing from 1 to count() which doesn't make sense; terminar 20220213
                if type(k) == "number" then
                log.info("getDeviceMT:__index> DEV:" .. string.format("0x%x",deviceid) .. " K: " .. k)
                    if devices[deviceid][k] then
                        log.info("Found via devices table, returning")
                        return devices[deviceid][k]
                    end
                    log.error("Error, unknown CONST index: " .. string.format("0x%x", k))
                else
                    log.error("Error, unknown CONST index: " .. k)
                end
--]]
            end,
            __newindex = function(_,k)
                log.error("Error, table is not writeable with index: " .. k)
            end,
            __pairs = function()
                local t = {}
                for _,v in ipairs(devices[deviceid]) do
                    t[v.name] = v
                end
                return next, t
            end
        })
    end

    return setmetatable({}, {
        __index = function(_,k)
            local dev = k
            if not devices[dev] then
                dev = #niproto.CONST_DEVICES[k]
            end

            if dev and devices[dev] then
                return getDeviceMT(dev)
            end
        end,
        __newindex = function(_,k)
            log.error("Error, table is not writeable with index: " .. k)
        end

    })

end



--TODO: wrap file path separator
local mappings_env = get_lua_env("scripts/mappings.lua", {
    niproto = niproto,
    importDevices = importDevices,
    importControls = importControls,
    insert = table.insert,
    string = tostring
})
local CONST_DEVICES = mappings_env.CONST_DEVICES
niproto.CONST_DEVICES = CONST_DEVICES

local CONST_CONTROLS = mappings_env.CONST_CONTROLS
niproto.CONST_CONTROLS = CONST_CONTROLS

--------------------------------------------------------------------------------

niproto.CONST_COLORS = setmetatable({
    RED = 1,
    ORANGE = 2,
    LIGHT_ORANGE = 3,
    WARM_YELLOW = 4,
    YELLOW = 5,
    LIME = 6,
    GREEN = 7,
    MINT = 8,
    CYAN = 9,
    TURQUOISE = 10,
    BLUE = 11,
    PLUM = 12,
    VIOLET = 13,
    PURPLE = 14,
    MAGENTA = 15,
    FUCHSIA = 16,
    WHITE = 17
}, CONST_META)


niproto.CONST_DISPLAY_COLORS = setmetatable({
    WHITE = 65535, --0xffff
    BLACK = 0,
    GREENISH = 4000,
    GRAY = 33000,
    BROWN = 49123,
    BLUE = 11310, --0x1f
    --RED = 1030,
    RED = 49152, --0xf800
    ORANGE = 2058,
    PURPLE = 14394,
    CYAN = 9254,  --0x7ff
    YELLOW = 5142, --0xffe0
    MINT = 8226,
    GREEN = 7198 --0x7e0
}, CONST_META)


niproto.CONST_MESSAGE_IDS = setmetatable({
    DEVICE_STATE_ON = 0x3444e2b,
    DEVICE_STATE_OFF = 0x3444e2d,
    PAD_DATA = 0x3504e00,
    BTN_DATA = 0x3734e00,
    KNOB_ROTATE_4D = 0x3774e00,
    KNOB_ROTATE = 0x3654e00,
    TOUCHSTRIP = 0x3744e00,
    --unknown:
    --0x3444e00 (after power on/off)
}, CONST_META)

-- quick access hash wrapper
niproto.CONST_MESSAGE_NAMES = {}
for k,v in pairs(niproto.CONST_MESSAGE_IDS) do
    niproto.CONST_MESSAGE_NAMES[v] = k
end
niproto.CONST_MESSAGE_NAMES = setmetatable(niproto.CONST_MESSAGE_NAMES, CONST_META)


--[[
MessageIdToString MessageIdToStringTable[] = {
    { 0x02536756, @"NIGetServiceVersionMessage"   },
    { 0x02444300, @"NIDeviceConnectMessage"       },
    { 0x02404300, @"NISetAsciiStringMessage"      },
    { 0x02446724, @"NIGetDeviceEnabledMessage"    },
    { 0x02444e00, @"NIDeviceStateChangeMessage"   },
    { 0x02446743, @"NIGetDeviceAvailableMessage"  },
    { 0x02434e00, @"NISetFocusMessage"            },
    { 0x02446744, @"NIGetDriverVersionMessage"    },
    { 0x02436746, @"NIGetFirmwareVersionMessage"  },
    { 0x02436753, @"NIGetSerialNumberMessage"     },
    { 0x02646749, @"NIGetDisplayInvertedMessage"  },
    { 0x02646743, @"NIGetDisplayContrastMessage"  },
    { 0x02646742, @"NIGetDisplayBacklightMessage" },
    { 0x02566766, @"NIGetFloatPropertyMessage"    },
    { 0x02647344, @"NIDisplayDrawMessage"         },
    { 0x02654e00, @"NIWheelsChangedMessage"       },
    { 0x02504e00, @"NIPadsChangedMessage"         },
    { 0x026c7500, @"NISetLedStateMessage"         },
};

@interface NIGetServiceVersionMessage   : NIPlainMessage       @end
@interface NIGetDriverVersionMessage    : NIPlainMessage       @end
@interface NIGetFirmwareVersionMessage  : NIPlainMessage       @end
@interface NIGetSerialNumberMessage     : NIPlainMessage       @end
@interface NIGetDeviceAvailableMessage  : NIPlainMessage       @end
@interface NIDeviceStateChangeMessage   : NINumberValueMessage @end
@interface NIGetDisplayInvertedMessage  : NINumberValueMessage @end
@interface NIGetDisplayContrastMessage  : NINumberValueMessage @end
@interface NIGetDisplayBacklightMessage : NINumberValueMessage @end
@interface NIGetFloatPropertyMessage    : NINumberValueMessage @end
@interface NIGetDeviceEnabledMessage    : NINumberValueMessage @end
@interface NISetFocusMessage            : NINumberValueMessage @end

@implementation NIDeviceStateChangeMessage   @end
@implementation NIGetDeviceEnabledMessage    @end
@implementation NIGetServiceVersionMessage   @end
@implementation NIGetDeviceAvailableMessage  @end
@implementation NISetFocusMessage            @end
@implementation NIGetDriverVersionMessage    @end
@implementation NIGetFirmwareVersionMessage  @end
@implementation NIGetSerialNumberMessage     @end
@implementation NIGetDisplayInvertedMessage  @end
@implementation NIGetDisplayContrastMessage  @end
@implementation NIGetDisplayBacklightMessage @end
@implementation NIGetFloatPropertyMessage    @end


@interface NIDeviceConnectMessage : NIMessage
@property uint32_t   controllerId;
@property uint32_t   boh;
@property uint32_t   clientRole;
@property NSString * clientName;
@end

@interface NISetAsciiStringMessage : NIMessage
@property uint32_t   boh1;
@property uint32_t   boh2;
@property NSString * string;
@end

@interface NIDisplayDrawMessage : NIMessage
@property uint32_t   displayNumber;
@property uint16_t   originX;
@property uint16_t   originY;
@property uint16_t   sizeWidth;
@property uint16_t   sizeHeight;
@property NSData   * st7529EncodedImage;
@end

@interface NIWheelsChangedEvent : NSObject
@property uint32_t wheelId;
@property float delta;
@end

@interface NIWheelsChangedMessage : NIMessage
@property uint32_t   boh1;
@property uint32_t   boh2;
@property NSArray  * events;
@end

@interface NIPadsChangedEvent : NSObject
@property uint32_t padId;
@property uint32_t eventState;
@property float pressure;
@end

@interface NIPadsChangedMessage : NIMessage
@property uint32_t   boh1;
@property uint32_t   boh2;
@property NSArray  * events;
@end

@interface NILedState : NSObject
- (void)setLed:(uint8_t)led intensity:(uint8_t)intensity;
- (uint8_t)getLedIntensity:(uint8_t)led;
- (NSData *)dataRepresentation;
@end

@interface NISetLedStateMessage : NIMessage
@property NILedState * state;
@end
--]]

function niproto.getDevice(name)
    return niproto.CONST_DEVICES[name:upper()]
end

function niproto.openPort(name, _retries)
    local retries = _retries or 1
    local port = NIIPC.new(name)
    local opened = false

    while opened == false and retries > 0 do
        retries = retries - 1
        opened  = port:open()
        if opened then
            return port
        end
        if opened == false and retries > 0 then
            App.sleep(1);
        end
    end
end

function niproto.createPort(name, callback, _retries)
    local retries = _retries or 1
    local port = NIIPC.new(name)
    if callback then
        log.info("createPort: Callback set")
        port.callback = callback
    end
    local created = false

    while created == false and retries > 0 do
        retries = retries - 1
        created  = port:create()
        if created then
            return port
        end
        if created == false and retries > 0 then
            App.sleep(1);
        end
    end
end

function niproto.getBootstrapConnection(devicename, retries)

    local product = niproto.getDevice(devicename)
    if not product then
        return nil, "Device " .. devicename .. " not found"
    end

    return niproto.openPort(product.port, retries)
end

function niproto.getNihaConnection(retries)
    return niproto.openPort(niproto.CONST_PORT_MAIN, retries)
end

function niproto.getNihiaConnection(retries)
    return niproto.openPort(niproto.CONST_PORT_HOST,retries)
end

function niproto._pack(...)
    local t = {...}
    return spack((niproto._structpref or "") .. t[1],tunpack(t,2))
end

function niproto._tobitstring(num,len)
    local len=len or 1 --luacheck: no redefined
    while (1 << len) < num do
        len = len + 1
    end

    local res={}
    for i= len,0,-1 do
        tinsert(res, (num & (1 << i)) >> i )
        --if (i+1) % 8 == 1 then
        --    tinsert(res,"|")
        --end
    end
    return table.concat(res)
end

function niproto._hexdump(data, nobr)
    local ttype = type(data)
    local t
    if ttype == 'table' then
        t = data
    elseif ttype == 'string' then
        t = {}
        data:gsub(".", function(c)
            tinsert(t,string.byte(c))
        end)
    end

    local tmp = {}
    local cnt = 1
    for _,v in pairs(t) do
        local ttype = type(v) --luacheck: no redefined
        if ttype == "number" then
            tinsert(tmp, string.format('0x%02X', v))
            if v > 31 then
                tinsert(tmp,"[" .. string.char(v) .. "] ")
            else
                tinsert(tmp, "[ ] ")
            end
            if cnt > 0 and cnt % 4 == 0 then tinsert(tmp, "    "); end
            if (nobr == nil or nobr == false) and cnt > 0 and cnt ~= #t and cnt % 8 == 0 then tinsert(tmp, "\n"); end
            cnt = cnt + 1
        else
            tinsert(tmp,tostring(v))
        end
    end
    return tconcat(tmp)
end


local lastdumpline = ""
local lastprefix = ""
function niproto._dumpdata(data, len, prefix) --luacheck: no unused
    local prefix = prefix and (prefix .. "\t") or ("") --luacheck: no redefined
    local f = io.open("printdump.log","a+")
    if f then
        local cline = niproto._hexdump(data, true)
        local nline = {}
        for i=1,cline:len() do

            local chr = cline:sub(i, i)
            if
                lastprefix ~= prefix or
                i > lastdumpline:len() or
                lastdumpline:sub(i,i) ~= chr
                then
                    nline[i] = chr
            else
                nline[i] = " "
            end
        end
        lastdumpline = cline
        lastprefix = prefix

        f:write(os.date("%X") ..  "|" .. prefix .. tconcat(nline) .. "\n")
        f:close()
    end
end

function niproto.writedump(self, prefix, data, len)
    local msgid, field2 = niproto._parseresult("ii", data, len)
    msgid = niproto.CONST_MESSAGE_NAMES[msgid] and (niproto.CONST_MESSAGE_NAMES[msgid] .. " " ..
            string.format("%i",field2)) or string.format("0x%x %i ", msgid, field2)
    local DID
    if self.getDevice() ~= nil then
        DID=self:getDevice():getName():sub(1,1) .. self:getDevice():getName():sub(-3)
    else
        DID=self:getName():sub(1,1) .. self:getName():sub(-3)
    end
    niproto._dumpdata(data, len, DID .. ">" .. prefix .. " " .. msgid)
    log.error("\n" .. prefix .. " DUMP " .. os.date("%x %X") .. "\t",niproto._hexdump(data, true))
end

function niproto._unpack(...)
    local t = {...}
    local res = { sunpack((niproto._structpref or "") .. t[1],tunpack(t,2)) }
    if #res > 1 then
        --last parameter of struct.unpack is last position in data, according to
        --http://www.inf.puc-rio.br/~roberto/struct/
        local spos = res[#res]
        res[#res] = nil
        res = setmetatable(res, {
            __tostring = function(t) --luacheck: no redefined
                return niproto._hexdump(t)
            end
        })

        return res, spos
    end
end

--broken?
function niproto._fromhex(str)
    return (str:gsub('..', function (cc)
        return string.char(tonumber(cc, 16))
    end))
end

--broken?
function niproto._tohex(str)
    return (str:gsub('.', function (c)
        return string.format('%02X', string.byte(c))
    end))
end

--[[
=> 56 67 53 03                                      VgS.
<= 00 09 01 00 03 00 00 00                          ........
--]]

function niproto._parseresult(fmt, data, len) --luacheck: no unused
    return sunpack("!4<" .. fmt, data)
end

function niproto.MSG_VERSION()
    -- 03 S g V |version? ping?
    return niproto._pack("i", 0x03536756)
end

function niproto.PARSE_VERSION_RESULT(data, len) --luacheck: no unused
    return niproto._unpack("BBBBBBBB", data)
end


function niproto.MSG_PID_CONNECT(deviceid)
    -- 03 D u 00
    return niproto._pack("iiiii",
            0x3447500, --msgid
            deviceid,  --uid/controllerid
            niproto.CONST_NISW, --boh?
            niproto.CONST_PRMY, --client role?
            0
        )
end

function niproto.PARSE_PID_CONNECT_RESULT(data, len) --luacheck: no unused
    if not data then return; end
    if type(data) ~= "string" then return ; end

--    local res = data:sub(1,4):reverse()
--    local len = niproto._unpack("i",data:sub(5,8))
--    print(res,len)
--    print(data:sub(9,len))

    local res,reqportlen = sunpack("!4<ii",data)
    if res ~= niproto.CONST_TRUE then
        return nil, "Result not true"
    end

    local notifportlen = sunpack("!4<i", data:sub(9 + reqportlen))

    local reqportname = data:sub(9,9+reqportlen-2)
    local notifportname = data:sub(13+reqportlen,13+reqportlen+notifportlen-2)

    return reqportname, notifportname
end

function niproto.MSG_ACK_NOT_PORT(name)
    -- 03 @ C 00
    --[[
    port_name_msg_t port_name_msg = {};
    port_name_msg.nonce = nonce;
	strncpy(port_name_msg.name, name, kAgentNotificationPortNameLen-1);
    port_name_msg.trueStr = kTrue;
    port_name_msg.unk = 0;
    port_name_msg.len = kAgentNotificationPortNameLen;

    typedef struct __attribute__((packed)) {
        uint32_t nonce;
        uint32_t trueStr;
        uint32_t unk;
        uint32_t len;
        char     name[kAgentNotificationPortNameLen];
    } port_name_msg_t;
    --]]
    return niproto._pack("iiiis",
                0x3404300,
                niproto.CONST_TRUE,
                0, --0x6e???
                name:len(),
                name
            )
end

function niproto.PARSE_ACK_NOT_PORT_RESULT(data, len) --luacheck: no unused
--    return niproto._unpack("BBBBBBBB", data)
    return data
end

function niproto.MSG_DEVSTATE()
    -- 03 C q C
    return niproto._pack("i", 0x03447143)
end

function niproto.PARSE_DEVSTATE_RESULT(data, len) --luacheck: no unused
--    return niproto._unpack("BBBBBBBB", data)
    return data
end

function niproto.MSG_SERIAL_CONNECT(deviceid, serial)
    -- 03 D I 00
    if deviceid == #niproto.CONST_DEVICES.KKM then
        return niproto._pack("iiiiic" .. serial:len(),
                0x03444900, --nonce
                deviceid,  --uid
                niproto.CONST_NIKK,
                niproto.CONST_PRMY,
                serial:len(),
                serial
            )
    else 
        return niproto._pack("iiiiic" .. serial:len(),
                0x03444900, --nonce
                deviceid,  --uid
                niproto.CONST_NISW, -- or CONST_NIM2
                niproto.CONST_PRMY,
                serial:len(),
                serial
            )
    end
end

--seems to be the same as RESULT_PID_CONNECT?
function niproto.PARSE_SERIAL_CONNECT_RESULT(data, len) --luacheck: no unused
--    local res = data:sub(1,4):reverse()
--    local len = niproto._unpack("i",data:sub(5,8))
--    print(res,len)
--    print(data:sub(9,len))

    local res,reqportlen = sunpack("!4<ii",data)
    if res ~= niproto.CONST_TRUE then
        return nil, "Result not true"
    end

    local notifportlen = sunpack("!4<i", data:sub(9 + reqportlen))

    local reqportname = data:sub(9,9+reqportlen-2)
    local notifportname = data:sub(13+reqportlen,13+reqportlen+notifportlen-2)
    return reqportname, notifportname
end

function niproto.PARSE_INSTANCE_CALLBACK_DATA(data, len) --luacheck: no unused

    log.info("PARSE_INSTANCE_CALLBACK_DATA> Trying to parse.")
    local dat, res = sunpack("!4<ii",data)
    if res ~= niproto.CONST_TRUE then
        --log.warn("PARSE_INSTANCE_CALLBACK_DATA° result not true")
        return nil, "Result not true"
    end

    return dat
end

function niproto.MSG_KEYCOUNT()
    return niproto._pack("ii",
        0x03566775, --ugV.
        0x4B657973  --syeK
    )
end

function niproto.PARSE_KEYCOUNT_RESULT(data, len) --luacheck: no unused
    if not data then return ; end
    if type(data) ~= "string" then return ; end

    local res = sunpack("!4<i",data)
    return res
end

function niproto.MSG_RKEYCOUNT()
    return niproto._pack("ii",
        0x03566775, --ugV.
        0x524B6579  --yeKR
    )
end

function niproto.PARSE_RKEYCOUNT_RESULT(data, len) --luacheck: no unused
    if not data then return ; end
    if type(data) ~= "string" then return ; end

    local res = sunpack("!4<i",data)
    return res
end


function niproto.MSG_GETSERIAL()
    return niproto._pack("i",
        0x3436753 --SgC.
    )
end

function niproto.PARSE_GETSERIAL_RESULT(data, len) --luacheck: no unused
    if not data then return ; end
    if type(data) ~= "string" then return ; end

    local slen, serial = sunpack("!4<is",data) --luacheck: no unused
    return serial
end

function niproto.MSG_NEWPROJECT()
    return niproto._pack("iiiiiii",
        0x349734e, --NsI.
        0xe7342bf0,
        0x00007ffe,
        0x0c,
        0x2077654e, --New
        0x6a6f7250, --Proj
        0x00746365
    )
end

function niproto.PARSE_NEWPROJECT_RESULT(data, len) --luacheck: no unused
    if not data then return ; end
    return data
end

function niproto.MSG_PROJECTNAME(name)
    return niproto._pack("iiiic" .. name:len() + 1,
        0x0349734e, --NsI.
        0x70001006,
        0xf6b24000,
        name:len() + 1, --msglen => strlen + 1 + 6x
        name .. '\0'
    )
end

function niproto.PARSE_PROJECTNAME_RESULT(data, len) --luacheck: no unused
    if not data then return ; end
    return data
end

function niproto.MSG_MGD(deviceid)
    return niproto._pack("ii",
        0x344674d, --MgD
        deviceid
    )
end

function niproto.PARSE_MGD_RESULT(data, len) --luacheck: no unused
    if not data then return ; end
    if type(data) ~= "string" then return ; end

    --[[
            if (*((uint32_t *)(response)) == 'MIDI') {
                printf("=> MIDI\n");
            }
            if (*((uint32_t *)(response)) == 'APP ') {
                printf("=> APP\n");
            }
    --]]
    local res = sunpack("!4<i",data)
    return res
end

function niproto.MSG_TRTS()
    return niproto._pack("ii",
        0x03434300, --.CC.
        0x73747274 --trts
    )
end

function niproto.PARSE_TRTS_RESULT(data, len) --luacheck: no unused
    if not data then return ; end
    return data
end

function niproto.MSG_TSI()
    return niproto._pack("ii",
        0x3497354, --TsI.
        0x0 --0
    )
end

function niproto.PARSE_TSI_RESULT(data, len) --luacheck: no unused
    if not data then return ; end
    return data
end


function niproto.MSG_FGC()
    return niproto._pack("i",
        0x3436746 --FgC.
    )
end

function niproto.PARSE_FGC_RESULT(data, len) --luacheck: no unused
    if not data then return ; end
    return data
end

function niproto.MSG_TSI_TRUE()
    return niproto._pack("ii",
        0x3497374, --TsI.
        0x74727565 --true
    )
end

function niproto.PARSE_TSI_TRUE_RESULT(data, len) --luacheck: no unused
    if not data then return ; end
    return data
end

function niproto.MSG_DSD_1()
    return niproto._pack("iiiiiiiiiiiii",
        0x3647344,  -- D  s  d  .
        0x0,        --00 00 00 00
        0x0,        --00 00 00 00
        0x01e00110, --10 01 E0 01

        0x20,       --20 00 00 00
        0x60000084, --84 00 *00 60
        0x0,        --00 00 00 00
        0x0,        --00 00 00 00

        0x1001e001, --01 e0 01 10
        0x00ff0001, --00 ff 00 00
        0x0,        --00 00 00 00
        0x3,        --03 00 00 00

        0x00000040 --40 00 01 00
    )
end

function niproto.MSG_DSD_2()
    return niproto._pack("iiiiiiiiiiiii",
        0x3647344,  -- D  s  d  .
        0x1,        --01 00 00 00
        0x0,        --00 00 00 00
        0x01e00110, --10 01 E0 01

        0x20,       --20 00 00 00
        0x60010084, --84 00 *01 60
        0x0,        --00 00 00 00
        0x0,        --00 00 00 00

        0x1001e001, --01 e0 01 10
        0x00ff0001, --00 ff 00 00
        0x0,        --00 00 00 00
        0x3,        --03 00 00 00

        0x00010040 --40 00 01 00
    )
end

function niproto.PARSE_DSD_RESULT(data, len) --luacheck: no unused
    if not data then return ; end
    return data
end

function niproto.MSG_RD()
    return niproto._pack("ii",
        0x3445200,  --.RD.
        0x2         --02 00 00 00
    )
end

function niproto.PARSE_RD_RESULT(data, len) --luacheck: no unused
    if not data then return ; end
    return data
end

function niproto.MSG_AD()
    return niproto._pack("iiii",
        0x3444100,  --.AD.
        0x03,       --03 00 00 00
        0x0,        --00 00 00 00
        0x4e297b0   --b0 97 e2 04
    )
end

function niproto.PARSE_AD_RESULT(data, len) --luacheck: no unused
    if not data then return ; end
    return data
end

function niproto.initLedData(deviceid)
    local data = {}
    local ledcnt=0
    if deviceid and niproto.CONST_DEVICES[deviceid]  then
        ledcnt = niproto.CONST_DEVICES[deviceid].ledcnt or 0
    end

    for i=1,ledcnt do
        data[i]=0
    end

    log.error("INIT LED COUNT: ", #data)
    return data
end

function niproto.setLedColor(data, led, color, intense)
--    btn_code = btn_num_to_code(btn_num);
    local led = led or 0            --luacheck: no redefined
    local color = color or 0        --luacheck: no redefined
    local intense = intense or 0    --luacheck: no redefined

    if led <= 0 then
        log.error("ERROR: btn index is 0")
        return
    end
    if led > #data then
        log.error("ERROR: led index ",led," is higher than ",#data)
        return
    end

    if type(color) == "string" and niproto.CONST_COLORS[color] then
        color = niproto.CONST_COLORS[color]
    end

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
    local lcolor = (color * 4) | intense

    --log.warn("LED:", led," COLOR:", color," INTENSE:", intense, " LCOLOR:", lcolor)
    data[led] = lcolor
end

function niproto.MSG_LED(data)
    --[[
    log.error("LIGHTUP:\n", niproto._hexdump(niproto._pack(
        "ii" .. string.rep('b',#data),
        0x36C7500,  --button msg
        #data//3,    --button array len (*3 => r,g,b)
        table.unpack(data)
    )))
    --]]
    --log.info("packing MSG_LED data: ",#data)
    --log.info(niproto._hexdump(data))
    return niproto._pack(
        "ii" .. string.rep('b',#data),
        0x36C7500,  --button msg
        #data,    --button array
        table.unpack(data)
    )
end

function niproto.PARSE_LED_RESULT(data, len) --luacheck: no unused
    if not data then return ; end
    return data
end


function niproto.getRGB565Color(red,green,blue)
  local _red = red >> 3
  local _green = green >> 2
  local _blue = blue >> 3
  return (_red << 11) | (_green << 5) | _blue
end

function niproto.initDisplayData()
    local data = {}
    local pixelcnt=1024

    for i=1,pixelcnt do
        data[i]=0
    end

    return data
end


function niproto.MSG_DISPLAY(displayid, data) --PATCHED_CHUNKED_DISPLAY
    -- Chunked packing to avoid Lua stack overflow on large display data
    local header = spack("!4<iiiii",
        0x3647344,
        displayid,
        0,
        0x01e00110,
        #data
    )
    local chunks = { header }
    local CHUNK = 4000
    for i = 1, #data, CHUNK do
        local j = math.min(i + CHUNK - 1, #data)
        local n = j - i + 1
        chunks[#chunks + 1] = spack(string.rep('b', n), table.unpack(data, i, j))
    end
    return table.concat(chunks)
end

function niproto.PARSE_DISPLAY_RESULT(data, len) --luacheck: no unused
    if not data then return ; end
    return data
end

function niproto.parseEventMessage(data, len, self)
    local msgid = sunpack("!4<i",data)
    if msgid == nil then
        return nil, "Error parsing event messageid"
    end

    local msgname = niproto.CONST_MESSAGE_NAMES[msgid]
    if msgname == nil then
        return nil, "Error, no message name found for msgid " .. string.format("0x%x", msgid)
    end

    local msgparser = niproto["PARSE_" .. msgname .. "_EVENT"]
    if not msgparser then
        return nil, "Error, no msgparser function found for msgid " ..
                string.format("0x%x", msgid) .. "(" .. msgname .. ")"
    end

    local res, err = msgparser(data, len, self)
    if not res then
        if err then log.warn("parseEventMessage> no result, error: " .. (err or "-")) end
        return nil, err
    end

    local ret = {
        name = msgname,
        id = msgid,
        data = res
    }

    --log.info("MSGNAME: " .. msgname .. " > " .. plpretty.write(ret))

    return ret
end

function niproto.PARSE_DEVICE_STATE_EVENT(data, len) --luacheck: no unused
    local res = sunpack("!4<i",data)
    local state
    local serial

    if res == niproto.CONST_MESSAGE_IDS.DEVICE_STATE_ON then
        state = "ON"
    elseif res == niproto.CONST_MESSAGE_IDS.DEVICE_STATE_OFF then
        state = "OFF"
    end

    if state then
        local slen = sunpack("!4<i", data:sub(13))
        serial = data:sub(17,17+slen)
    end

    if not state or not serial then
        return nil, "Error parsing device state message"
    end

    return {
        serial = serial,
        state =  state
    }
end
niproto.PARSE_DEVICE_STATE_ON_EVENT = niproto.PARSE_DEVICE_STATE_EVENT
niproto.PARSE_DEVICE_STATE_OFF_EVENT = niproto.PARSE_DEVICE_STATE_EVENT

function niproto._pad_code_to_num(code)
    --https://github.com/SamL98/NIProtocol/blob/4eb4451d736aac97e020ec3c6f47903d1c52b40c/parser/parser.cpp#L5
    --Thinking of the buttons as a 2d array,
	--the button code is the linear index into that array.
	--We want to convert that to the button number that appears on the MK2.
	-- 0, 1, 2, 3       =>      13, 14, 15, 16
    -- 4, 5, 6, 7               9, 10, 11, 12
	-- 8, 9, 10, 11             5, 6, 7, 8
	-- 12, 13, 14, 15           1, 2, 3, 4
	--Do this by taking the linear index of the code when reflected
	--about the y-axis and adding 1.
	local r = code // 4;
	local c = code % 4;
	return ((3 - r) * 4 + c + 1) // 1;
end

function niproto._pad_num_to_code(num)
    --https://github.com/SamL98/NIProtocol/blob/4eb4451d736aac97e020ec3c6f47903d1c52b40c/parser/parser.cpp#L32
    --Perform the inverse of btn_num_to_btn_code
	local num = num - 1; --luacheck: no redefined
	local r = 3 - num // 4;
	local c = num % 4;
	return (r * 4 + c) // 1;
end

local _pressure_lowest = nil
local _pressure_highest = nil
function niproto._pad_calculate_pressure(pressure)

    if pressure == 0 then
        return 0
    end

    if _pressure_lowest == nil then
        _pressure_lowest = pressure
    end
    if _pressure_highest == nil then
        _pressure_highest = pressure
    end

    if _pressure_lowest > pressure then
        _pressure_lowest = pressure
    end
    if _pressure_highest < pressure then
        _pressure_highest = pressure
    end

    local step = (_pressure_highest - _pressure_lowest) / 127
    local cpressure = (pressure - _pressure_lowest) // step
    if cpressure ~= cpressure then
        cpressure = 0
    end
    --log.info("PAD_CALIBRATE:",_pressure_lowest, _pressure_highest, step, pressure, cpressure)

    return cpressure
end

function niproto.PARSE_PAD_DATA_EVENT(data, len) --luacheck: no unused

    local _, cnt, unk1, order, pad, nstate, pressure = sunpack("!4<iiiiiii", data)
    if cnt == nil or unk1 == nil or order == nil or pad == nil or nstate == nil or pressure == nil then
        return nil, "Error, parsing of PAD_DATA failed"
    end
    log.info("MAPPING>PAD_DATA: ", (cnt or "-"), unk1 or "-", order or "-", (pad or "-"), (nstate or "-"), (pressure / 100000) or "-")

    local cpressure = niproto._pad_calculate_pressure(pressure)

    --pad states seem to be from 1-4, sometimes number 2 and 3 are fired before number 4
    --Maybe this is some bouncing detection?

    local state = pressure > 0 and "PRESSED" or "RELEASED"
    return {
        order = order,
        padid = niproto._pad_code_to_num(pad),
        pad = "PAD" .. tostring(niproto._pad_code_to_num(pad)),
        nstate = nstate,
        state = state,
        unk1 = unk1,
        pressure = pressure // 1,
        cpressure = cpressure
    }

end

local function addPressedBtn(self, btn)
    local btnStack = self:getPressedBtnStack() or {}
    tinsert(btnStack, btn)

    self:setPressedBtnStack(btnStack)
    return { tconcat(btnStack,"/") }
end

local function removePressedBtn(self, btn)
    local btnStack = self:getPressedBtnStack() or {}
    local found = false
    local events = {}

    for idx,sBtn in ipairs(btnStack) do
        if found == false and sBtn == btn then
            found = idx
        end
    end

    if found then
        log.info("Found on stack: ", found, #btnStack)
        for i = #btnStack,found,-1 do --luacheck: no unused
            log.info("Remove-Event:",tconcat(btnStack,"/"))
            tinsert( events, tconcat(btnStack,"/") )
            tremove(btnStack)
        end
    end

    self:setPressedBtnStack(btnStack)
    return events
end

--local lastbitstring = ""
--local bitstrings = {}
function niproto.PARSE_BTN_DATA_EVENT(data, len, self) --luacheck: no unused
    log:warn("PARSE_BTN_DATA_EVENT> TODO: replace self with deviceid, self should never be passed to niproto")
    log:warn("PARSE_BTN_DATA_EVENT> CURRENT STATE: " .. self:getState())
    local deviceid = self:getDevice():getId()

    --Buttondata-Len: 78
    --0x00[ ] 0x4E[N] 0x73[s] 0x03[ ]     0xFD[�] 0x1A[ ] 0x00[ ] 0x00[ ]
    --0xF4[�] 0xD2[�] 0x7E[~] 0x30[0]     0x01[ ] 0x00[ ] 0x00[ ] 0x00[ ]
    --0x25[%] 0x00[ ] 0x00[ ] 0x00[ ]     0x01[ ] 0xF0[�] 0x68[h] 0xB1[�]

    --[[ CLEAR key?
        --- >>> SEND --------------- 24
        0x00[ ] 0x4e[N] 0x65[e] 0x03[ ]         0x92[.] 0x10[ ] 0x00[ ] 0x00[ ]
        0xab[.] 0x05[ ] 0x93[.] 0x1e[ ]         0x01[ ] 0x00[ ] 0x00[ ] 0x00[ ]
        0x01[ ] 0x00[ ] 0x00[ ] 0x00[ ]         0x6f[o] 0x12[ ] 0x83[.] 0xba[.]
    --]]

    local cnt, unk1, msgtype, btn, state, _
    if deviceid == #CONST_DEVICES.MMK3 then
        _, cnt, unk1, msgtype, btn, state = sunpack("!4<iiiiib", data)
    --elseif deviceid == 0x1610 then
    else
        cnt, unk1, msgtype = sunpack("!4<iii",data,5) --luacheck: no unused
        if msgtype == 1 then
            log.info("msgtype == 1 => parseable")
            _, cnt, unk1, msgtype, btn, state = sunpack("!4<iiiiib", data)
        elseif deviceid == #CONST_DEVICES.KKM and msgtype == 0 then
            log.error("msgtype != 1 => that's an unknown wrong mode. Only sent sometimes by KKMK2 (and KK-M32!)")
            return nil, "Ignoring message type 0"
        else
            return nil, "Ignoring message type 0"
        end
    --else
    --    log.error("Unsupported deviceid")
    --    os.exit(1)
    end

    log.info("MAPPING>BTN_DATA: ", (cnt) or "-", unk1 or "-", msgtype or "-", (btn) or "-", (state) or "-")
    if cnt == nil or unk1 == nil or msgtype == nil or btn == nil or state == nil then
        return nil, "Error, parsing of BTN_DATA failed"
    end

    --local pressed = state & ~(0xffffff << 8)
    --log.info("State:",state, "Pressed:", pressed)

    log.info("DEB> btnid: " .. btn)
    log.info("DEB> deviceid: " .. string.format("0x%x", deviceid))
    local citems = niproto.CONST_CONTROLS[deviceid]
    if not citems then
        log.error("error getting deviceid items")
        os.exit(1)
    end

    --try to resolve device names
    local btnid = btn
    if deviceid and niproto.CONST_CONTROLS[deviceid] and niproto.CONST_CONTROLS[deviceid][btn] then
        btn = niproto.CONST_CONTROLS[deviceid][btn].name
    end


    local event = state > 0 and "PRESSED" or "RELEASED"

    log.info("BTN_DATA_PARSED: State " .. tostring(state) .. ", " .. event)

    local events
    if event == "PRESSED" then
        events = addPressedBtn(self, btn)
    else
        events = removePressedBtn(self, btn)
    end

    return {
        button = btn,
        buttonid = btnid,
        state = event,
        unk1 = unk1,
        stategroups = events

    }
end

local _rotation_lowest = nil
local _rotation_highest = nil
function niproto._knob_calculate_rotation(rotation)

    if rotation == 0 then
        return 0
    end

    local arotation = math.abs(rotation)

    if _rotation_lowest == nil then
        _rotation_lowest = arotation
    end
    if _rotation_highest == nil then
        _rotation_highest = arotation
    end

    if _rotation_lowest > arotation then
        _rotation_lowest = arotation
    end
    if _rotation_highest < arotation then
        _rotation_highest = arotation
    end

    local rotationstep = (_rotation_highest - _rotation_lowest) / 127
    local crotation = (rotation - _rotation_lowest) // rotationstep
    --log.info("ROTATION_CALIBRATE:",_rotation_lowest, _rotation_highest, rotation, rotationstep, crotation)

    return crotation
end

function niproto.PARSE_KNOB_ROTATE_EVENT(data, len) --luacheck: no unused
    local _, cnt, unk1, msgtype, knob, rotation = sunpack("!4<iiiiii", data)

    --log.info("KNOB_ROTATE: ", (cnt) or "-", unk1 or "-", msgtype or "-", (knob) or "-", (rotation) or "-")
    if cnt == nil or unk1 == nil or msgtype == nil or knob == nil or rotation == nil then
        return nil, "Error, parsing of KNOB_ROTATE failed"
    end

    local direction
    if rotation > 0 then
        direction="CLOCKWISE"
    else
        direction="COUNTER_CLOCKWISE"
    end

    local crotation = niproto._knob_calculate_rotation(rotation)
    --log.error("Rotation:",direction, rotation, crotation)
    if crotation  ~= crotation then --not set yet? nan. check for nan means: compare value against itself is not equal.
        crotation=0
    end

    return {
        knob = "KNOB" .. tostring(knob+1),
        direction = direction,
        rotation = rotation // 1,
        crotation = crotation
    }

end

function niproto.PARSE_KNOB_ROTATE_4D_EVENT(data, len) --luacheck: no unused
    log.info("TODO: PARSE_KNOB_ROTATE_4D_EVENT")
end

function niproto.PARSE_TOUCHSTRIP_EVENT(data, len) --luacheck: no unused

      local _,_,unk1,unk2,tid,cnt,unk3,pos1,pos2 = sunpack("!4<IIIIIIIhh", data)
      log.info("TOUCHSTRIP: ",unk1,unk2,tid,cnt,unk3,pos1,pos2)


    if unk1 == nil or unk2 == nil or unk3 == nil or cnt == nil or tid == nil or pos1 == nil or pos2 == nil then
        return nil, "Error, parsing of TOUCHSTRIP failed"
    end

    return {
        knob = "TOUCHSTRIP" .. tostring(tid+1),
        pos1 = pos1,
        pos2 = pos2,
        state = pos1 > 0 and "TOUCHED" or "UNTOUCHED"
    }


end

return niproto
