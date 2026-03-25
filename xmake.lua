add_rules("mode.debug", "mode.release")
add_requires("fmt", "drogon", "glog", "iguana", "sqlitecpp", "tinycc", "yyjson", "uriparser")
target("terranova")
    set_kind("binary")
    add_defines("KDL_STATIC_LIB", "KDLPP_STATIC_LIB")
    set_languages("c++23")
    add_includedirs("deps/ckdl/include","deps/ckdl/bindings/cpp/include")
    add_packages( "fmt", "drogon", "glog", "iguana", "sqlitecpp", "tinycc", "yyjson", "uriparser")
    add_files("src/*.cpp","deps/ckdl/src/*.c",
    "deps/ckdl/bindings/cpp/src/*.cpp")
    after_install(function(target)
local pkg = target:pkgs()["tinycc"]
            if pkg then
                local tcc_libtccdll = path.join(pkg:installdir(), 'bin', 'libtcc.dll')
                local local_libtccdll_dir = path.join(target:installdir(), 'bin', 'libtcc.dll')
                os.cp(tcc_libtccdll,local_libtccdll_dir)
            else
                print("tinycc was not found")
                os.exit(1)
            end
    end)
    after_load(function(target)
            local pkg = target:pkgs()["tinycc"]
            if pkg then
                target:add("includedirs", path.join(pkg:installdir(), "bin"))
                target:add("linkdirs", path.join(pkg:installdir(), "bin"))
                target:add("links", "libtcc")
                 --[[local tcc_lib_dir = path.join(pkg:installdir(), 'lib')
                 local local_lib_dir = path.join(target:targetdir(), 'lib')
                 -- target:add("defines", 'TCC_LIB_PATH="' .. libpath:gsub("\\", "/") .. '"')
                 if not os.exists(local_lib_dir) then
                    os.cp(tcc_lib_dir,local_lib_dir)
                 end ]]
                local tcc_libtccdll = path.join(pkg:installdir(), 'bin', 'libtcc.dll')
                local local_libtccdll_dir = path.join(target:targetdir(), 'libtcc.dll')
                if not os.exists(local_libtccdll_dir) then
                    os.cp(tcc_libtccdll,local_libtccdll_dir)
                end
            else
                print("tinycc was not found")
                os.exit(1)
            end
        end)

--
-- If you want to known more usage about xmake, please see https://xmake.io
--
-- ## FAQ
--
-- You can enter the project directory firstly before building project.
--
--   $ cd projectdir
--
-- 1. How to build project?
--
--   $ xmake
--
-- 2. How to configure project?
--
--   $ xmake f -p [macosx|linux|iphoneos ..] -a [x86_64|i386|arm64 ..] -m [debug|release]
--
-- 3. Where is the build output directory?
--
--   The default output directory is `./build` and you can configure the output directory.
--
--   $ xmake f -o outputdir
--   $ xmake
--
-- 4. How to run and debug target after building project?
--
--   $ xmake run [targetname]
--   $ xmake run -d [targetname]
--
-- 5. How to install target to the system directory or other output directory?
--
--   $ xmake install
--   $ xmake install -o installdir
--
-- 6. Add some frequently-used compilation flags in xmake.lua
--
-- @code
--    -- add debug and release modes
--    add_rules("mode.debug", "mode.release")
--
--    -- add macro definition
--    add_defines("NDEBUG", "_GNU_SOURCE=1")
--
--    -- set warning all as error
--    set_warnings("all", "error")
--
--    -- set language: c99, c++11
--    set_languages("c99", "c++11")
--
--    -- set optimization: none, faster, fastest, smallest
--    set_optimize("fastest")
--
--    -- add include search directories
--    add_includedirs("/usr/include", "/usr/local/include")
--
--    -- add link libraries and search directories
--    add_links("tbox")
--    add_linkdirs("/usr/local/lib", "/usr/lib")
--
--    -- add system link libraries
--    add_syslinks("z", "pthread")
--
--    -- add compilation and link flags
--    add_cxflags("-stdnolib", "-fno-strict-aliasing")
--    add_ldflags("-L/usr/local/lib", "-lpthread", {force = true})
--
-- @endcode
--

