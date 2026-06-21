-----------------------------------------------------------------------------
-- NetLib automated test module
-- command.lua
-- To make sure the client and server are consistend, this module is
-- responsible for the command exchange
-----------------------------------------------------------------------------

-----------------------------------------------------------------------------
-- Global variables
-----------------------------------------------------------------------------
SEND_BLOCK = "SNDBL"
SEND_LINE = "SNDLN"
RECEIVE_LINE = "RCVLN"
RECEIVE_BLOCK = "RCVBL"
CLOSE = "CLOSE"
CONNECT = "CNECT"
SLEEP = "SLEEP"
EXIT = "EXIT0"
ECHO_BLOCK = "ECOBL"
ECHO_LINE = "ECOLN"
ECHO_TIMEOUT = "ECOTM"
COMMAND = "COMND"
SYNC = "SYNCR"

HOST = "localhost"
PORT = 2020

-----------------------------------------------------------------------------
-- Tests if library is in _DEBUG mode
-----------------------------------------------------------------------------
function test_debug_mode()
	if not sleep or not time then
		print("_DEBUG not defined at library compilation!")
		exit(1)
	end
end

-----------------------------------------------------------------------------
-- Sends a command string through the command socket
-- Input
--   cmd: command to be sent
--   par: command parameters, if needed
-----------------------------------------------------------------------------
function send_command(cmd, par)
    if (cmd == RECEIVE_BLOCK) or (cmd == ECHO_BLOCK) then
        c:send(cmd,format("%10d", par))
    elseif (cmd == SLEEP) then
        c:send(cmd,format("%10d", par))
    else
        c:send(cmd)
    end
end

-----------------------------------------------------------------------------
-- Prints out a command 
-- Input
--   cmd: command to be sent
--   par: command parameters, if needed
-----------------------------------------------------------------------------
function print_command(cmd, par)
    if (cmd == RECEIVE_BLOCK) or (cmd == ECHO_BLOCK) then
        write(cmd, ": ", par, ";\n")
    elseif (cmd == SLEEP) then
        write(cmd, ": ", par, ";\n")
    else
        write(cmd, ";\n")
    end
end

-----------------------------------------------------------------------------
-- Reads a command and it's parameters from the command socket
-- Returns
--   the command followed by any parameters, if needed
-----------------------------------------------------------------------------
function get_command()
    local cmd, err = c:receive(5)
    if err then
print(err)
        return nil, err
    end
    if (cmd == RECEIVE_BLOCK) or (cmd == ECHO_BLOCK) then
        local par, err = c:receive(10)
        if err then
            return nil
        end
		par = tonumber(par)
        return cmd, par
    elseif (cmd == SLEEP) then
        local par, err = c:receive(10)
        if err then
            return nil
        end
		par = tonumber(par)
        return cmd, par
    end
    return cmd
end

-----------------------------------------------------------------------------
-- Prints a failure message and exits the program
-- Input
--   msg: message to be printed
-----------------------------------------------------------------------------
function fail(msg)
    write("FAILED: ", msg, "\n")
    exit(1)
end

-----------------------------------------------------------------------------
-- Prints a success message
-- Input
--   msg: message to be printed, if needed
-----------------------------------------------------------------------------
function pass(msg)
	if not msg then
		print("passed")
	else
		write("passed: ", msg, "\n")
	end
end
