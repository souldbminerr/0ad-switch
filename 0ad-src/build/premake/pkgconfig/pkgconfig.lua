local pkg_config_command = "pkg-config"
if os.getenv("PKG_CONFIG") then
	pkg_config_command = os.getenv("PKG_CONFIG")
end

local pkg_config_path = ""
if os.getenv("PKG_CONFIG_PATH") then
	pkg_config_path = os.getenv("PKG_CONFIG_PATH")
end

local static_link_libs = false


local function os_capture(cmd)
	return io.popen(cmd, 'r'):read('*a'):gsub("\n", " ")
end

local function parse_pkg_config_includes(lib, alternative_cmd, alternative_flags)
	local result
	if not alternative_cmd then
		result = os_capture("PKG_CONFIG_PATH=" .. pkg_config_path .. " " .. pkg_config_command .. " --cflags " .. lib)
	else
		if not alternative_flags then
			result = os_capture(alternative_cmd.." --cflags")
		else
			result = os_capture(alternative_cmd.." "..alternative_flags)
		end
	end

	-- Small trick: delete the space after -include so that we can detect
	-- which files have to be force-included without difficulty.
	result = result:gsub("%-include +(%g+)", "-include%1")
	result = result:gsub("%-isystem +(%g+)", "-isystem%1")

	local dirs = {}
	local files = {}
	local options = {}
	for w in string.gmatch(result, "[^' ']+") do
		if string.sub(w,1,2) == "-I" then
			table.insert(dirs, string.sub(w,3))
		elseif string.sub(w,1,8) == "-isystem" then
			table.insert(dirs, string.sub(w,9))
		elseif string.sub(w,1,8) == "-include" then
			table.insert(files, string.sub(w,9))
		else
			table.insert(options, w)
		end
	end

	return dirs, files, options
end

local function parse_pkg_config_links(lib, alternative_cmd, alternative_flags)
	local result
	if not alternative_cmd then
		local static = static_link_libs and " --static " or ""
		result = os_capture("PKG_CONFIG_PATH=" .. pkg_config_path .. " " .. pkg_config_command .. " --libs " .. static .. lib)
	else
		if not alternative_flags then
			result = os_capture(alternative_cmd.." --libs")
		else
			result = os_capture(alternative_cmd.." "..alternative_flags)
		end
	end

	-- On OSX, wx-config outputs "-framework foo" instead of "-Wl,-framework,foo"
	-- which doesn't fare well with the splitting into libs, libdirs and options
	-- we perform afterwards.
	result = result:gsub("%-framework +(%g+)", "-Wl,-framework,%1")

	local libs = {}
	local dirs = {}
	local options = {}
	for w in string.gmatch(result, "[^' ']+") do
		if string.sub(w,1,2) == "-l" then
			table.insert(libs, string.sub(w,3))
		elseif string.sub(w,1,2) == "-L" then
			table.insert(dirs, string.sub(w,3))
		else
			table.insert(options, w)
		end
	end

	return dirs, libs, options
end

-- ----------------------------------------------------------------------------
-- Public API
-- ----------------------------------------------------------------------------
local m = {}
m._VERSION = "2.0.0"

--- Append path to PKG_CONFIG_PATH
-- @param path string Path to append to PKG_CONFIG_PATH
function m.add_pkg_config_path(path)
	if pkg_config_path == "" then
		pkg_config_path = path
	else
		pkg_config_path = pkg_config_path .. ":" .. path
	end
end

--- Set whether to link libraries statically.
-- @param value boolean True for static linking, false otherwise.
function m.set_static_link_libs(value)
	static_link_libs = value
end

--- Add include directories and build options for a library.
-- @param lib string The library name.
-- @param alternative_cmd string|nil Optional alternative command.
-- @param alternative_flags string|nil Optional alternative flags.
function m.add_includes(lib, alternative_cmd, alternative_flags)
	local dirs, files, options = parse_pkg_config_includes(lib, alternative_cmd, alternative_flags)

	externalincludedirs(dirs)
	forceincludes(files)
	buildoptions(options)
end

--- Add include directories after the default ones (used for overrides).
-- @param lib string The library name.
-- @param alternative_cmd string|nil Optional alternative command.
-- @param alternative_flags string|nil Optional alternative flags.
function m.add_includes_after(lib, alternative_cmd, alternative_flags)
	local dirs, files, options = parse_pkg_config_includes(lib, alternative_cmd, alternative_flags)

	includedirsafter(dirs)
	forceincludes(files)
	buildoptions(options)
end

--- Add library directories, libraries, and link options for a library.
-- @param lib string The library name.
-- @param alternative_cmd string|nil Optional alternative command.
-- @param alternative_flags string|nil Optional alternative flags.
function m.add_links(lib, alternative_cmd, alternative_flags)
	local dirs, libs, options = parse_pkg_config_links(lib, alternative_cmd, alternative_flags)

	libdirs(dirs)
	links(libs)
	linkoptions(options)
end

return m
