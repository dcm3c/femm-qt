-----------------------------------------------------------------------------
-- NetLib automated test module
-- server.lua
-- This is the server module. It's completely controled by the client module
-- by the use of a control connection.
-----------------------------------------------------------------------------

-----------------------------------------------------------------------------
-- Read command definitions
-----------------------------------------------------------------------------
dofile("command.lua")
test_debug_mode()

-----------------------------------------------------------------------------
-- Bind to address and wait for control connection
-----------------------------------------------------------------------------
s, err = bind(HOST, PORT)
if err then
	print(err)
	exit(1)
end
print("server: waiting for control connection...")
c = s:accept()
print("server: control connection stablished!")

-----------------------------------------------------------------------------
-- Executes a command, detecting any possible failures
-- Input
--   cmd: command to be executed
--   par: command parameters, if needed
-----------------------------------------------------------------------------
function execute_command(cmd, par)
	if cmd == CONNECT then
		print("server: waiting for data connection...")
		d = s:accept()
		print("server: data connection stablished!")
	elseif cmd == CLOSE then
		print("server: closing connection with client...")
		d:close()
		d = nil
	elseif cmd == ECHO_LINE then
		str, err = d:receive()
		if err then fail("server: " .. err) end
		err = d:send(str, "\n")
		if err then fail("server: " .. err) end
	elseif cmd == ECHO_BLOCK then
		str, err = d:receive(par)
		if err then fail("server: " .. err) end
		err = d:send(str)
		if err then fail("server: " .. err) end
	elseif cmd == RECEIVE_BLOCK then
		str, err = d:receive(par)
	elseif cmd == SEND_BLOCK then
		err = d:send(str)
	elseif cmd == ECHO_TIMEOUT then
		str, err = d:receive(par)
		if err then fail("server: " .. err) end
		err = d:send(str)
		if err then fail("server: " .. err) end
	elseif cmd == COMMAND then
		cmd, par = get_command()
		send_command(cmd, par)
	elseif cmd == EXIT then
		print("server: exiting...")
		exit(0)
	elseif cmd == SYNC then
		print("server: synchronizing...")
		send_command(SYNC)
	elseif cmd == SLEEP then
		print("server: sleeping for " .. par .. " seconds...")
		sleep(par)
		print("server: woke up!")
	end
end

-----------------------------------------------------------------------------
-- Loop forever, accepting and executing commands
-----------------------------------------------------------------------------
while 1 do
	cmd, par = get_command()
	if not cmd then fail("server: " .. par) end
	print_command(cmd, par)
	execute_command(cmd, par)
end
