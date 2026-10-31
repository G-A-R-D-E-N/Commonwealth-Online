using System;
using System.IO;
using System.Linq;
using CommonwealthOnline.Host.Services;
using CommonwealthOnline.Host.ViewModels;

static class HostPackageStateRegression
{
    public static void Run(string root, string aePackage, string ngPackage, string hostStatePath, string hostConfigPath)
    {
        File.Delete(hostStatePath);
        File.WriteAllText(hostConfigPath, "{}\n");
        using (var initial = new MainWindowViewModel())
        {
            initial.SetClientPackageFolder(aePackage);
            initial.RefreshClientPackageCommand.Execute(null);
            Require(initial.RequiredClientVersion == "0.3.0" &&
                    initial.RequiredFalloutRuntimeVersion == "1.11.240" &&
                    initial.RequiredContentManifestFingerprint == new string('d', 64) &&
                    initial.RequiredCapabilitiesText == "player-state-v1",
                "Host must detect automatic package metadata before restart");
            initial.SaveCommand.Execute(null);
        }

        var saved = ServerConfig.Load(hostConfigPath);
        Require(saved.RequiredClientVersion == "0.3.0" &&
                saved.RequiredFalloutRuntimeVersion == "1.11.240" &&
                saved.RequiredContentManifestFingerprint == new string('d', 64) &&
                saved.RequiredCapabilities.SequenceEqual(new[] { "player-state-v1" }),
            "Host must write detected values to the native config");

        using (var recreated = new MainWindowViewModel())
        {
            Require(recreated.ClientPackageFolder == Path.GetFullPath(aePackage) &&
                    recreated.RequiredClientVersion == "0.3.0" &&
                    recreated.RequiredFalloutRuntimeVersion == "1.11.240" &&
                    recreated.RequiredContentManifestFingerprint == new string('d', 64) &&
                    recreated.RequiredCapabilitiesText == "player-state-v1",
                "Host must restore the package folder and automatic metadata after restart");
            recreated.SetClientPackageFolder(ngPackage);
            recreated.RefreshClientPackageCommand.Execute(null);
            Require(recreated.RequiredClientVersion == "0.4.0" &&
                    recreated.RequiredFalloutRuntimeVersion == "1.10.984" &&
                    recreated.RequiredContentManifestFingerprint == new string('e', 64) &&
                    recreated.RequiredCapabilitiesText == "cap-one, cap-two",
                "automatic metadata must update from package A to B after restart");
            recreated.RequiredClientVersion = "manual-client";
            recreated.RequiredFalloutRuntimeVersion = "manual-runtime";
            recreated.RequiredContentManifestFingerprint = new string('f', 64);
            recreated.RequiredCapabilitiesText = "manual-capability";
            recreated.SaveCommand.Execute(null);
        }

        using (var manual = new MainWindowViewModel())
        {
            Require(manual.RequiredClientVersion == "manual-client" &&
                    manual.RequiredFalloutRuntimeVersion == "manual-runtime" &&
                    manual.RequiredContentManifestFingerprint == new string('f', 64) &&
                    manual.RequiredCapabilitiesText == "manual-capability",
                "manual package metadata overrides must survive restart");
            manual.RequiredClientVersion = null;
            manual.RequiredFalloutRuntimeVersion = null;
            manual.RequiredContentManifestFingerprint = null;
            manual.RequiredCapabilitiesText = string.Empty;
            manual.RefreshClientPackageCommand.Execute(null);
            Require(manual.RequiredClientVersion == "0.4.0" &&
                    manual.RequiredFalloutRuntimeVersion == "1.10.984" &&
                    manual.RequiredContentManifestFingerprint == new string('e', 64) &&
                    manual.RequiredCapabilitiesText == "cap-one, cap-two",
                "cleared overrides must return to automatic mode after restart");
            manual.SaveCommand.Execute(null);
        }

        RunAutoClearRegression(root, aePackage, hostStatePath, hostConfigPath);

        var external = ServerConfig.Load(hostConfigPath);
        external.RequiredClientVersion = "external-client";
        external.RequiredFalloutRuntimeVersion = "external-runtime";
        external.RequiredContentManifestFingerprint = new string('b', 64);
        external.RequiredCapabilities = new[] { "external-capability" };
        external.Save(hostConfigPath);
        using (var edited = new MainWindowViewModel())
        {
            Require(edited.RequiredClientVersion == "external-client" &&
                    edited.RequiredFalloutRuntimeVersion == "external-runtime" &&
                    edited.RequiredContentManifestFingerprint == new string('b', 64) &&
                    edited.RequiredCapabilitiesText == "external-capability",
                "native config edits must take precedence over stale Host AUTO state");
            edited.RefreshClientPackageCommand.Execute(null);
            Require(edited.RequiredClientVersion == "external-client" &&
                    edited.RequiredFalloutRuntimeVersion == "external-runtime" &&
                    edited.RequiredContentManifestFingerprint == new string('b', 64) &&
                    edited.RequiredCapabilitiesText == "external-capability",
                "package refresh must not overwrite native config edits classified as manual");
            edited.SaveCommand.Execute(null);
        }
        var preserved = ServerConfig.Load(hostConfigPath);
        Require(preserved.RequiredClientVersion == "external-client" &&
                preserved.RequiredFalloutRuntimeVersion == "external-runtime" &&
                preserved.RequiredContentManifestFingerprint == new string('b', 64) &&
                preserved.RequiredCapabilities.SequenceEqual(new[] { "external-capability" }),
            "Host Save must preserve externally edited native identity values");

        File.Delete(hostStatePath);
        var missing = Path.Combine(root, "missing-remembered-package");
        File.WriteAllText(hostStatePath, $$"""
        {
          "client_package_folder": "{{missing.Replace("\\", "\\\\")}}",
          "last_auto_client_version": "0.4.0",
          "last_auto_runtime": "1.10.984",
          "last_auto_fingerprint": "{{new string('e', 64)}}",
          "last_auto_capabilities": "cap-one, cap-two",
          "client_version_manual": false,
          "runtime_manual": false,
          "fingerprint_manual": false,
          "capabilities_manual": false
        }
        """);
        using (var unavailable = new MainWindowViewModel())
        {
            var configured = ServerConfig.Load(hostConfigPath);
            Require(unavailable.ClientPackageFolder == missing &&
                    unavailable.RequiredClientVersion == configured.RequiredClientVersion &&
                    unavailable.RequiredFalloutRuntimeVersion == configured.RequiredFalloutRuntimeVersion &&
                    unavailable.RequiredContentManifestFingerprint == configured.RequiredContentManifestFingerprint &&
                    unavailable.RequiredCapabilitiesText == string.Join(", ", configured.RequiredCapabilities) &&
                    unavailable.ClientPackageStatus.Contains("No CommonwealthOnlineClient.json", StringComparison.Ordinal),
                "missing remembered package paths must retain native identity values");
            unavailable.SaveCommand.Execute(null);
            RequireSameIdentity(configured, ServerConfig.Load(hostConfigPath),
                "Host Save must preserve native identity values when detection is unavailable");

            var apphost = Path.Combine(AppContext.BaseDirectory,
                OperatingSystem.IsWindows() ? "CommonwealthOnline.Server.exe" : "CommonwealthOnline.Server");
            var marker = Path.Combine(AppContext.BaseDirectory, "CommonwealthOnline.Server.native");
            var bridge = Path.Combine(AppContext.BaseDirectory, NativeBridgeName());
            var runtime = Path.Combine(AppContext.BaseDirectory, "NATIVE_RUNTIME.txt");
            CreateNativeLayout(apphost, marker, bridge, runtime);
            File.WriteAllText(apphost, "not an executable\n");
            unavailable.StartCommand.Execute(null);
            Require(!unavailable.IsRunning && unavailable.StatusText == "Start failed",
                "Start must use the preserved native config when the remembered package is missing");
            RequireSameIdentity(configured, ServerConfig.Load(hostConfigPath),
                "Start must preserve native identity values when detection is unavailable");
            foreach (var path in new[] { apphost, marker, bridge, runtime })
            {
                try { File.Delete(path); } catch { }
            }
        }
        File.Delete(hostStatePath);
        File.Delete(hostConfigPath);
        RunUtf8SizeRegression(root);
    }

    private static void RunAutoClearRegression(string root, string aePackage, string hostStatePath, string hostConfigPath)
    {
        var manifest = Path.Combine(aePackage, "F4SE", "Plugins", "CommonwealthOnlineClient.json");
        var fields = new (Action<MainWindowViewModel, string?> Set, Action<ServerConfig, string> SetConfig,
            Func<MainWindowViewModel, string?> Get, Func<HostClientPackageState, bool> AutoCleared,
            string Manual, string Automatic, string External)[]
        {
            ((vm, value) => vm.RequiredClientVersion = value, (config, value) => config.RequiredClientVersion = value,
                vm => vm.RequiredClientVersion,
                state => state.ClientVersionAutoCleared, "manual-client", "0.3.0", "external-client"),
            ((vm, value) => vm.RequiredFalloutRuntimeVersion = value, (config, value) => config.RequiredFalloutRuntimeVersion = value,
                vm => vm.RequiredFalloutRuntimeVersion,
                state => state.RuntimeAutoCleared, "manual-runtime", "1.11.240", "external-runtime"),
            ((vm, value) => vm.RequiredContentManifestFingerprint = value, (config, value) => config.RequiredContentManifestFingerprint = value,
                vm => vm.RequiredContentManifestFingerprint,
                state => state.FingerprintAutoCleared, "manual-fingerprint", new string('d', 64), new string('a', 64)),
            ((vm, value) => vm.RequiredCapabilitiesText = value ?? string.Empty,
                (config, value) => config.RequiredCapabilities = new[] { value },
                vm => vm.RequiredCapabilitiesText,
                state => state.CapabilitiesAutoCleared, "manual-capability", "player-state-v1", "external-capability"),
        };

        foreach (var field in fields)
        {
            File.Delete(hostStatePath);
            File.WriteAllText(hostConfigPath, "{}\n");
            using (var automatic = new MainWindowViewModel())
            {
                automatic.SetClientPackageFolder(aePackage);
                automatic.RefreshClientPackageCommand.Execute(null);
                automatic.SaveCommand.Execute(null);
            }

            using (var cleared = new MainWindowViewModel())
            {
                Require(field.Get(cleared) == field.Automatic, "auto-clear regression must start with detected metadata");
                field.Set(cleared, field.Manual);
                field.Set(cleared, null);
                cleared.SaveCommand.Execute(null);
            }

            var saved = HostClientPackageState.Load(hostStatePath);
            Require(saved is not null && field.AutoCleared(saved) &&
                    !saved.ClientVersionManual && !saved.RuntimeManual &&
                    !saved.FingerprintManual && !saved.CapabilitiesManual,
                "clearing a manual value must persist AUTO provenance before refresh");

            var backup = manifest + ".bak";
            File.Delete(backup);
            File.Move(manifest, backup);
            try
            {
                using var unavailable = new MainWindowViewModel();
                Require(string.IsNullOrEmpty(field.Get(unavailable)),
                    "an unavailable package may leave a cleared AUTO value blank after restart");
            }
            finally
            {
                File.Move(backup, manifest);
            }

            using (var restored = new MainWindowViewModel())
            {
                restored.SetClientPackageFolder(aePackage);
                restored.RefreshClientPackageCommand.Execute(null);
                Require(field.Get(restored) == field.Automatic,
                    "a later successful package refresh must repopulate a cleared AUTO value");
            }

            using (var clearedAgain = new MainWindowViewModel())
            {
                field.Set(clearedAgain, field.Manual);
                field.Set(clearedAgain, null);
                clearedAgain.SaveCommand.Execute(null);
            }
            var external = ServerConfig.Load(hostConfigPath);
            field.SetConfig(external, field.External);
            external.Save(hostConfigPath);
            using (var edited = new MainWindowViewModel())
            {
                Require(field.Get(edited) == field.External,
                    "a native edit after an AUTO clear must become MANUAL after restart");
                edited.RefreshClientPackageCommand.Execute(null);
                Require(field.Get(edited) == field.External,
                    "refresh must preserve a native edit after an AUTO clear");
            }
        }
    }

    private static void RequireSameIdentity(ServerConfig expected, ServerConfig actual, string message)
    {
        Require(expected.RequiredClientVersion == actual.RequiredClientVersion &&
                expected.RequiredFalloutRuntimeVersion == actual.RequiredFalloutRuntimeVersion &&
                expected.RequiredContentManifestFingerprint == actual.RequiredContentManifestFingerprint &&
                expected.RequiredCapabilities.SequenceEqual(actual.RequiredCapabilities), message);
    }

    private static void RunUtf8SizeRegression(string root)
    {
        var path = Path.Combine(root, "host-state-utf8.json");
        var state = new HostClientPackageState { Capabilities = new string('é', 10000) };
        state.Save(path);
        Require(new FileInfo(path).Length <= 64 * 1024 &&
                HostClientPackageState.Load(path)?.Capabilities == state.Capabilities,
            "Host state must measure and round-trip serialized UTF-8 bytes");
        state.Capabilities = new string('é', 11000);
        RequireThrows<InvalidDataException>(() => state.Save(path),
            "Host state must reject serialized UTF-8 bytes above 64 KiB");
        File.Delete(path);
    }

    private static string NativeBridgeName() => OperatingSystem.IsWindows()
        ? "commonwealth_online_gns_bridge.dll"
        : "libcommonwealth_online_gns_bridge.so";

    private static void CreateNativeLayout(string apphost, string marker, string bridge, string runtime)
    {
        File.WriteAllText(apphost, "native server\n");
        File.WriteAllText(marker, "native-cpp\n");
        File.WriteAllText(bridge, "bridge\n");
        File.WriteAllText(runtime, "native runtime\n");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    private static void RequireThrows<TException>(Action action, string message) where TException : Exception
    {
        try { action(); }
        catch (TException) { return; }
        throw new InvalidOperationException(message);
    }
}
