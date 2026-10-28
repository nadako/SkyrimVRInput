local version = "0.1.1"

set_project("skyrim_vrinput")
set_version(version)

set_arch("x64")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")
set_defaultmode("releasedbg")

add_rules("plugin.compile_commands.autoupdate")

set_config("rex_ini", true)
set_config("skyrim_se", false)
set_config("skyrim_ae", false)
set_config("skyrim_vr", true)
includes("CommonLibSSE-NG")

target("skyrim_vrinput")
    set_kind("shared")
    add_rules("commonlibsse-ng.plugin", {
        name = "skyrim_vrinput",
        author = "nadako",
        description = "Modern SteamVR Input for Skyrim VR"
    })
    set_pcxxheader("src/pch.h")
    add_files("src/**.cpp")

    after_build(function(target)
        import("utils.archive")

        os.vrunv("python", {path.join(os.projectdir(), "tools/generate_controlmapvr.py")})

        local source_data_dir = path.join(os.projectdir(), "data")
        local source_data_files = os.files(path.join(source_data_dir, "**"))

        local dist_dir = path.absolute(vformat("$(builddir)/dist"))
        local dist_mod_dir = path.join(dist_dir, "SkyrimVRInput")
        local dist_archive = path.join(dist_dir, "SkyrimVRInput-" .. target:version() .. ".zip")
        os.tryrm(dist_dir)

        local target_dirs = {
            --"C:/SteamLibrary/steamapps/common/SkyrimVR/Data",
            "C:/Games/MySkyrimVR/modlist/mods/SkyrimVRInput",
            "C:/Games/SkyrimVRForMod/mods/SkyrimVRInput",
            dist_mod_dir,
        }
        for _, target_dir in ipairs(target_dirs) do
            local target_plugin_dir = path.join(target_dir, "SKSE", "Plugins")
            os.mkdir(target_plugin_dir)
            os.cp(target:targetfile(), target_plugin_dir)
            os.cp(target:symbolfile(), target_plugin_dir)

            for _, file in ipairs(source_data_files) do
                local target_file = path.join(target_dir, path.relative(file, source_data_dir))
                os.mkdir(path.directory(target_file))
                os.cp(file, target_file)
            end
            print("Deployed into " .. target_dir)
        end

        archive.archive(dist_archive, "*", {curdir = dist_mod_dir})
        print("Packaged " .. dist_archive)
    end)
target_end()
