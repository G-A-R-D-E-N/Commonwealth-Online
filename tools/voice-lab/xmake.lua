set_project("voice-lab")
set_languages("c++23")
set_warnings("allextra")
set_allowedplats("windows")
add_rules("mode.debug", "mode.release")

add_requires("libopus 1.5.2", {configs = {runtimes = "MD"}})

target("voice-lab")
    set_kind("binary")
    add_files("lab_*.cpp", "../../CO-BETA/plugin/src/F4TVoiceCodec.cpp", "../../CO-BETA/plugin/src/F4TVoiceWasapiCapture.cpp", "../../CO-BETA/plugin/src/F4TVoiceWasapiRender.cpp", "../../co-core/src/protocol/F4TVoiceWire.cpp", "../../co-core/src/protocol/F4TVoiceJitter.cpp")
    add_includedirs("../../CO-BETA/plugin/include", "../../CO-BETA/plugin/src", "../../co-core/include")
    add_packages("libopus")
    add_syslinks("ws2_32", "winmm", "psapi", "ole32", "avrt")
