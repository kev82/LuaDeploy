if type(app[arg[1]]) ~= "function" then
	print("No handlers")
	return
end

app[arg[1]](table.unpack(arg, 2))
