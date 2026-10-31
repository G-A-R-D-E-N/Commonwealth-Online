using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using Avalonia.Threading;
using CommonwealthOnline.Host.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CommonwealthOnline.Host.ViewModels;

public partial class MainWindowViewModel : ObservableObject, IDisposable
{
    private const int MaxLogLines = 2000;

    private readonly ServerController _controller = new();
    private readonly string _serverDir = AppContext.BaseDirectory;
    private readonly string _configPath = Path.Combine(AppContext.BaseDirectory, "commonwealth-server.json");
    private readonly string _hostStatePath = Path.Combine(AppContext.BaseDirectory, ".commonwealth-online-host-state.json");
    private readonly DispatcherTimer _pollTimer;
    private readonly string? _configLoadError;
    private bool _polling;
    private string? _lastPollFailure;
    private int? _resumeTokenLifetimeSeconds;
    private bool _refreshingPackage;
    private bool _manualClientVersionOverride;
    private bool _manualRuntimeOverride;
    private bool _manualFingerprintOverride;
    private bool _manualCapabilityOverride;
    private bool _clientVersionAutoCleared, _runtimeAutoCleared, _fingerprintAutoCleared, _capabilitiesAutoCleared;
    private string? _rememberedClientPackageFolder;
    private string? _lastAutoClientVersion;
    private string? _lastAutoRuntime;
    private string? _lastAutoFingerprint;
    private string _lastAutoCapabilities = string.Empty;
    private IReadOnlyDictionary<string, JsonElement> _unknownConfigKeys =
        new Dictionary<string, JsonElement>();

    [ObservableProperty] private string _serverName = "Commonwealth Online Server";
    [ObservableProperty] private string _serverDescription = string.Empty;
    [ObservableProperty] private string _host = "0.0.0.0";
    [ObservableProperty] private int _port = 7777;
    [ObservableProperty] private int _maxPlayers = 16;
    [ObservableProperty] private int _adminPort = 7779;
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsPasswordProtected))]
    private string _serverPassword = string.Empty;
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(WorldMode))]
    [NotifyPropertyChangedFor(nameof(GameModeDescription))]
    private string _gameModeLabel = GameModes.SelectableLabels[0];
    [ObservableProperty] private bool _requireV2Identity;
    [ObservableProperty] private bool _enablePersistence;
    [ObservableProperty] private string _durableStatePath = "durable-state.json";
    [ObservableProperty] private string _durableStatePathStatus = string.Empty;
    [ObservableProperty] private string _clientPackageFolder = string.Empty;
    [ObservableProperty] private string _clientPackageStatus = string.Empty;
    [ObservableProperty] private string? _requiredClientVersion;
    [ObservableProperty] private string? _requiredFalloutRuntimeVersion;
    [ObservableProperty] private string? _requiredContentManifestFingerprint;
    [ObservableProperty] private string _requiredCapabilitiesText = string.Empty;
    [ObservableProperty] private int _resumeTokenLifetimeHours = 720;
    [ObservableProperty] private bool _enableLanDiscovery = true;
    [ObservableProperty] private int _lanDiscoveryPort = 7778;
    [ObservableProperty] private bool _listPublicly;
    [ObservableProperty] private string _publicListingStatus = "Not listed";
    private string _directoryUrl = ServerConfig.DefaultDirectoryUrl;
    public ObservableCollection<string> DetectedCapabilities { get; } = new();
    [ObservableProperty] private bool _isRunning;
    [ObservableProperty] private string _statusText = "Stopped";
    [ObservableProperty] private string _statsText = string.Empty;
    public bool IsPasswordProtected => !string.IsNullOrEmpty(ServerPassword);
    public IReadOnlyList<string> GameModeLabels { get; } = GameModes.SelectableLabels;
    public string WorldMode => GameModes.ModeForLabel(GameModeLabel);
    public string GameModeDescription => GameModes.Describe(WorldMode);

    public string GuideText { get; } = """
        QUICK START
        1. Confirm the native server package, GNS bridge, and NATIVE_RUNTIME.txt are beside this Host.
        2. Set the server name, address, gameplay port, maximum players, and admin port.
        3. Save configuration, then start the server. The Host launches the packaged native binary.

        SERVER SETTINGS
        Server Name and Description are shown to clients. Host Address is an explicit IPv4 bind address.
        Gameplay Port carries GNS traffic. Max Players is the admission limit. Admin Port is the local
        authenticated control channel and must differ from the gameplay port.

        PROTOCOL V2 IDENTITY
        V2 identity is mandatory. Client version, Fallout runtime, content manifest SHA-256, and
        capabilities restrict which clients may join. A packaged CommonwealthOnlineClient.json can
        supply client version and manifest fingerprint automatically; runtime and capabilities stay
        manual unless one target is proven by the package.

        PERSISTENCE AND RESUME
        Durable persistence stores server state at the selected path. Resume token lifetime controls
        how long reconnect credentials remain usable. Keep the state file private and back it up safely.

        LAN AND NETWORKING
        LAN discovery uses its own UDP port. Allow the gameplay, admin, and discovery ports through the
        local firewall as appropriate. Router forwarding is required only for internet-hosted servers.
        Do not expose the admin port beyond the local machine.

        ADMINISTRATION
        The Players panel supports authenticated kick and ban actions. Review the server log for startup,
        admission, rejection, persistence, and shutdown failures.

        PACKAGE LAYOUT AND COMPATIBILITY
        The server package contains the Avalonia Host, native server, GNS bridge, server config,
        NATIVE_RUNTIME.txt, and runtime support files. AE 1.11.240, NG 1.10.984, and OG 1.10.163
        client mod packages are separate. Client Package Folder selects one client package for V2
        compatibility metadata detection. PrismaUI_F4 2.1.0 is an external Ultralight prerequisite.

        TROUBLESHOOTING AND DEVELOPMENT STATUS
        If start fails, verify the native marker, bridge, runtime layout, config, and firewall. If clients
        are rejected, compare their V2 identity fields and manifest fingerprint. This Host is a developer
        build surface; Fallout runtime, save continuity, NPC, and full protocol acceptance still require
        manual game validation.
        """;

    private string _detectedIdentityText = "No packaged client manifest detected.";
    public string DetectedIdentityText
    {
        get => _detectedIdentityText;
        private set => SetProperty(ref _detectedIdentityText, value);
    }

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(KickCommand))]
    [NotifyCanExecuteChangedFor(nameof(BanCommand))]
    private PlayerRow? _selectedPlayer;

    public ObservableCollection<string> Log { get; } = new();
    public ObservableCollection<PlayerRow> Players { get; } = new();


    public MainWindowViewModel()
    {
        ServerConfig config;
        try
        {
            config = ServerConfig.Load(_configPath);
        }
        catch (Exception ex)
        {
            config = new ServerConfig();
            _configLoadError = ex.Message;
        }
        var hostState = HostClientPackageState.Load(_hostStatePath);

        ServerName = config.ServerName;
        ServerDescription = config.ServerDescription;
        Host = config.Host;
        Port = config.Port;
        MaxPlayers = config.MaxPlayers;
        AdminPort = config.AdminPort;
        ServerPassword = config.ServerPassword ?? string.Empty;
        GameModeLabel = GameModes.LabelForMode(config.WorldMode);
        RequireV2Identity = config.RequireV2Identity;
        EnablePersistence = config.EnablePersistence;
        DurableStatePath = config.DurableStatePath;
        _refreshingPackage = true;
        try
        {
            RequiredClientVersion = config.RequiredClientVersion;
            RequiredFalloutRuntimeVersion = config.RequiredFalloutRuntimeVersion;
            RequiredContentManifestFingerprint = config.RequiredContentManifestFingerprint;
            RequiredCapabilitiesText = string.Join(", ", config.RequiredCapabilities);
            if (hostState is not null) ClientPackageFolder = hostState.ClientPackageFolder ?? string.Empty;
        }
        finally
        {
            _refreshingPackage = false;
        }
        _rememberedClientPackageFolder = hostState?.ClientPackageFolder;
        _lastAutoClientVersion = hostState?.LastAutoClientVersion ?? hostState?.ClientVersion;
        _lastAutoRuntime = hostState?.LastAutoRuntime ?? hostState?.Runtime;
        _lastAutoFingerprint = hostState?.LastAutoFingerprint ?? hostState?.Fingerprint;
        _lastAutoCapabilities = hostState?.LastAutoCapabilities ?? hostState?.Capabilities ?? string.Empty;
        _clientVersionAutoCleared = hostState?.ClientVersionAutoCleared ?? false;
        _runtimeAutoCleared = hostState?.RuntimeAutoCleared ?? false;
        _fingerprintAutoCleared = hostState?.FingerprintAutoCleared ?? false;
        _capabilitiesAutoCleared = hostState?.CapabilitiesAutoCleared ?? false;
        _manualClientVersionOverride = IsManual(hostState?.ClientVersionManual, _clientVersionAutoCleared,
            config.RequiredClientVersion, _lastAutoClientVersion);
        _manualRuntimeOverride = IsManual(hostState?.RuntimeManual, _runtimeAutoCleared,
            config.RequiredFalloutRuntimeVersion, _lastAutoRuntime);
        _manualFingerprintOverride = IsManual(hostState?.FingerprintManual, _fingerprintAutoCleared,
            config.RequiredContentManifestFingerprint, _lastAutoFingerprint);
        _manualCapabilityOverride = IsManual(hostState?.CapabilitiesManual,
            _capabilitiesAutoCleared, string.Join(", ", config.RequiredCapabilities), _lastAutoCapabilities);
        ResumeTokenLifetimeHours = config.ResumeTokenLifetimeHours;
        _resumeTokenLifetimeSeconds = config.ResumeTokenLifetimeSeconds;
        EnableLanDiscovery = config.LanDiscoveryEnabled;
        LanDiscoveryPort = config.LanDiscoveryPort;
        ListPublicly = config.PublicListing;
        _directoryUrl = config.DirectoryUrl;
        _unknownConfigKeys = config.UnknownKeys;

        RefreshClientPackage();
        DurableStatePathStatus = DurableStatePaths.Validate(DurableStatePath, _serverDir);

        _controller.LogReceived += line => Dispatcher.UIThread.Post(() => Append(line));
        _controller.RunningChanged += running =>
            Dispatcher.UIThread.Post(() =>
            {
                IsRunning = running;
                StatusText = running ? "Running" : "Stopped";
                StartCommand.NotifyCanExecuteChanged();
                StopCommand.NotifyCanExecuteChanged();
                SaveCommand.NotifyCanExecuteChanged();
                if (!running)
                {
                    Players.Clear();
                    StatsText = string.Empty;
                    PublicListingStatus = ListPublicly ? "Not listed (server stopped)" : "Not listed";
                }
            });

        _pollTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(3) };
        _pollTimer.Tick += async (_, _) => await PollAsync();
        _pollTimer.Start();

        if (_configLoadError is not null)
        {
            StatusText = "Config error";
            Append($"[host] configuration load failed: {_configLoadError}");
            Append("[host] Save and Start are disabled until commonwealth-server.json is repaired.");
        }
    }

    partial void OnEnablePersistenceChanged(bool value)
    {
        if (value && !RequireV2Identity) RequireV2Identity = true;
    }

    partial void OnResumeTokenLifetimeHoursChanged(int value)
    {
        if (_resumeTokenLifetimeSeconds is null) return;
        _resumeTokenLifetimeSeconds = null;
        Append($"[host] cleared the resume_token_lifetime_seconds override; using {value}h");
    }

    private AdminClient CreateAdminClient()
    {
        var tokenPath = Path.Combine(Path.GetDirectoryName(_configPath) ?? _serverDir, ".admin-token");
        return new AdminClient(AdminPort, tokenPath);
    }

    private async Task PollAsync()
    {
        if (!IsRunning || _polling) return;

        _polling = true;
        try
        {
            var admin = CreateAdminClient();
            var clients = await admin.ClientsAsync().ConfigureAwait(true);
            if (!clients.Ok)
            {
                ReportPollFailure($"clients: {clients.Describe()}");
                return;
            }

            ApplyClients(clients.Data);

            var status = await admin.StatusAsync().ConfigureAwait(true);
            if (!status.Ok)
            {
                ReportPollFailure($"status: {status.Describe()}");
                return;
            }

            ApplyStatus(status.Data);
            _lastPollFailure = null;
        }
        catch (Exception ex)
        {
            ReportPollFailure(ex.Message);
        }
        finally
        {
            _polling = false;
        }
    }

    private void ReportPollFailure(string reason)
    {
        StatusText = "Admin unreachable";
        if (_lastPollFailure == reason) return;
        _lastPollFailure = reason;
        Append($"[host] admin poll failed: {reason}");
    }

    private void ApplyClients(JsonObject? data)
    {
        if (data?["clients"] is not JsonArray array) return;

        var previouslySelected = SelectedPlayer?.PlayerId;
        Players.Clear();
        foreach (var node in array)
        {
            if (node is not JsonObject client) continue;

            Players.Add(new PlayerRow
            {
                PlayerId = (uint)(client["player_id"]?.GetValue<long>() ?? 0),
                Address = client["address"]?.GetValue<string>() ?? string.Empty,
                Port = (int)(client["port"]?.GetValue<long>() ?? 0),
                ConnectedAtUnixSeconds = client["connected_at"]?.GetValue<double>() ?? 0,
                LastPacketAtUnixSeconds = client["last_packet_at"]?.GetValue<double>(),
            });
        }

        if (previouslySelected is { } id)
        {
            foreach (var row in Players)
            {
                if (row.PlayerId != id) continue;
                SelectedPlayer = row;
                break;
            }
        }
    }

    private void ApplyStatus(JsonObject? data)
    {
        if (data is null) return;
        var connected = data["connected_clients"]?.GetValue<long>() ?? Players.Count;
        var maximum = data["max_players"]?.GetValue<long>() ?? MaxPlayers;
        var pending = data["pending_connections"]?.GetValue<long>() ?? 0;
        var received = data["packets_received"]?.GetValue<long>() ?? 0;
        var rejected = data["packets_rejected"]?.GetValue<long>() ?? 0;
        StatsText = $"{connected}/{maximum} players · {pending} pending · {received} packets, {rejected} rejected";
        if (IsRunning) StatusText = "Running";
        PublicListingStatus = PublicListingText.Describe(data["public_listing"] as JsonObject);
    }

    private void Append(string line)
    {
        Log.Add(line);
        while (Log.Count > MaxLogLines) Log.RemoveAt(0);
    }

    private ServerConfig CurrentConfig() => new()
    {
        ServerName = ServerName,
        ServerDescription = ServerDescription,
        Host = Host,
        Port = Port,
        MaxPlayers = MaxPlayers,
        AdminPort = AdminPort,
        RequireV2Identity = RequireV2Identity,
        EnablePersistence = EnablePersistence,
        DurableStatePath = DurableStatePath,
        RequiredClientVersion = Optional(RequiredClientVersion),
        RequiredFalloutRuntimeVersion = Optional(RequiredFalloutRuntimeVersion),
        RequiredContentManifestFingerprint = Optional(RequiredContentManifestFingerprint),
        RequiredCapabilities = RequiredCapabilitiesText
            .Split(',', StringSplitOptions.RemoveEmptyEntries)
            .Select(ServerConfig.TrimNative)
            .ToArray(),
        ResumeTokenLifetimeHours = ResumeTokenLifetimeHours,
        ResumeTokenLifetimeSeconds = _resumeTokenLifetimeSeconds,
        LanDiscoveryEnabled = EnableLanDiscovery,
        LanDiscoveryPort = LanDiscoveryPort,
        PublicListing = ListPublicly,
        DirectoryUrl = _directoryUrl,
        ServerPassword = string.IsNullOrEmpty(ServerPassword) ? null : ServerPassword,
        WorldMode = WorldMode,
        UnknownKeys = _unknownConfigKeys,
    };

    private static string? Optional(string? value)
    {
        var normalized = ServerConfig.TrimNative(value);
        return string.IsNullOrEmpty(normalized) ? null : normalized;
    }

    private static bool IsManual(bool? stateManual, bool autoCleared, string? configured, string? lastAuto)
    {
        if (stateManual is not null && stateManual.Value) return true;
        if (autoCleared)
            return !string.IsNullOrWhiteSpace(configured) &&
                !string.Equals(configured.Trim(), lastAuto?.Trim() ?? string.Empty, StringComparison.Ordinal);
        if (stateManual is null) return !string.IsNullOrWhiteSpace(configured);
        return !string.Equals(configured?.Trim() ?? string.Empty, lastAuto?.Trim() ?? string.Empty,
            StringComparison.Ordinal);
    }

    partial void OnDurableStatePathChanged(string value)
    {
        DurableStatePathStatus = DurableStatePaths.Validate(value, _serverDir);
    }

    public void SetDurableStatePath(string path)
    {
        DurableStatePath = ServerConfig.TrimNative(path);
    }

    internal readonly record struct PackagedIdentity(
        string ClientVersion,
        string Fingerprint,
        string? Runtime,
        string[] Capabilities);

    internal static string? DetectPackagedRuntime(string root)
    {
        try
        {
            var directory = new DirectoryInfo(Path.GetFullPath(root));
            DirectoryInfo? targetDirectory = null;
            if (IsNamed(directory.Parent, "build")) targetDirectory = directory;
            else if (IsNamed(directory, "Fallout 4") && IsNamed(directory.Parent?.Parent, "build"))
                targetDirectory = directory.Parent;
            else if (IsNamed(directory, "Data") && IsNamed(directory.Parent, "Fallout 4") &&
                     IsNamed(directory.Parent?.Parent?.Parent, "build"))
                targetDirectory = directory.Parent?.Parent;

            return targetDirectory?.Name.ToUpperInvariant() switch
            {
                "AE" => "1.11.240",
                "NG" => "1.10.984",
                "OG" => "1.10.163",
                _ => null,
            };

            static bool IsNamed(DirectoryInfo? directory, string name) =>
                directory is not null && string.Equals(directory.Name, name, StringComparison.OrdinalIgnoreCase);
        }
        catch (Exception ex) when (ex is ArgumentException or NotSupportedException or PathTooLongException)
        {
        }

        return null;
    }

    internal static PackagedIdentity? DetectPackagedIdentity(string root)
    {
        var candidates = new[]
        {
            Path.Combine(root, "CommonwealthOnlineClient.json"),
            Path.Combine(root, "F4SE", "Plugins", "CommonwealthOnlineClient.json"),
            Path.Combine(root, "Fallout 4", "Data", "CommonwealthOnlineClient.json"),
            Path.Combine(root, "Fallout 4", "Data", "F4SE", "Plugins", "CommonwealthOnlineClient.json"),
            Path.Combine(root, "Data", "CommonwealthOnlineClient.json"),
            Path.Combine(root, "Data", "F4SE", "Plugins", "CommonwealthOnlineClient.json"),
        };

        foreach (var path in candidates)
        {
            try
            {
                if (!File.Exists(path) || new FileInfo(path).Length > 64 * 1024) continue;
                using var document = JsonDocument.Parse(File.ReadAllText(path));
                var objectRoot = document.RootElement;
                if (objectRoot.ValueKind != JsonValueKind.Object ||
                    !objectRoot.TryGetProperty("clientVersion", out var client) ||
                    !objectRoot.TryGetProperty("contentManifestFingerprint", out var fingerprint) ||
                    client.ValueKind != JsonValueKind.String || fingerprint.ValueKind != JsonValueKind.String)
                    continue;

                var clientText = ServerConfig.TrimNative(client.GetString());
                var fingerprintText = ServerConfig.TrimNative(fingerprint.GetString()).ToLowerInvariant();
                if (clientText.Length is < 1 or > 64 || fingerprintText.Length != 64 ||
                    fingerprintText.Any(ch => !Uri.IsHexDigit(ch))) continue;

                var capabilities = Array.Empty<string>();
                if (objectRoot.TryGetProperty("capabilities", out var capabilityList) &&
                    capabilityList.ValueKind == JsonValueKind.Array)
                {
                    capabilities = capabilityList.EnumerateArray()
                        .Where(item => item.ValueKind == JsonValueKind.String)
                        .Select(item => ServerConfig.TrimNative(item.GetString()))
                        .Where(item => item.Length is > 0 and <= 64)
                        .Distinct(StringComparer.Ordinal)
                        .Take(64)
                        .ToArray();
                }

                return new PackagedIdentity(clientText, fingerprintText, DetectPackagedRuntime(root), capabilities);
            }
            catch (JsonException)
            {
            }
            catch (IOException)
            {
            }
            catch (UnauthorizedAccessException)
            {
            }
        }

        return null;
    }

    public void SetClientPackageFolder(string path)
    {
        ClientPackageFolder = ServerConfig.TrimNative(path);
    }

    private void SaveHostClientPackageState()
    {
        try
        {
            new HostClientPackageState
            {
                ClientPackageFolder = _rememberedClientPackageFolder,
                ClientVersion = RequiredClientVersion,
                Runtime = RequiredFalloutRuntimeVersion,
                Fingerprint = RequiredContentManifestFingerprint,
                Capabilities = RequiredCapabilitiesText,
                ClientVersionManual = _manualClientVersionOverride,
                RuntimeManual = _manualRuntimeOverride,
                FingerprintManual = _manualFingerprintOverride,
                CapabilitiesManual = _manualCapabilityOverride,
                ClientVersionAutoCleared = _clientVersionAutoCleared,
                RuntimeAutoCleared = _runtimeAutoCleared,
                FingerprintAutoCleared = _fingerprintAutoCleared,
                CapabilitiesAutoCleared = _capabilitiesAutoCleared,
                LastAutoClientVersion = _lastAutoClientVersion,
                LastAutoRuntime = _lastAutoRuntime,
                LastAutoFingerprint = _lastAutoFingerprint,
                LastAutoCapabilities = _lastAutoCapabilities,
            }.Save(_hostStatePath);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException or ArgumentException or NotSupportedException)
        {
        }
    }

    private void SetManualOverride(ref bool manualOverride, ref bool autoCleared, bool value)
    {
        autoCleared = value ? false : manualOverride || autoCleared;
        manualOverride = value;
        SaveHostClientPackageState();
    }

    public void SetSelectedCapabilities(IEnumerable<string> capabilities)
    {
        if (_manualCapabilityOverride) return;
        _refreshingPackage = true;
        try
        {
            RequiredCapabilitiesText = string.Join(", ", capabilities.Distinct(StringComparer.Ordinal));
        }
        finally
        {
            _refreshingPackage = false;
        }
    }

    [RelayCommand]
    private void RefreshClientPackage()
    {
        var root = string.IsNullOrWhiteSpace(ClientPackageFolder) ? _serverDir : ClientPackageFolder;
        try
        {
            root = Path.IsPathRooted(root) ? Path.GetFullPath(root) : Path.GetFullPath(root, _serverDir);
        }
        catch (Exception ex) when (ex is ArgumentException or NotSupportedException or PathTooLongException)
        {
            ClientPackageStatus = $"Invalid package folder: {ex.Message}";
            return;
        }

        var detected = DetectPackagedIdentity(root);
        DetectedCapabilities.Clear();
        if (detected is not { } identity)
        {
            DetectedIdentityText = "No packaged client manifest detected.";
            ClientPackageStatus = $"No CommonwealthOnlineClient.json found under {root}";
            SaveHostClientPackageState();
            return;
        }

        foreach (var capability in identity.Capabilities) DetectedCapabilities.Add(capability);
        _refreshingPackage = true;
        try
        {
            if (!_manualClientVersionOverride)
            {
                RequiredClientVersion = identity.ClientVersion;
                _lastAutoClientVersion = identity.ClientVersion;
                _clientVersionAutoCleared = false;
            }
            if (!_manualFingerprintOverride)
            {
                RequiredContentManifestFingerprint = identity.Fingerprint;
                _lastAutoFingerprint = identity.Fingerprint;
                _fingerprintAutoCleared = false;
            }
            if (!_manualRuntimeOverride)
            {
                RequiredFalloutRuntimeVersion = identity.Runtime;
                _lastAutoRuntime = identity.Runtime;
                _runtimeAutoCleared = false;
            }
            if (!_manualCapabilityOverride)
            {
                RequiredCapabilitiesText = string.Join(", ", identity.Capabilities);
                _lastAutoCapabilities = RequiredCapabilitiesText;
                _capabilitiesAutoCleared = false;
            }
        }
        finally
        {
            _refreshingPackage = false;
        }

        var runtime = identity.Runtime is null ? "runtime not proven by package path" : $"runtime {identity.Runtime}";
        DetectedIdentityText = $"Detected client {identity.ClientVersion}; manifest {identity.Fingerprint[..12]}...; {runtime}";
        ClientPackageStatus = $"Using package folder {root}";
        if (!string.IsNullOrWhiteSpace(ClientPackageFolder)) _rememberedClientPackageFolder = root;
        SaveHostClientPackageState();
    }

    partial void OnRequiredCapabilitiesTextChanged(string value)
    {
        if (!_refreshingPackage) SetManualOverride(ref _manualCapabilityOverride, ref _capabilitiesAutoCleared, !string.IsNullOrWhiteSpace(value));
    }

    partial void OnRequiredClientVersionChanged(string? value)
    {
        if (!_refreshingPackage) SetManualOverride(ref _manualClientVersionOverride, ref _clientVersionAutoCleared, !string.IsNullOrWhiteSpace(value));
    }

    partial void OnRequiredFalloutRuntimeVersionChanged(string? value)
    {
        if (!_refreshingPackage) SetManualOverride(ref _manualRuntimeOverride, ref _runtimeAutoCleared, !string.IsNullOrWhiteSpace(value));
    }

    partial void OnRequiredContentManifestFingerprintChanged(string? value)
    {
        if (!_refreshingPackage) SetManualOverride(ref _manualFingerprintOverride, ref _fingerprintAutoCleared, !string.IsNullOrWhiteSpace(value));
    }

    private bool TrySave()
    {
        if (_configLoadError is not null)
        {
            Append($"[host] configuration remains blocked: {_configLoadError}");
            return false;
        }

        try
        {
            var config = CurrentConfig();
            var errors = config.Validate();
            if (errors.Count > 0)
            {
                foreach (var error in errors) Append($"[host] configuration error: {error}");
                StatusText = "Config error";
                return false;
            }

            config.Save(_configPath);
            Append("[host] configuration saved");
            if (!IsRunning) StatusText = "Stopped";
            return true;
        }
        catch (Exception ex)
        {
            Append($"[host] configuration save failed: {ex.Message}");
            StatusText = "Config error";
            return false;
        }
    }

    [RelayCommand(CanExecute = nameof(CanSave))]
    private void Save() => TrySave();

    private bool CanSave() => _configLoadError is null && !IsRunning;

    [RelayCommand(CanExecute = nameof(CanStart))]
    private void Start()
    {
        if (!TrySave()) return;
        Append($"[host] starting server on {Host}:{Port}...");
        try
        {
            _controller.Start(_serverDir, _configPath);
        }
        catch (Exception ex)
        {
            StatusText = "Start failed";
            Append($"[host] server start failed: {ex.Message}");
        }
    }

    private bool CanStart() => !IsRunning && _configLoadError is null;

    [RelayCommand(CanExecute = nameof(CanStop))]
    private void Stop()
    {
        Append("[host] stopping server...");
        _controller.Stop();
    }

    private bool CanStop() => IsRunning;

    public void Dispose()
    {
        _pollTimer.Stop();
        _controller.Dispose();
    }

    [RelayCommand(CanExecute = nameof(CanActOnPlayer))]
    private async Task Kick()
    {
        if (SelectedPlayer is not { } player) return;
        await RunAdminAction(admin => admin.KickAsync(player.PlayerId, "Kicked by host"),
            $"[host] kick #{player.PlayerId}").ConfigureAwait(true);
    }

    private bool CanActOnPlayer() => IsRunning && SelectedPlayer is not null;

    [RelayCommand(CanExecute = nameof(CanActOnPlayer))]
    private async Task Ban()
    {
        if (SelectedPlayer is not { } player) return;
        await RunAdminAction(admin => admin.BanAsync(player.PlayerId, "Banned by host"),
            $"[host] ban #{player.PlayerId}").ConfigureAwait(true);
    }

    private async Task RunAdminAction(Func<AdminClient, Task<AdminResponse>> action, string label)
    {
        try
        {
            var response = await action(CreateAdminClient()).ConfigureAwait(true);
            Append($"{label} {(response.Ok ? "succeeded" : "FAILED")}: {response.Describe()}");
            await PollAsync().ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            Append($"{label} failed: {ex.Message}");
        }
    }
}
