add_rules("mode.debug", "mode.release")

add_repositories("liteldev-repo https://github.com/LiteLDev/xmake-repo.git")
add_repositories("yyz-repo https://github.com/yangyangzhong82/xmake-repo.git")
-- add_requires("levilamina x.x.x") for a specific version
-- add_requires("levilamina develop") to use develop version
-- please note that you should add bdslibrary yourself if using dev version
if is_config("target_type", "server") then
    add_requires("levilamina 1.9.1", {configs = {target_type = "server"}})
else
    add_requires("levilamina", {configs = {target_type = "client"}})
end

add_requires("levibuildscript")
add_requires("sqlite3")
add_requires("legacyremotecall")

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
    add_packages("levilamina","sqlite3","legacyremotecall","drogon")
    -- Fix jsoncpp conflict: bedrock_runtime_api.lib contains partial jsoncpp symbols that conflict with drogon's jsoncpp
    -- Solution: Remove jsoncpp from package links before prelink, then add it back after prelink
    on_config(function(target)
        -- Remove jsoncpp from package links to prevent prelink.exe from processing it
        for _, pkg in pairs(target:pkgs()) do
            local links = pkg:get("links")
            if links then
                local newlinks = {}
                for _, link in ipairs(links) do
                    if link ~= "jsoncpp" then
                        table.insert(newlinks, link)
                    end
                end
                pkg:set("links", newlinks)
            end
        end
    end)
    -- After prelink generates bedrock_runtime_api.lib, add jsoncpp.lib back for final linking
    after_link(function(target)
        -- jsoncpp will be linked via linkdirs already set by the package
    end)
    before_link(function(target)
        -- Re-add jsoncpp for final link without hardcoded local package path.
        target:add("links", "jsoncpp", {public = true})
    end)
    set_exceptions("none") -- To avoid conflicts with /EHa.
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    add_includedirs("src")
    if is_config("target_type", "server") then
        add_defines("LL_PLAT_S")
    else
        add_defines("LL_PLAT_C")
    end
        after_build(function (target)
        local bindir = path.join(os.projectdir(), "bin")
        local includedir = path.join(bindir, "include", "Bedrock-Authority") -- 修改目标包含目录
        local libdir = path.join(bindir, "lib")
        os.mkdir(includedir)
        os.mkdir(libdir)
        os.mkdir(path.join(includedir, "permission"))
        os.cp(path.join(os.projectdir(), "src", "permission", "*.h"), path.join(includedir, "permission"))
        os.cp(path.join(os.projectdir(), "src", "permission", "events", "*.h"), path.join(includedir, "permission", "events"))
        os.cp(path.join(target:targetdir(), target:name() .. ".lib"), libdir)
        end)
