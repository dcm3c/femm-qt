c = connect("localhost", 2000)
if not c then
	print("Unable to connect to listener!")
	exit()
end
print("Connected! Please type stuff (empty line to stop):")
l = read()
while l ~= "" and not e do
	e = c:send(l, "\n")
	l = read()
end
