using System.Net;
using System.Net.Sockets;
using System.Text.Json;
using CommonwealthOnline.Host.Services;
using CommonwealthOnline.Host.ViewModels;

static void Require(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

static string[] Arguments(System.Diagnostics.ProcessStartInfo info) => info.ArgumentList.ToArray();

static void RequireThrows<TException>(Action action, string message) where TException : Exception
{
    var thrown = false;
    try
    {
        action();
    }
    catch (TException)
    {
        thrown = true;
    }

    Require(thrown, message);
}

static string NativeBridgeName() => OperatingSystem.IsWindows()
    ? "commonwealth_online_gns_bridge.dll"
    : "libcommonwealth_online_gns_bridge.so";

static void CreateNativeLayout(string root, string apphost, string marker, string bridge)
{
    File.WriteAllText(apphost, "native server\n");
    File.WriteAllText(marker, "native-cpp\n");
    File.WriteAllText(Path.Combine(root, NativeBridgeName()), bridge);
    File.WriteAllText(Path.Combine(root, "NATIVE_RUNTIME.txt"), "native runtime\n");
}

static void MakeExecutable(string path)
{
    if (!OperatingSystem.IsWindows())
    {
        File.SetUnixFileMode(path, UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
    }
}

static void RunGracefulStopRegression(string root, string config)
{
    if (OperatingSystem.IsWindows()) return;

    var apphost = Path.Combine(root, "CommonwealthOnline.Server");
    var signal = Path.Combine(root, "signal.txt");
    File.WriteAllText(apphost, $"#!/bin/sh\ntrap 'printf graceful > \"{signal}\"; exit 0' TERM INT\nwhile :; do sleep 1; done\n");
    MakeExecutable(apphost);

    using var controller = new ServerController();
    controller.Start(root, config);
    Require(controller.IsRunning, "graceful-stop regression process did not start");
    controller.Stop();
    Require(!controller.IsRunning, "graceful-stop regression process remained running");
    Require(File.Exists(signal) && File.ReadAllText(signal) == "graceful",
        "Stop bypassed the native server graceful signal");
}

static string FindRepositoryFile(string relativePath)
{
    for (var directory = new DirectoryInfo(Environment.CurrentDirectory);
         directory is not null;
         directory = directory.Parent)
    {
        var path = Path.Combine(directory.FullName, relativePath);
        if (File.Exists(path)) return path;
    }

    throw new InvalidOperationException($"repository file not found: {relativePath}");
}

static void RunWorkflowCleanupRegression()
{
    var workflow = File.ReadAllText(FindRepositoryFile(".github/workflows/co-server-package.yml"));
    const string stepName = "- name: Pack Windows runtime smoke inputs";
    var start = workflow.IndexOf(stepName, StringComparison.Ordinal);
    Require(start >= 0, "Windows smoke publish step is missing");
    var end = workflow.IndexOf("\n      - name:", start + stepName.Length, StringComparison.Ordinal);
    var step = workflow[start..(end < 0 ? workflow.Length : end)];
    Require(step.Contains("if: vars.WINDOWS_RUNTIME_SMOKE == 'true'", StringComparison.Ordinal),
        "Windows smoke publishing must be gated with the runtime smoke condition");
}

static int FreeUdpPort()
{
    using var socket = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
    socket.Bind(new IPEndPoint(IPAddress.Loopback, 0));
    return ((IPEndPoint)socket.LocalEndPoint!).Port;
}

static int RunPackagedControllerSmoke(string packageDir, string configPath)
{
    packageDir = Path.GetFullPath(packageDir);
    configPath = Path.GetFullPath(configPath);
    Require(Directory.Exists(packageDir), $"package directory does not exist: {packageDir}");
    Require(File.Exists(configPath), $"package smoke config does not exist: {configPath}");
    Require(File.Exists(Path.Combine(packageDir, "CommonwealthOnline.Server.native")), "package smoke requires native marker");

    var scratch = Path.Combine(Path.GetTempPath(), "co-host-package-smoke-" + Guid.NewGuid().ToString("N"));
    Directory.CreateDirectory(scratch);
    var smokeConfigPath = Path.Combine(scratch, "commonwealth-server.json");
    var smokeConfig = ServerConfig.Load(configPath);
    smokeConfig.Host = "127.0.0.1";
    smokeConfig.Port = FreeUdpPort();
    smokeConfig.AdminPort = HostTestNetworking.FreeTcpPort();
    while (smokeConfig.AdminPort == smokeConfig.Port) smokeConfig.AdminPort = HostTestNetworking.FreeTcpPort();
    smokeConfig.LanDiscoveryEnabled = false;
    smokeConfig.LanDiscoveryPort = FreeUdpPort();
    while (smokeConfig.LanDiscoveryPort == smokeConfig.Port) smokeConfig.LanDiscoveryPort = FreeUdpPort();
    smokeConfig.RequireV2Identity = true;
    smokeConfig.EnablePersistence = false;
    smokeConfig.PublicListing = false;
    smokeConfig.Save(smokeConfigPath);
    Require(!JsonDocument.Parse(File.ReadAllText(smokeConfigPath)).RootElement.GetProperty("public_listing").GetBoolean(),
        "package smoke must write public_listing false so it never registers with the production directory");

    using var ready = new ManualResetEventSlim(false);
    using var stopped = new ManualResetEventSlim(false);
    var log = new List<string>();
    var controller = new ServerController();
    controller.LogReceived += line =>
    {
        lock (log) log.Add(line);
        if (line.Contains("CommonwealthOnline.Server C++ listening on", StringComparison.Ordinal)) ready.Set();
    };
    controller.RunningChanged += running =>
    {
        if (!running) stopped.Set();
    };

    try
    {
        controller.Start(packageDir, smokeConfigPath);
        if (!ready.Wait(TimeSpan.FromSeconds(10)))
        {
            string text;
            lock (log) text = string.Join(Environment.NewLine, log);
            throw new InvalidOperationException("Host controller did not start packaged native server.\n" + text);
        }
        Require(controller.IsRunning, "Host controller reported stopped after native server became ready");
        controller.Stop();
        Require(stopped.Wait(TimeSpan.FromSeconds(10)), "Host controller did not stop packaged native server");
        Require(log.Any(line => line.Contains("Authenticated admin shutdown requested", StringComparison.Ordinal)),
            "Host controller did not use the authenticated native shutdown path");
        Console.WriteLine("Packaged Host controller native launch smoke passed.");
        return 0;
    }
    finally
    {
        controller.Stop();
        try { Directory.Delete(scratch, recursive: true); } catch { }
    }
}

if (args.Length > 0)
{
    if (args.Length == 3 && args[0] == "--package-smoke")
    {
        return RunPackagedControllerSmoke(args[1], args[2]);
    }
    Console.Error.WriteLine("usage: CommonwealthOnline.Host.Tests [--package-smoke PACKAGE_DIR CONFIG_PATH]");
    return 2;
}

var root = Path.Combine(Path.GetTempPath(), "co-host-launch-tests-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(root);

try
{
    var manifestRoot = Path.Combine(root, "manifest");
    var manifestPlugins = Path.Combine(manifestRoot, "F4SE", "Plugins");
    Directory.CreateDirectory(manifestPlugins);
    File.WriteAllText(Path.Combine(manifestPlugins, "CommonwealthOnlineClient.json"),
        "{\"clientVersion\":\"0.1.0\",\"contentManifestFingerprint\":\"" + new string('b', 64) + "\",\"capabilities\":[\"interest-v1\"]}");
    var detected = MainWindowViewModel.DetectPackagedIdentity(manifestRoot);
    Require(detected is { ClientVersion: "0.1.0", Fingerprint: var fingerprint, Capabilities: var capabilities } &&
            fingerprint == new string('b', 64) && capabilities.SequenceEqual(new[] { "interest-v1" }),
        "Host must auto-detect a valid packaged client manifest from the known plugin location");
    File.WriteAllText(Path.Combine(manifestRoot, "CommonwealthOnlineClient.json"),
        "{\"clientVersion\":\"0.2.0\",\"contentManifestFingerprint\":\"" + new string('c', 64) + "\"}");
    detected = MainWindowViewModel.DetectPackagedIdentity(manifestRoot);
    Require(detected is { ClientVersion: "0.2.0", Fingerprint: var preferred } && preferred == new string('c', 64),
        "Host must prefer the package-root manifest over the fallback plugin location");
    var aePackage = Path.Combine(root, "build", "AE", "Fallout 4", "Data");
    Directory.CreateDirectory(aePackage);
    var aeRoot = Path.Combine(root, "build", "AE");
    var aeFalloutRoot = Path.Combine(aeRoot, "Fallout 4");
    Require(MainWindowViewModel.DetectPackagedRuntime(aeRoot) == "1.11.240" &&
            MainWindowViewModel.DetectPackagedRuntime(aeFalloutRoot) == "1.11.240" &&
            MainWindowViewModel.DetectPackagedRuntime(aePackage) == "1.11.240",
        "Host must derive the AE runtime from each canonical package root");
    var ngRoot = Path.Combine(root, "build", "NG");
    var ngFalloutRoot = Path.Combine(ngRoot, "Fallout 4");
    var ngDataRoot = Path.Combine(ngFalloutRoot, "Data");
    Directory.CreateDirectory(ngDataRoot);
    Require(MainWindowViewModel.DetectPackagedRuntime(ngRoot) == "1.10.984" &&
            MainWindowViewModel.DetectPackagedRuntime(ngFalloutRoot) == "1.10.984" &&
            MainWindowViewModel.DetectPackagedRuntime(ngDataRoot) == "1.10.984",
        "Host must derive the NG runtime from each canonical package root");
    var ogRoot = Path.Combine(root, "build", "OG");
    var ogFalloutRoot = Path.Combine(ogRoot, "Fallout 4");
    var ogDataRoot = Path.Combine(ogFalloutRoot, "Data");
    Directory.CreateDirectory(ogDataRoot);
    Require(MainWindowViewModel.DetectPackagedRuntime(ogRoot) == "1.10.163" &&
            MainWindowViewModel.DetectPackagedRuntime(ogFalloutRoot) == "1.10.163" &&
            MainWindowViewModel.DetectPackagedRuntime(ogDataRoot) == "1.10.163",
        "Host must derive the OG runtime from each canonical package root");
    var unrelatedAePackage = Path.Combine(root, "unrelated", "AE", "Fallout 4", "Data");
    Directory.CreateDirectory(unrelatedAePackage);
    Require(MainWindowViewModel.DetectPackagedRuntime(unrelatedAePackage) is null,
        "Host must not infer a runtime from an unrelated AE ancestor");
    var aeManifestPlugins = Path.Combine(aePackage, "F4SE", "Plugins");
    Directory.CreateDirectory(aeManifestPlugins);
    File.WriteAllText(Path.Combine(aeManifestPlugins, "CommonwealthOnlineClient.json"),
        "{\"clientVersion\":\"0.3.0\",\"contentManifestFingerprint\":\"" + new string('d', 64) + "\",\"capabilities\":[\"player-state-v1\"]}");
    var selectedRootIdentity = MainWindowViewModel.DetectPackagedIdentity(aeRoot);
    Require(selectedRootIdentity is { ClientVersion: "0.3.0", Fingerprint: var selectedFingerprint, Runtime: "1.11.240" } &&
            selectedFingerprint == new string('d', 64),
        "Host must find CommonwealthOnlineClient.json when the package root is selected");
    var hostStatePath = Path.Combine(AppContext.BaseDirectory, ".commonwealth-online-host-state.json");
    var hostConfigPath = Path.Combine(AppContext.BaseDirectory, "commonwealth-server.json");
    File.Delete(hostStatePath);
    File.WriteAllText(hostConfigPath, "{}\n");
    using (var packageViewModel = new MainWindowViewModel())
    {
        packageViewModel.RequiredClientVersion = null;
        packageViewModel.RequiredFalloutRuntimeVersion = "manual-runtime";
        packageViewModel.RequiredContentManifestFingerprint = null;
        packageViewModel.RequiredCapabilitiesText = "manual-capability";
        packageViewModel.SetClientPackageFolder(aePackage);
        packageViewModel.RefreshClientPackageCommand.Execute(null);
        Require(packageViewModel.RequiredClientVersion == "0.3.0" &&
                packageViewModel.RequiredContentManifestFingerprint == new string('d', 64),
            "Host must detect client identity from the selected package folder");
        Require(packageViewModel.RequiredFalloutRuntimeVersion == "manual-runtime",
            "manual runtime override must win over package-path detection");
        Require(packageViewModel.RequiredCapabilitiesText == "manual-capability",
            "manual capability override must win over manifest metadata");
    }
    File.Delete(hostStatePath);
    var ngPackage = Path.Combine(root, "build", "NG", "Fallout 4", "Data");
    var ngManifestPlugins = Path.Combine(ngPackage, "F4SE", "Plugins");
    Directory.CreateDirectory(ngManifestPlugins);
    File.WriteAllText(Path.Combine(ngManifestPlugins, "CommonwealthOnlineClient.json"),
        "{\"clientVersion\":\"0.4.0\",\"contentManifestFingerprint\":\"" + new string('e', 64) + "\",\"capabilities\":[\"cap-one\",\"cap-two\"]}");
    using (var packageViewModel = new MainWindowViewModel())
    {
        var identityChanged = false;
        packageViewModel.PropertyChanged += (_, args) =>
        {
            if (args.PropertyName == nameof(MainWindowViewModel.DetectedIdentityText)) identityChanged = true;
        };
        packageViewModel.SetClientPackageFolder(aePackage);
        packageViewModel.RefreshClientPackageCommand.Execute(null);
        Require(packageViewModel.RequiredClientVersion == "0.3.0" &&
                packageViewModel.RequiredContentManifestFingerprint == new string('d', 64) &&
                packageViewModel.RequiredFalloutRuntimeVersion == "1.11.240",
            "Host must populate all auto-managed identity fields from package A");
        packageViewModel.SetClientPackageFolder(ngPackage);
        packageViewModel.RefreshClientPackageCommand.Execute(null);
        Require(packageViewModel.RequiredClientVersion == "0.4.0" &&
                packageViewModel.RequiredContentManifestFingerprint == new string('e', 64) &&
                packageViewModel.RequiredFalloutRuntimeVersion == "1.10.984" &&
                packageViewModel.RequiredCapabilitiesText == "cap-one, cap-two",
            "Host must refresh all auto-managed identity fields from package A to package B");
        Require(identityChanged, "package refresh must notify DetectedIdentityText observers");

        packageViewModel.RequiredClientVersion = "manual-client";
        packageViewModel.RequiredFalloutRuntimeVersion = "manual-runtime";
        packageViewModel.RequiredContentManifestFingerprint = new string('f', 64);
        packageViewModel.RequiredCapabilitiesText = "manual-capability";
        packageViewModel.SetClientPackageFolder(aePackage);
        packageViewModel.RefreshClientPackageCommand.Execute(null);
        Require(packageViewModel.RequiredClientVersion == "manual-client" &&
                packageViewModel.RequiredFalloutRuntimeVersion == "manual-runtime" &&
                packageViewModel.RequiredContentManifestFingerprint == new string('f', 64) &&
                packageViewModel.RequiredCapabilitiesText == "manual-capability",
            "explicit identity and capability overrides must survive package refresh");

        packageViewModel.RequiredClientVersion = null;
        packageViewModel.RequiredFalloutRuntimeVersion = null;
        packageViewModel.RequiredContentManifestFingerprint = null;
        packageViewModel.RequiredCapabilitiesText = string.Empty;
        packageViewModel.SetClientPackageFolder(ngPackage);
        packageViewModel.RefreshClientPackageCommand.Execute(null);
        Require(packageViewModel.RequiredClientVersion == "0.4.0" &&
                packageViewModel.RequiredFalloutRuntimeVersion == "1.10.984" &&
                packageViewModel.RequiredContentManifestFingerprint == new string('e', 64) &&
                packageViewModel.RequiredCapabilitiesText == "cap-one, cap-two",
            "clearing overrides must restore automatic package metadata");

        packageViewModel.SetSelectedCapabilities(new[] { "cap-one" });
        Require(packageViewModel.RequiredCapabilitiesText == "cap-one",
            "selecting one capability must update the requirement");
        packageViewModel.SetSelectedCapabilities(new[] { "cap-one", "cap-two" });
        Require(packageViewModel.RequiredCapabilitiesText == "cap-one, cap-two",
            "selecting another capability must update the requirement");
        packageViewModel.SetSelectedCapabilities(Array.Empty<string>());
        Require(packageViewModel.RequiredCapabilitiesText == string.Empty,
            "deselecting capabilities must clear the automatic requirement");
        packageViewModel.SetSelectedCapabilities(new[] { "cap-two" });
        Require(packageViewModel.RequiredCapabilitiesText == "cap-two",
            "reselecting a capability must update the requirement");
        packageViewModel.RequiredCapabilitiesText = "raw-capability";
        packageViewModel.SetSelectedCapabilities(new[] { "cap-one" });
        Require(packageViewModel.RequiredCapabilitiesText == "raw-capability",
            "raw capability input must override list selection");
        packageViewModel.RequiredCapabilitiesText = string.Empty;
        packageViewModel.SetSelectedCapabilities(new[] { "cap-one" });
        Require(packageViewModel.RequiredCapabilitiesText == "cap-one",
            "clearing raw capability input must restore list selection");
        packageViewModel.RefreshClientPackageCommand.Execute(null);
        Require(packageViewModel.RequiredCapabilitiesText == "cap-one, cap-two",
            "automatic capability mode must resume after raw input is cleared");
    }
    File.Delete(hostStatePath);

    HostPackageStateRegression.Run(root, aePackage, ngPackage, hostStatePath, hostConfigPath);
    File.Delete(hostStatePath);
    File.Delete(hostConfigPath);
    Require(DurableStatePaths.Validate(Path.Combine(root, "state.json")).StartsWith("Ready:", StringComparison.Ordinal),
        "Host must accept a browse-selected file in an existing folder");
    Require(DurableStatePaths.Validate(Path.Combine(root, "missing", "state.json")).Contains("parent folder", StringComparison.Ordinal),
        "Host must reject a durable state path whose parent folder is absent");
    var stateDirectory = Path.Combine(root, "state");
    Directory.CreateDirectory(stateDirectory);
    var otherWorkingDirectory = Path.Combine(root, "other-working-directory");
    Directory.CreateDirectory(otherWorkingDirectory);
    var originalWorkingDirectory = Directory.GetCurrentDirectory();
    Directory.SetCurrentDirectory(otherWorkingDirectory);
    try
    {
        var resolved = DurableStatePaths.Resolve("state/relative.json", root);
        Require(resolved == Path.Combine(root, "state", "relative.json"),
            "relative durable state paths must resolve beside the Host configuration directory");
        Require(DurableStatePaths.Validate("state/relative.json", root).StartsWith("Ready:", StringComparison.Ordinal),
            "relative durable state validation must ignore process CWD");
    }
    finally
    {
        Directory.SetCurrentDirectory(originalWorkingDirectory);
    }

    var config = Path.Combine(root, "commonwealth-server.json");
    File.WriteAllText(config, "{}\n");
    var defaults = ServerConfig.Load(config);
    Require(defaults.RequireV2Identity, "empty config must default Protocol V2 identity on");
    Require(defaults.MaxPlayers == 16, "an existing config with omitted max_players must use the native parser default");
    var missingDefaults = ServerConfig.Load(Path.Combine(root, "missing-server.json"));
    Require(missingDefaults.RequireV2Identity, "missing config must default Protocol V2 identity on");
    Require(missingDefaults.MaxPlayers == 16, "a genuinely new Host config must use the shipped sixteen-player default");
    defaults.RequireV2Identity = false;
    Require(defaults.Validate().Any(error => error.Contains("mandatory", StringComparison.Ordinal)),
        "Host validation must reject disabling Protocol V2 identity");

    var apphost = Path.Combine(root, OperatingSystem.IsWindows() ? "CommonwealthOnline.Server.exe" : "CommonwealthOnline.Server");
    var marker = Path.Combine(root, "CommonwealthOnline.Server.native");
    var dll = Path.Combine(root, "CommonwealthOnline.Server.dll");

    CreateNativeLayout(root, apphost, marker, "bridge\n");
    var native = ServerController.ResolveLaunch(root, config);
    Require(native.FileName == apphost, "native package did not select the apphost");
    Require(Arguments(native).SequenceEqual(new[] { "--config", config }),
        "native package launch must not include the managed serve verb");

    File.WriteAllText(marker, "not-native\n");
    RequireThrows<InvalidOperationException>(() => ServerController.ResolveLaunch(root, config),
        "an invalid native marker must reject the package");
    File.WriteAllText(marker, "native-cpp\n");
    File.Delete(Path.Combine(root, NativeBridgeName()));
    RequireThrows<InvalidOperationException>(() => ServerController.ResolveLaunch(root, config),
        "a native package without its bridge must reject the package");
    File.WriteAllText(Path.Combine(root, NativeBridgeName()), "bridge\n");
    File.Delete(Path.Combine(root, "NATIVE_RUNTIME.txt"));
    RequireThrows<InvalidOperationException>(() => ServerController.ResolveLaunch(root, config),
        "a native package without its runtime layout must reject the package");
    File.WriteAllText(Path.Combine(root, "NATIVE_RUNTIME.txt"), "native runtime\n");
    File.Delete(config);
    RequireThrows<InvalidOperationException>(() => ServerController.ResolveLaunch(root, config),
        "a native package without commonwealth-server.json must reject the package");
    File.WriteAllText(config, "{}\n");

    var nativeConfig = new ServerConfig
    {
        Host = "127.0.0.1",
        Port = 7777,
        AdminPort = 7779,
        ServerName = "Native Save Test",
        ServerDescription = "native schema",
        MaxPlayers = 8,
        RequireV2Identity = true,
        EnablePersistence = true,
        DurableStatePath = "state/custom-durable.json",
        RequiredClientVersion = "test-client",
        RequiredFalloutRuntimeVersion = "1.11.240",
        RequiredContentManifestFingerprint = new string('a', 64),
        RequiredCapabilities = new[] { "player-state-v1" },
        ResumeTokenLifetimeHours = 24,
    };
    nativeConfig.Save(config);
    using (var document = JsonDocument.Parse(File.ReadAllText(config)))
    {
        var json = document.RootElement;
        Require(json.GetProperty("host").GetString() == "127.0.0.1", "native save lost host");
        Require(json.GetProperty("server_name").GetString() == "Native Save Test", "native save lost server name");
        Require(json.GetProperty("enable_persistence").GetBoolean(), "native save lost persistence flag");
        Require(json.GetProperty("durable_state_path").GetString() == "state/custom-durable.json", "native save lost custom durable state path");
        Require(json.GetProperty("required_client_version").GetString() == "test-client", "native save lost identity requirement");
        Require(json.GetProperty("required_capabilities").GetArrayLength() == 1, "native save lost required capabilities");
        Require(!json.TryGetProperty("enable_gns_transport", out _), "save reintroduced retired enable_gns_transport");
        Require(!json.TryGetProperty("gns_bridge_path", out _), "save reintroduced retired gns_bridge_path");
        Require(!json.TryGetProperty("log_verbosity", out _), "save reintroduced retired log_verbosity");
    }

    var reloadedNative = ServerConfig.Load(config);
    Require(reloadedNative.DurableStatePath == "state/custom-durable.json", "native load lost custom durable state path");
    reloadedNative.Save(config);
    using (var document = JsonDocument.Parse(File.ReadAllText(config)))
    {
        Require(document.RootElement.GetProperty("durable_state_path").GetString() == "state/custom-durable.json",
            "native Load -> Save round trip reset custom durable state path");
    }

    File.Delete(apphost);
    File.WriteAllText(dll, string.Empty);
    var managedRejected = false;
    try
    {
        _ = ServerController.ResolveLaunch(root, config);
    }
    catch (InvalidOperationException)
    {
        managedRejected = true;
    }
    Require(managedRejected, "a managed server layout must be rejected, not launched");

    File.WriteAllText(config, """
    {
      "host": "127.0.0.1",
      "port": 7777,
      "admin_port": 7779,
      "lan_discovery_enabled": false,
      "lan_discovery_port": 7999,
      "resume_token_lifetime_seconds": 45,
      "future_native_option": { "nested": true }
    }
    """);
    var preserved = ServerConfig.Load(config);
    Require(!preserved.LanDiscoveryEnabled, "load lost lan_discovery_enabled");
    Require(preserved.LanDiscoveryPort == 7999, "load lost lan_discovery_port");
    Require(preserved.ResumeTokenLifetimeSeconds == 45, "load lost resume_token_lifetime_seconds");
    preserved.Save(config);
    using (var document = JsonDocument.Parse(File.ReadAllText(config)))
    {
        var json = document.RootElement;
        Require(!json.GetProperty("lan_discovery_enabled").GetBoolean(),
            "save re-enabled LAN discovery, which is the native default");
        Require(json.GetProperty("lan_discovery_port").GetInt32() == 7999, "save lost lan_discovery_port");
        Require(json.GetProperty("resume_token_lifetime_seconds").GetInt32() == 45,
            "save lost the seconds-level resume lifetime override");
        Require(json.TryGetProperty("future_native_option", out var future) &&
            future.GetProperty("nested").GetBoolean(),
            "save erased a native key the Host does not model");
    }

    var success = AdminResponse.Parse(System.Text.Json.Nodes.JsonNode.Parse(
        """{"ok":true,"data":{"clients":[{"player_id":4}]}}""") as System.Text.Json.Nodes.JsonObject);
    Require(success.Ok, "a successful admin reply must read as ok");
    Require(success.Data?["clients"] is System.Text.Json.Nodes.JsonArray,
        "admin command payloads must be read from data, not the root");
    var failure = AdminResponse.Parse(System.Text.Json.Nodes.JsonNode.Parse(
        """{"ok":false,"error":"No such player."}""") as System.Text.Json.Nodes.JsonObject);
    Require(!failure.Ok, "a failed admin reply must not read as ok");
    Require(failure.Describe() == "No such player.", "a failed admin reply must surface its error");
    Require(!AdminResponse.Parse(null).Ok, "an unreadable admin reply must not read as ok");

    var nullFields = Path.Combine(root, "null-fields.json");
    File.WriteAllText(nullFields, """
    {
      "host": "127.0.0.1",
      "port": null,
      "admin_port": null,
      "require_v2_identity": null,
      "required_capabilities": null,
      "resume_token_lifetime_seconds": null,
      "required_client_version": "  client-v1  ",
      "required_fallout_runtime_version": "   ",
      "required_content_manifest_fingerprint": "  aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa  "
    }
    """);
    var nullConfig = ServerConfig.Load(nullFields);
    Require(nullConfig.Port == 7777 && nullConfig.AdminPort == 7779,
        "native null port fields must use defaults");
    Require(nullConfig.RequireV2Identity, "native null identity field must use its default");
    Require(nullConfig.RequiredCapabilities.Length == 0, "native null capability field must use its default");
    Require(nullConfig.ResumeTokenLifetimeSeconds is null,
        "native null resume seconds field must mean no override");
    Require(nullConfig.RequiredClientVersion == "client-v1", "optional version must be trimmed like native");
    Require(nullConfig.RequiredFalloutRuntimeVersion is null, "blank optional version must be absent like native");
    Require(nullConfig.RequiredContentManifestFingerprint == new string('a', 64),
        "optional fingerprint must be trimmed like native");

    foreach (var host in new[] { "127.1", "127.0.0", "0x7f.0.0.1" })
    {
        var parity = new ServerConfig { Host = host };
        Require(parity.Validate().Any(error => error.Contains("explicit IPv4", StringComparison.Ordinal)),
            $"non-explicit IPv4 host '{host}' must be rejected");
    }
    Require(new ServerConfig { MaxPlayers = 16 }.Validate().Count == 0,
        "the shipped sixteen-player native configuration must validate");
    Require(new ServerConfig { MaxPlayers = 17 }.Validate().Count > 0, "Host config must reject more than sixteen players");
    Require(new ServerConfig().MaxPlayers == 16, "Host-created config must default to sixteen players");

    File.WriteAllText(apphost, "not an executable\n");
    using (var controller = new ServerController())
    {
        RequireThrows<Exception>(() => controller.Start(root, config),
            "an invalid native executable must surface a launch failure");
        Require(!controller.IsRunning, "a failed launch must leave the controller stopped");
    }

    var viewModelRoot = AppContext.BaseDirectory;
    var viewModelConfig = Path.Combine(viewModelRoot, "commonwealth-server.json");
    var viewModelApphost = Path.Combine(viewModelRoot,
        OperatingSystem.IsWindows() ? "CommonwealthOnline.Server.exe" : "CommonwealthOnline.Server");
    var viewModelMarker = Path.Combine(viewModelRoot, "CommonwealthOnline.Server.native");
    var viewModelBridge = Path.Combine(viewModelRoot, NativeBridgeName());
    var viewModelRuntime = Path.Combine(viewModelRoot, "NATIVE_RUNTIME.txt");
    File.WriteAllText(viewModelConfig, "{}\n");
    CreateNativeLayout(viewModelRoot, viewModelApphost, viewModelMarker, "bridge\n");
    File.WriteAllText(viewModelApphost, "not an executable\n");
    var viewModel = new MainWindowViewModel();
    try
    {
        viewModel.StartCommand.Execute(null);
        Require(!viewModel.IsRunning, "a failed VM launch must remain stopped");
        Require(viewModel.StatusText == "Start failed", "a failed VM launch must update status");
    }
    finally
    {
        if (viewModel is IDisposable disposable) disposable.Dispose();
        foreach (var path in new[] { viewModelConfig, viewModelApphost, viewModelMarker, viewModelBridge, viewModelRuntime })
        {
            try { File.Delete(path); } catch { }
        }
    }

    File.Delete(apphost);
    RunGracefulStopRegression(root, config);
    WindowsShutdownAcknowledgementRegression.Run();
    var aliasFallback = Path.Combine(root, "alias-fallback.json");
    File.WriteAllText(aliasFallback, """
    {
      "host": "127.0.0.1",
      "admin_port": null,
      "adminPort": 7780,
      "server_name": null,
      "serverName": "Alias server"
    }
    """);
    var aliasConfig = ServerConfig.Load(aliasFallback);
    Require(aliasConfig.AdminPort == 7780, "a canonical null must fall back to the native alias");
    Require(aliasConfig.ServerName == "Alias server", "a canonical null string must fall back to the native alias");

    var numericSyntax = Path.Combine(root, "numeric-syntax.json");
    File.WriteAllText(numericSyntax, """
    {
      "port": "+7788"
    }
    """);
    RequireThrows<InvalidDataException>(() => ServerConfig.Load(numericSyntax),
        "numeric strings with a leading plus must be rejected like native from_chars");

    var unicodeBoundary = new ServerConfig { RequiredClientVersion = new string('é', 33) };
    Require(unicodeBoundary.Validate().Any(error => error.Contains("required client version", StringComparison.Ordinal)),
        "optional string bounds must use UTF-8 byte length");
    var unicodeName = new ServerConfig { ServerName = new string('é', 33) };
    Require(unicodeName.Validate().Any(error => error.Contains("server_name", StringComparison.Ordinal)),
        "server name bounds must use UTF-8 byte length");
    var unicodeCapabilities = new ServerConfig { RequiredCapabilities = new[] { new string('é', 33) } };
    Require(unicodeCapabilities.Validate().Any(error => error.Contains("capability", StringComparison.Ordinal)),
        "capability bounds must use UTF-8 byte length");

    var nbsp = "\u00a0";
    var nbspBind = Path.Combine(root, "nbsp-bind.json");
    File.WriteAllText(nbspBind, $$"""
    {
      "host": "{{nbsp}}127.0.0.1{{nbsp}}"
    }
    """);
    RequireThrows<InvalidDataException>(() => ServerConfig.Load(nbspBind),
        "native ASCII trimming must reject a bind address surrounded by non-ASCII whitespace");
    var nbspNumber = Path.Combine(root, "nbsp-number.json");
    File.WriteAllText(nbspNumber, $$"""
    {
      "port": "{{nbsp}}7788{{nbsp}}"
    }
    """);
    RequireThrows<InvalidDataException>(() => ServerConfig.Load(nbspNumber),
        "native ASCII trimming must reject numeric fields surrounded by non-ASCII whitespace");
    var nbspFields = Path.Combine(root, "nbsp-fields.json");
    File.WriteAllText(nbspFields, $$"""
    {
      "required_client_version": "{{nbsp}}client-v1{{nbsp}}",
      "required_capabilities": ["{{nbsp}}capability{{nbsp}}"]
    }
    """);
    var nbspConfig = ServerConfig.Load(nbspFields);
    Require(nbspConfig.RequiredClientVersion == $"{nbsp}client-v1{nbsp}",
        "optional strings must preserve non-ASCII whitespace like native");
    Require(nbspConfig.RequiredCapabilities.Single() == $"{nbsp}capability{nbsp}",
        "capabilities must preserve non-ASCII whitespace like native");
    Require(new ServerConfig { RequiredClientVersion = nbsp, RequiredCapabilities = new[] { nbsp } }.Validate().Count == 0,
        "non-ASCII whitespace is printable to native validation");

    var normalizationRoot = AppContext.BaseDirectory;
    var normalizationConfig = Path.Combine(normalizationRoot, "commonwealth-server.json");
    File.WriteAllText(normalizationConfig, "{}\n");
    using (var normalizationViewModel = new MainWindowViewModel())
    {
        normalizationViewModel.RequiredClientVersion = $"{nbsp}client-v1{nbsp}";
        normalizationViewModel.RequiredCapabilitiesText = $"{nbsp}capability{nbsp}";
        normalizationViewModel.SaveCommand.Execute(null);
        var saved = ServerConfig.Load(normalizationConfig);
        Require(saved.RequiredClientVersion == $"{nbsp}client-v1{nbsp}",
            "ViewModel optional normalization must use native ASCII whitespace");
        Require(saved.RequiredCapabilities.Single() == $"{nbsp}capability{nbsp}",
            "ViewModel capability normalization must use native ASCII whitespace");
    }
    File.Delete(normalizationConfig);

    ServerPasswordRegression.Run(root);
    WorldModeRegression.Run(root);
    PublicListingRegression.Run(root);
    RunWorkflowCleanupRegression();

    var listingPath = Path.Combine(root, "public-listing.json");
    Require(ServerConfig.DefaultPublicListing, "the shared public listing default must be on");
    var listingDefaults = ServerConfig.Load(Path.Combine(root, "missing-listing.json"));
    Require(listingDefaults.PublicListing, "public listing must default on for a new config");
    Require(new ServerConfig().PublicListing == ServerConfig.DefaultPublicListing,
        "a Host-created config must use the shared public listing default");
    var emptyListing = Path.Combine(root, "empty-listing.json");
    File.WriteAllText(emptyListing, "{}\n");
    Require(ServerConfig.Load(emptyListing).PublicListing,
        "a config without a public_listing key must fall back to the shared default");
    var explicitOffListing = Path.Combine(root, "explicit-off-listing.json");
    File.WriteAllText(explicitOffListing, "{ \"public_listing\": false }\n");
    Require(!ServerConfig.Load(explicitOffListing).PublicListing, "an explicit public_listing false must stay off");
    Require(ServerConfig.Load(explicitOffListing).Validate().Count == 0, "an explicit listing-off config must validate");
    Require(listingDefaults.DirectoryUrl == ServerConfig.DefaultDirectoryUrl, "directory url default changed");
    listingDefaults.Save(listingPath);
    var listingNative = JsonDocument.Parse(File.ReadAllText(listingPath)).RootElement;
    Require(listingNative.GetProperty("public_listing").GetBoolean(), "public_listing not written for the native server");
    Require(listingNative.GetProperty("directory_url").GetString() == ServerConfig.DefaultDirectoryUrl, "directory_url not written");
    Require(ServerConfig.Load(listingPath).PublicListing, "public_listing lost on reload");
    listingDefaults.DirectoryUrl = "http://directory.example.com/api/v1/servers";
    Require(listingDefaults.Validate().Any(error => error.Contains("directory_url")), "insecure directory url accepted");
    listingDefaults.DirectoryUrl = "http://127.0.0.1:3000/api/v1/servers";
    Require(listingDefaults.Validate().Count == 0, "loopback directory url rejected");
    Require(!ServerConfig.IsAllowedDirectoryUrl("http://localhost.evil.example/"), "localhost prefix spoof accepted");
    Require(!ServerConfig.IsAllowedDirectoryUrl("http://127.0.0.1:1@evil.example"), "loopback userinfo spoof accepted");
    Require(!ServerConfig.IsAllowedDirectoryUrl("https://user@x.supabase.co/"), "https userinfo accepted");
    Require(ServerConfig.IsAllowedDirectoryUrl("http://127.0.0.1:8080/functions/v1/server-directory"), "loopback port url rejected");
    Require(ServerConfig.IsAllowedDirectoryUrl(ServerConfig.DefaultDirectoryUrl), "default directory url rejected");

    Require(PublicListingText.Describe(null) == "Not listed", "missing listing status");
    Require(PublicListingText.Describe(new System.Text.Json.Nodes.JsonObject { ["state"] = "registering" }) == "Registering…", "registering status");
    Require(PublicListingText.Describe(new System.Text.Json.Nodes.JsonObject { ["state"] = "registered", ["publicAddress"] = "203.0.113.10:7777" }) == "Registered as 203.0.113.10:7777", "registered status");
    var nonPublicStatus = PublicListingText.Describe(new System.Text.Json.Nodes.JsonObject
    {
        ["state"] = "error",
        ["error"] = "non_public_address (HTTP 422): Registration must come from the server's public IPv4 address.",
    });
    Require(nonPublicStatus.StartsWith("Error: ", StringComparison.Ordinal) &&
            nonPublicStatus.Contains("CGNAT", StringComparison.Ordinal) &&
            nonPublicStatus.Contains("STEAM P2P", StringComparison.Ordinal) &&
            nonPublicStatus.Contains("direct connect", StringComparison.Ordinal) &&
            nonPublicStatus.Contains("router", StringComparison.Ordinal),
        "a non_public_address error must explain CGNAT and suggest STEAM P2P or direct connect");

    Console.WriteLine("Host launch and native config contract tests passed.");
    return 0;
}
finally
{
    Directory.Delete(root, recursive: true);
}
