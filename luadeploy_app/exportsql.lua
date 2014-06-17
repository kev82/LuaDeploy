function app.releaseApp(sqlfile, softwarename, newsoftwarename)
	local luadeploy
	do
		local ok, rv = pcall(require, "luadeploy")
		if not ok then 
			print("Unable to find luadeploy module")
			return
		end
		luadeploy = rv
	end

	local sql
	do
		local fileh = io.open(sqlfile)
		sql = fileh:read("*a")
		fileh:close()
	end

	local appdb = luadeploy.openSQLString(sql)

	appdb:exportSoftware(softwarename, newsoftwarename)
end
