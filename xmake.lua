add_rules("mode.debug", "mode.release")

add_repositories("liteldev-repo https://github.com/LiteLDev/xmake-repo.git")
add_repositories("yyz-repo https://github.com/yangyangzhong82/xmake-repo.git")
-- add_requires("levilamina x.x.x") for a specific version
-- add_requires("levilamina develop") to use develop version
-- please note that you should add bdslibrary yourself if using dev version
if is_config("target_type", "server") then
    add_requires("levilamina 1.3.3", {configs = {target_type = "server"}})
else
    add_requires("levilamina", {configs = {target_type = "client"}})
end

add_requires("levibuildscript")
add_requires("sqlite3")
add_requires("drogon")
add_requires("nlohmann_json")
add_requires("legacyremotecall")
add_requires("postgresql")
add_requires("mysql")
if not has_config("vs_runtime") then
    set_runtimes("MD")
end

option("target_type")
    set_default("server")
    set_showmenu(true)
    set_values("server", "client")
option_end()

target("Bedrock-Authority") -- Change this to your mod name.
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")
    add_cxflags( "/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
    -- Add BA_EXPORTS to enable dllexport/dllimport macros
    add_defines("NOMINMAX", "UNICODE", "BA_EXPORTS")
    add_packages("levilamina","sqlite3","mysql" ,"legacyremotecall","postgresql","drogon","nlohmann_json")
    set_exceptions("none") -- To avoid conflicts with /EHa.
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    add_includedirs("src")
    -- add_includedirs(package.includedirs("mariadb-connector-c"))
    -- if is_config("target_type", "server") then
    --     add_includedirs("src-server")
    --     add_files("src-server/**.cpp")
    -- else
    --     add_includedirs("src-client")
    --     add_files("src-client/**.cpp")
    -- end
        after_build(function (target)
        local bindir = path.join(os.projectdir(), "bin")
        local includedir = path.join(bindir, "include", "Bedrock-Authority") -- 修改目标包含目录
        local libdir = path.join(bindir, "lib")
        os.mkdir(includedir)
        os.mkdir(libdir)
        os.cp(path.join(os.projectdir(), "src", "permission", "**.h"), includedir)
        os.cp(path.join(target:targetdir(), target:name() .. ".lib"), libdir)
        end)
