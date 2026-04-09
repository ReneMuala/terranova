add_rules("mode.debug", "mode.release")
add_requires("fmt", "drogon", "glog", "iguana", "sqlitecpp", "yyjson", "uriparser")

target("terranova")
set_kind("binary")
add_defines("KDL_STATIC_LIB", "KDLPP_STATIC_LIB",[[TERRANOVA_VERSION="0.2.1 Pontagea/Beira"]])
set_languages("c++23")
add_includedirs("deps/ckdl/include", "deps/ckdl/bindings/cpp/include")
add_packages("fmt", "drogon", "glog", "iguana", "sqlitecpp", "yyjson", "uriparser")
add_files("src/*.cpp", "deps/ckdl/src/*.c",
    "deps/ckdl/bindings/cpp/src/*.cpp")
add_files("src/docs.html",{rule = "utils.bin2c"})
if is_plat("windows") then
add_cxxflags("/bigobj")
end
after_load(function(target)
    local version = "0.9.27"
    local downloaded_file = "deps/tcc/tcc-" .. version .. ".tar.bz2"
    if not os.exists(downloaded_file) then
        import("net.http")
        print("[tcc not found, downloading]")
        local download_url = "https://download-mirror.savannah.gnu.org/releases/tinycc/tcc-" .. version .. ".tar.bz2"
        http.download(download_url, downloaded_file)
    end
    local extracted_dir_root = "deps/tcc/"
    local extracted_dir = path.join(extracted_dir_root, "tcc-" .. version)
    if not os.exists(extracted_dir) then
        print("[tcc not extracted, extracting]")
        import("utils.archive")
        archive.extract(downloaded_file, extracted_dir_root)
    end
    local config_file = path.join(extracted_dir, "config.h")
    if not os.exists(config_file) then
        print("[tcc not configured, configuring]")
        if is_plat("windows") then
            local file_contents = [[
                #define TCC_VERSION "]] .. version .. [["
                #ifdef TCC_TARGET_X86_64
                #define TCC_LIBTCC1 "libtcc1-64.a"
                #else
                #define TCC_LIBTCC1 "libtcc1-32.a"
                #endif
            ]]
            io.writefile(config_file, file_contents)
        else 
            local old = os.cd(extracted_dir)
            os.exec("./configure")
            os.cd(old)
        end
    end
    target:add("includedirs", extracted_dir)
    target:add("files", path.join(extracted_dir, "libtcc.c"))
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
