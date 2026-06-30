local m = {}
m._VERSION = "1.1.0-dev"

m.exepath = nil
m.options = ""
m.rootoptions = ""

-- Premake module for CxxTest support (http://cxxtest.com/).
-- The module can be used for generating a root file (that contains the entrypoint
-- for the test executable) and source files for each test header.

-- Set the executable path for cxxtestgen
function m.setpath(exepath)
	m.exepath = path.getabsolute(exepath)
end

-- Pass all the necessary options to cxxtest (see http://cxxtest.com/guide.html)
-- for a reference of available options, that should eventually be implemented in
-- this module.
function m.init(have_std, have_eh, runner, includes, root_includes)

	if have_std then
		m.options = m.options.." --have-std"
	end

	if have_eh then
		m.options = m.options.." --have-eh"
	end

	m.rootoptions = m.options
	for _,includefile in ipairs(root_includes) do
		m.rootoptions = m.rootoptions.." --include="..includefile
	end

	for _,includefile in ipairs(includes) do
		m.options = m.options.." --include="..includefile
	end

	-- With gmake, create a Utility project that generates the test root file
	-- This is a workaround for https://github.com/premake/premake-core/issues/286
	if _ACTION == "gmake" then
		project "cxxtestroot"
		kind "Makefile"

		targetdir "%{wks.location}/generated"
		targetname "test_root.cpp"

		-- Note: this command is not silent and clutters the output
		-- Reported upstream: https://github.com/premake/premake-core/issues/954
		buildmessage 'Generating test root file'
		buildcommands {
			"{MKDIR} %{wks.location}/generated",
			m.exepath .. " --root " .. m.rootoptions .. " --template ../../../source/CxxTestRunner.tpl -o %{wks.location}/generated/test_root.cpp"
		}
		cleancommands { "{DELETE} %{wks.location}/generated/test_root.cpp" }
	end
end

-- Populate the test project that was created in premake5.lua.
function m.configure_project(hdrfiles)

	-- Generate the root file, or make sure the utility for generating
	-- it is a dependancy with gmake.
	if _ACTION == "gmake" then
		dependson { "cxxtestroot" }
	else
		prebuildmessage 'Generating test root file'
		prebuildcommands {
			"{MKDIR} %{wks.location}/generated",
			m.exepath .. " --root " .. m.rootoptions .. " --template ../../../source/CxxTestRunner.tpl -o %{wks.location}/generated/test_root.cpp"
		}
	end

	-- Add headers
	for _,hdrfile in ipairs(hdrfiles) do
		files { hdrfile }
	end

	-- Generate the source files from headers
	-- This doesn't work with xcode, see https://github.com/premake/premake-core/issues/940
	filter { "files:**.h", "files:not **precompiled.h" }
		buildmessage 'Generating %{file.basename}.cpp'
		buildcommands {
			"{MKDIR} %{wks.location}/generated",
			m.exepath.." --part "..m.options.." -o %{wks.location}/generated/%{file.basename}.cpp %{file.relpath}"
		}
		buildoutputs { "%{wks.location}/generated/%{file.basename}.cpp" }
	filter {}

	-- Add source files
	files { "%{wks.location}/generated/test_root.cpp" }
	if not (_ACTION == "gmake") then
		for _,hdrfile in ipairs(hdrfiles) do
			local srcfile = "%{wks.location}/generated/".. path.getbasename(hdrfile) .. ".cpp"
			files { srcfile }
		end
	end
end

return m
