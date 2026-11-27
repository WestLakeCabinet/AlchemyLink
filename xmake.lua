add_rules("mode.debug", "mode.release")

set_languages("cxx23")

add_requires("commonlibsse-ng", "boost")

target("SkyPotionSKSE")
    set_kind("shared")
    add_rules("commonlibsse-ng.plugin", {
        name = "SkyPotionSKSE",
        author = "Dolzzo",
        description = "Skyrim SE/AE WebSocket Plugin for SkyPotion"
    })
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    add_packages("commonlibsse-ng", "boost")
    add_syslinks("ws2_32")
