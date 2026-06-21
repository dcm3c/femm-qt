-----------------------------------------------------------------------------
-- Program constants
-----------------------------------------------------------------------------
local TIMEOUT = 20
local PORT = 25
local CONNECT_ERR = "connection error"
local TIMEOUT_ERR = "connection timed out"
local UNKNOWN_ERR = "unhandled server reply"
local UNAVAILABLE_ERR = "service unavailable, please try later"
local SYNTAX_ERR = "syntax error"
local ABORTED_ERR = "command aborted"
local OVERFLOW_ERR = "server buffer overflow"

local DOMAIN = getenv("SERVER_NAME")
if not DOMAIN then
    DOMAIN = "localhost"
end

-----------------------------------------------------------------------------
-- Send DOS mode lines.
-- Input
--   sock: server socket
--   str: string to be sent
-- Returns
--   err: true if error
-----------------------------------------------------------------------------
local puts = function (sock, str) -- : err
    return sock:send(str, "\r\n")
end

-----------------------------------------------------------------------------
-- Receive DOS mode lines.
-- Input
--   sock: server socket
-- Returns
--   str: received string
--   err: true if error
-----------------------------------------------------------------------------
local gets = function (sock) -- : str, err
	return sock:receive("*l")
end

-----------------------------------------------------------------------------
-- Gets a reply from the server
-- Input
--   sock: server socket
-- Returns
--   code: server reply code. nil if timed out.
--   msg: server reply message
-----------------------------------------------------------------------------
local get_reply = function (sock) -- : code, msg
    local line, err = %gets(sock) 
    if line then
        local code = tonumber(strsub(line, 1, 3))
        local msg = strsub(line, 5)
        if msg then msg = strlower(msg) else msg = "" end
        return code, msg
    else
        return nil, nil
    end
end

-----------------------------------------------------------------------------
-- Sends a command to the server
-- Input
--   sock: server socket
--   command: command to be sent
--   param: command parameters
-- Returns
--   err: true if timed out
-----------------------------------------------------------------------------
local send_command = function (sock, command, param) -- : err
    local line
    if param then
        line = format("%s %s", command, param)
    else
        line = command
    end
    return %puts(sock, line)
end

-----------------------------------------------------------------------------
-- Returns a string with a formatted error message
-- Input
--   msg: error message
--   code: error code
-- Returns
--   msg: the formatted string
-----------------------------------------------------------------------------
local error_msg = function (msg, code) -- : msg
    if code then
        return format("%s (%d)", msg, code)
    else
        return msg
    end
end

-----------------------------------------------------------------------------
-- Gets the initial server greeting
-- Input
--   sock: server socket
-- Returns
--   msg: a toolkit message describing the error. nil if successfull
--   smsg: a server generated error message.
-----------------------------------------------------------------------------
local get_helo = function (sock) -- : msg, smsg
    local code, smsg = %get_reply(sock)
    if code == 220 then
        return nil, nil
    elseif code == 421 then
        return %error_msg(%UNAVAILABLE_ERR, code), smsg
    elseif not code then
        return %error_msg(%TIMEOUT_ERR), nil
    else 
        return %error_msg(%UNKNOWN_ERR, code), smsg
    end
end

-----------------------------------------------------------------------------
-- Sends initial client greeting
-- Input
--   sock: server socket
-- Returns
--   msg: a toolkit message describing the error. nil if successfull
--   smsg: a server generated error message.
-----------------------------------------------------------------------------
local send_helo = function (sock) -- : msg, smsg
    local err = %send_command(sock, "HELO", %DOMAIN)
    if not err then
        local code, smsg = %get_reply(sock)
        if code == 250 then
            return nil, nil
        elseif (code == 500) or (code == 501) then
            return %error_msg(%SYNTAX_ERR.." (domain error) '"..
                %DOMAIN .. "' ", code), smsg
        elseif code == 421 then
            return %error_msg(%UNAVAILABLE_ERR, code), smsg
        elseif not code then
            return %error_msg(%TIMEOUT_ERR), nil
        else
            return %error_msg(%UNKNOWN_ERR, code), smsg
        end
    else
        return %error_msg(%TIMEOUT_ERR), nil
    end
    return nil, nil
end

-----------------------------------------------------------------------------
-- Sends mime headers
-- Input
--   sock: server socket
--   mime: table with mime headers to be sent
-- Returns
--   err: true if error
-----------------------------------------------------------------------------
local send_mime = function (sock, mime)
	local name, value = next(mime, nil)
	local err
	while name do
		err = sock:send(name, ": ", value, "\r\n")
		if err then return err end
		name, value = next(mime, name)
	end
	-- end mime part
	err = sock:send("\r\n")
	return err
end

-----------------------------------------------------------------------------
-- Sends connection termination command
-- Input
--   sock: server socket
-- Returns
--   msg: a toolkit message describing the error. nil if successfull
--   smsg: a server generated error message.
-----------------------------------------------------------------------------
local send_quit = function (sock)
    local err = %send_command(sock, "QUIT")
    if not err then
        local code, smsg = %get_reply(sock)
        if code == 221 then
            return nil, nil
        else
            return %error_msg(%UNKNOWN_ERR, code), smsg
        end
    else
        return %error_msg(%TIMEOUT_ERR), nil
    end
    return nil, nil
end

-----------------------------------------------------------------------------
-- Sends sender command
-- Input
--   sock: server socket
-- Returns
--   msg: a toolkit message describing the error. nil if successfull
--   smsg: a server generated error message.
-----------------------------------------------------------------------------
local send_mail = function (sock, sender)
    local param = format("FROM:<%s>", sender)
    local err = %send_command(sock, "MAIL", param)
    if not err then
        local code, smsg = %get_reply(sock)
        if code == 250 then
            return nil, nil
        elseif (code == 500) or (code == 501) then
            return %error_msg(%SYNTAX_ERR.." (sender error)", code), 
                smsg
        elseif (code == 451) or (code == 452) or (code == 552) then
            return %error_msg(%ABORTED_ERR, code), smsg
        elseif code == 421 then
            return %error_msg(%UNAVAILABLE_ERR, code), smsg
        elseif not code then
            return %error_msg(%TIMEOUT_ERR), nil
        else
            return %error_msg(%UNKNOWN_ERR, code), smsg
        end
    else
        return %error_msg(%TIMEOUT_ERR), nil
    end
    return nil, nil
end

-----------------------------------------------------------------------------
-- Sends message mime headers and body
-- Input
--   sock: server socket
--   mime: table containing all mime headers to be sent
--   body: message body
-- Returns
--   err: true if error condition
--   msg: a toolkit message describing the error.
--   smsg: a server generated error message.
-----------------------------------------------------------------------------
local send_data = function (sock, mime, body)
    local err, smsg, code
    body = gsub(body, "\n%.", "\n%.%.")
    err = %send_command(sock, "DATA")
    if not err then
        code, smsg = %get_reply(sock)
        if code == 354 then
        	err = %send_mime(sock, mime)
            if err then
                return %error_msg(%TIMEOUT_ERR), nil
            end
            err = %puts(sock, body)
            if err then
                return %error_msg(%TIMEOUT_ERR), nil
            end
            err = %puts(sock, ".")
            if err then
                return %error_msg(%TIMEOUT_ERR), nil
            end
            code, smsg = %get_reply(sock)
            if code == 250 then
                return nil, nil
            elseif code == 552 then
                return %error_msg(%OVERFLOW_ERR, code), smsg
            else
                return %error_msg(%UNKNOWN_ERR, code), smsg
            end
        elseif code == 421 then
            return %error_msg(%UNAVAILABLE_ERR, code), smsg
        else
            return %error_msg(%UNKNOWN_ERR, code), smsg
        end
    end
end

-----------------------------------------------------------------------------
-- Sends recipient list command
-- Input
--   sock: server socket
--   from: message sender 
--   rcpt: lua table with recipient list
--   mime: table containing all mime headers to be sent
--   body: message body
-- Returns
--   err: true if error condition
--   msg: a toolkit message describing the error.
--   smsg: a server generated error message.
-----------------------------------------------------------------------------
local send_rcpt = function (sock, from, rcpt, mime, body)
    local i, to, code, msg, smsg, err
    i, to = next(rcpt, i)
    while i do
        err = %send_command(sock, "RCPT", format("TO:<%s>", to))
        if not err then
            code, smsg = %get_reply(sock)
            if (code ~= 250) and (code ~= 251) and (code ~= 550) then
                if code == 552 then
                    msg, smsg = %send_data(sock, mime, body)
                    if not msg then
                        msg, smsg = %send_mail(sock, from)
                        if msg then
                            return msg, smsg
                        end
                    else
                        return msg, smsg
                    end
                else
                    return %error_msg(%UNKNOWN_ERR), smsg
                end
            else
                i, to = next(rcpt, i)
            end
        else
            return %error_msg(%TIMEOUT_ERR), nil
        end
    end
    return nil, nil
end

-----------------------------------------------------------------------------
-- Sends verify recipient command
-- Input
--   sock: server socket
--   user: user to be verified
-- Returns
--   msg: a toolkit message describing the error. nil if successfull
--   smsg: a server generated error message.
-----------------------------------------------------------------------------
local send_vrfy = function (sock, user)
    local err = %send_command(sock, "VRFY", format("<%s>", user))
    local code, smsg
    if not err then
        code, smsg = %get_reply(sock)
        if (code >= 250) and (code < 300) then
            return nil, nil
        else
            return %error_msg(%UNKNOWN_ERR, code), smsg
        end
    else
        return %error_msg(%TIMEOUT_ERR), nil
    end
end

-----------------------------------------------------------------------------
-- Verify user list
-- Input
--   rcpt: recipient list
--   server: mail server address
-- Returns
--   vrfy: verified addresses
--   failed: verified addresses
--   msg: a toolkit message describing the error. nil if successfull
--   smsg: a server generated error message.
-----------------------------------------------------------------------------
local verifyex = function (rcpt, server)
    local vrfy = {}
    local failed = {}
    local v = 1
    local f = 1
    local msg
    -- Connects to server
    local sock, smsg = %connect(server, %PORT)
    if not sock then
        return nil, rcpt, %CONNECT_ERR, smsg
    end
    -- Sets timeout
    sock:timeout(%TIMEOUT)
    -- Initial server greeting
    msg, smsg = %get_helo(sock)
    if msg then
        sock:close()
        return nil, rcpt, msg, smsg
    end
    -- HELO
    msg, smsg = %send_helo(sock)
    if msg then
        sock:close()
        return nil, rcpt, msg, smsg
    end
    -- VRFYs
    local r = next(rcpt, nil)
    while r do
        msg = %send_vrfy(sock, rcpt[r])
        if not msg then
            vrfy[v] = rcpt[r]
            v = v + 1
        else
            failed[f] = rcpt[r]
            f = f + 1
        end
        r = next(rcpt, r)
    end
    if not v then vrfy = nil end
    if not f then failed = nil end
    -- QUIT
    msg, smsg = %send_quit(sock)
    if msg then
        sock:close()
        return vrfy, failed, msg, smsg
    end
    sock:close()
    return vrfy, failed, nil, nil
end

-----------------------------------------------------------------------------
-- Main mail function
-- Input
--   from: message sender
--   rcpt: table containing message recipients
--   mime: table containing mime headers
--   body: message body
--   server: smtp server to be used
-- Returns
--   msg: a toolkit message describing the error. nil if successfull
--   smsg: a server generated error message.
-----------------------------------------------------------------------------
local mailex = function (from, rcpt, mime, body, server)
    -- Connects to server
    local sock, smsg = %connect(server, %PORT)
    local msg
    if not sock then
        return %CONNECT_ERR, smsg
    end
    -- Sets timeout
    sock:timeout(%TIMEOUT)
    -- Initial server greeting
    msg, smsg = %get_helo(sock)
    if msg then
        sock:close() 
        return msg, smsg
    end
    -- HELO
    msg, smsg = %send_helo(sock)
    if msg then
        sock:close() 
        return msg, smsg
    end

    -- MAIL
    msg, smsg = %send_mail(sock, from)
    if msg then
        sock:close() 
        return msg, smsg
    end
    -- RCPT
    msg, smsg = %send_rcpt(sock, from, rcpt, mime, body)
    if msg then
        sock:close() 
        return msg, smsg
    end
    -- DATA
    msg, smsg = %send_data(sock, mime, body)
    if msg then
        sock:close() 
        return msg, smsg
    end

    -- QUIT
    msg, smsg = %send_quit(sock)
    if msg then
        sock:close() 
        return msg, smsg
    end
    sock:close() 
    return nil, nil
end

function mailsendex(sock, from, rcpt, mime, body)
    -- MAIL
    msg, smsg = %send_mail(sock, from)
    if msg then
        return msg, smsg
    end
    -- RCPT
    msg, smsg = %send_rcpt(sock, from, rcpt, mime, body)
    if msg then
        return msg, smsg
    end
    -- DATA
    msg, smsg = %send_data(sock, mime, body)
    if msg then
        return msg, smsg
    end
end

function mailconnect(server)
    -- Connects to server
    local sock, smsg = %connect(server, %PORT)
    local msg
    if not sock then
        return nil, %CONNECT_ERR, smsg
    end
    -- Sets timeout
    sock:timeout(%TIMEOUT)
    -- Initial server greeting
    msg, smsg = %get_helo(sock)
    if msg then
        sock:close() 
        return nil, msg, smsg
    end
    -- HELO
    msg, smsg = %send_helo(sock)
    if msg then
        sock:close() 
        return nil, msg, smsg
    end
	return sock, nil, nil
end

function mailclose(sock)
    -- QUIT
    msg, smsg = %send_quit(sock)
    if msg then
        sock:close() 
        return msg, smsg
    end
    sock:close() 
    return nil, nil
end

-----------------------------------------------------------------------------
-- Converts a comma separated list into a Lua table with one entry for each
-- list element.
-- Input
--   str: string containing the list to be converted
--   tab: table to be filled with entries
-- Returns
--   a table t, where t.n is the number of elements with an entry t[i] 
--   for each element
-----------------------------------------------------------------------------
local fill = function (str, tab)
	gsub(str, "([^%s,]+)", function (w) tinsert(%tab, w) end)
	return tab
end

-----------------------------------------------------------------------------
-- Client mail function, implementing the old interface
-----------------------------------------------------------------------------
function mail(msg)
  local rcpt = {}
  local mime = {}

  mime["Subject"] = msg.subject
  mime["To"] = msg.to
  mime["From"] = msg.from
  
  %fill(msg.to, rcpt)
  if msg.cc then 
    %fill(msg.cc, rcpt) 
    mime["Cc"] = msg.cc
  end
  if msg.bcc then
    %fill(msg.bcc, rcpt)
  end
  rcpt.n = nil
	
  return %mailex(msg.from, rcpt, mime, msg.message, msg.mailserver)
end

function mailsend(sock, msg)
  local rcpt = {}
  local mime = {}

  mime["Subject"] = msg.subject
  mime["To"] = msg.to
  mime["From"] = msg.from
  
  %fill(msg.to, rcpt)
  if msg.cc then 
    %fill(msg.cc, rcpt) 
    mime["Cc"] = msg.cc
  end
  if msg.bcc then
    %fill(msg.bcc, rcpt)
  end
  rcpt.n = nil
	
  return %mailsendex(sock, msg.from, rcpt, mime, msg.message)
end

