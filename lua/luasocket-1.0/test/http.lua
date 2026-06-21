function get_message(c)
	local l, e 
	l, e = c:receive()
	print(l)
	if not e then
		if strfind(l, " 200 ") then
			return 1
		elseif strfind(l, " 301 ") then
			return 1
		else
			return nil
		end
	else
		return nil
	end
end

function get_mime(c, mime)
	local l, e
	local h, v
	local i, j
	repeat
		l, e = c:receive()
		if e or l == "" then
			return mime
		end
		i, j, h = strfind(l, "(.-):")
		h = strlower(h)
		i, j, v =  strfind(l, ":%s*(.*)")
		v = strsub(l, i+2, j)
		if mime[h] then
			mime[h] = format("%s,%s",mime[h],v)
		else
			mime[h] = v
		end
	until nil
end

function get_body(c, mime)
	local b, e, l
	if mime["transfer-encoding"] == "chunked" then
		local s
		b = ""
		l, e = c:receive()
		s = gsub(l, ";.*", "")
		s = tonumber(s, 16)
		while s > 0 do
			b = b .. c:receive(s); c:receive()
			l, e = c:receive()
			s = gsub(l, ";.*", "")
			s = tonumber(s, 16)
		end
		get_mime(c, mime)
		return b
	elseif mime["content-length"] then
		return c:receive(tonumber(mime["content-length"]))
	else 
		b, e = c:receive() 
		while not e do
			l, e = c:receive() 
			b = format("%s\n%s", b, l)
		end
		return b
	end
end

function split_url(url)
	local h, r, i, j
	url = gsub(url, "^http://", "")
	h = gsub(url, "/.*", "")
	i, j = strfind(url, h, 1, 1)
	r = strsub(url, j+1, -1)
	if r == "" then r = "/" end
	return h, r
end

function get_url(url)
	local h, r
	local c, l, mime, body
	h, r = split_url(url)
	c = connect(h, 80)
	if not c then return nil, nil end
	c:send("GET " .. r .. " HTTP/1.1\n")
	c:send("Connection: close\n")
	c:send("Host: ", h, "\n\n")
	if get_message(c) then
		mime = {}
		get_mime(c, mime)
		body = get_body(c, mime)
	end
	c:close()
	return body, mime
end

b,h = get_url("www.tecgraf.puc-rio.br/")
print(b)
