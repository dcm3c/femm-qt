s = bind("localhost", 2000)
print("Waiting connection from talker...")
c = s:accept()
print("Connected. Here is the stuff:")
l, e = c:receive()
while not e do
	print(l)
	l, e = c:receive()
end
