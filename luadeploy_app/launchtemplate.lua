function app.makelauncher(sqlfile, softwarename, outputfile)
	do
		local cmd = string.format("readlink -e -n %s", sqlfile)
		local fileh = io.popen(cmd, "r")
		sqlfile = fileh:read("*a")
		fileh:close()
	end


	assert(sqlfile:sub(1,1) == "/", "SQL file must exist")
	assert(type(softwarename) == "string", "Must specify software name")
	assert(type(outputfile) == "string", "Must specify output file")

	local fileh = io.open(outputfile, "w+")
	local function writeln(x)
		fileh:write(x)
		fileh:write("\n")
	end
		
	writeln([[#!/usr/bin/env lua]])
	writeln(string.format([[local sqlfilename = "%s"]], sqlfile))
	writeln(string.format([[local softwarename = "%s"]], softwarename))
	writeln(launcher_template_file)

	fileh:close()

	os.execute(string.format([[chmod 755 "%s"]], outputfile))
end
