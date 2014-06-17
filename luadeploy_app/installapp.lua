function app.installapp(database, softwarename)
	local luadeploy
	do
		local ok, rv = pcall(require, "luadeploy")
		if not ok then 
			print("Unable to find luadeploy module")
			return
		end
		luadeploy = rv
	end

	local appdb = luadeploy.openDBFile(database)

	local sodir = luadeploy.tmpsodir("/tmp/ldinstall_{pid}")
	appdb:writeSharedObjs(softwarename, sodir)

	local dbserver = luadeploy.startServer("application", softwarename,
	 appdb, sodir)

	local modules = {
	 "base",
	 "package",
	 "coroutine",
	 "table",
	 "io",
	 "os",
	 "string",
	 "bit32",
	 "math",
	 "debug",
	 "ldclient",
	}

	local state = luadeploy.newState(modules)

	state:runCode([[
	package.searchers[#package.searchers+1] = function(x)
		return ldclient.search("application", "module", x)
	end
	]])

	state:runSearch("application", "luadeploy",
	 "install " .. table.concat(arg, " "))
end
