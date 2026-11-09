core_test("CoreModpackManifestTests", "CommonwealthOnlineCoreModpackManifestTests", "F4TModpackManifestTests.cpp")
core_test("CoreNexusSourcesTests", "CommonwealthOnlineCoreNexusSourcesTests", "F4TNexusSourcesTests.cpp")
core_test("CoreWindowsCaseFoldTests", "CommonwealthOnlineCoreWindowsCaseFoldTests", "F4TWindowsCaseFoldTests.cpp")
if is_host("windows") then
    core_test("CoreModpackMismatchReportTests", "CommonwealthOnlineCoreModpackMismatchReportTests", "F4TModpackMismatchReportTests.cpp")
end
if is_host("windows") then
    core_test("CoreModpackProbeBudgetTests", "CommonwealthOnlineCoreModpackProbeBudgetTests", "F4TModpackProbeBudgetTests.cpp")
    core_test("CoreModpackProbeTests", "CommonwealthOnlineCoreModpackProbeTests", "F4TModpackProbeTests.cpp")
    core_test("CoreModpackProbeCacheTests", "CommonwealthOnlineCoreModpackProbeCacheTests", "F4TModpackProbeCacheTests.cpp")
    core_test("CoreModpackNeedTests", "CommonwealthOnlineCoreModpackNeedTests", "F4TModpackNeedTests.cpp")
    core_test("CoreModpackInstallTests", "CommonwealthOnlineCoreModpackInstallTests", "F4TModpackInstallTests.cpp")
    core_test("CoreModpackAutoFixTests", "CommonwealthOnlineCoreModpackAutoFixTests", "F4TModpackAutoFixTests.cpp")
end
core_test("CoreModpackChunksTests", "CommonwealthOnlineCoreModpackChunksTests", "F4TModpackChunksTests.cpp")
core_test("CoreModpackContentPolicyTests", "CommonwealthOnlineCoreModpackContentPolicyTests", "F4TModpackContentPolicyTests.cpp")
core_test("CoreModpackDownloadTests", "CommonwealthOnlineCoreModpackDownloadTests", "F4TModpackDownloadTests.cpp")
core_test("CorePluginsTxtTests", "CommonwealthOnlineCorePluginsTxtTests", "F4TPluginsTxtTests.cpp")
core_test("CoreBa2InspectTests", "CommonwealthOnlineCoreBa2InspectTests", "F4TBa2InspectTests.cpp")
core_test("CoreModpackServeTests", "CommonwealthOnlineCoreModpackServeTests", "F4TModpackServeTests.cpp")
