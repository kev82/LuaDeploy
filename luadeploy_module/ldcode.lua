--[[
/******************************************************************************
* Copyright (C) 2014, Kevin Martin (kev82@khn.org.uk)
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************/
--]]
local int_module = ...

local module = {}

module.openSQLString = int_module.openSQLString
module.openDBFile = int_module.openDBFile

do
	local sodir_mt = {}

	function sodir_mt:__gc()
		os.execute(string.format("rm %s/*", self._dirname))
		os.execute(string.format("rmdir %s", self._dirname))
	end

	function sodir_mt:__tostring()
		return self._dirname
	end

	function module.tmpsodir(dir)
		local rv = {
		 _dirname = dir:gsub("{pid}", tostring(int_module.getpid()))
		}

		os.execute("mkdir " .. rv._dirname)
		return setmetatable(rv, sodir_mt)
	end
end

function module.startServer(sname, software, db, path)
	local rv = int_module.createServer(sname, software, db, tostring(path))
	rv:start()
	return rv
end

module.newState = int_module.newState

do
	local s = int_module.newState({})
	local m = getmetatable(s)

	function m:runCode(code)
		self:pushCode(code)
		self:run()
	end

	function m:runSearch(server, codetype, request)
		self:pushSearch(server, codetype, request)
		self:run()
	end
end

function module.newSearcher(servername, datatype)
	return function(x)
		return int_module.sendRequest(servername, datatype, x)
	end
end

return module
