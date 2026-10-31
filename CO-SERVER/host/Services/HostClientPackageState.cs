using System;
using System.IO;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace CommonwealthOnline.Host.Services;

internal sealed class HostClientPackageState
{
    private const int MaxFileBytes = 64 * 1024;
    private static readonly JsonSerializerOptions Options = new() { WriteIndented = true };

    [JsonPropertyName("client_package_folder")]
    public string? ClientPackageFolder { get; set; }

    [JsonPropertyName("client_version")]
    public string? ClientVersion { get; set; }

    [JsonPropertyName("runtime")]
    public string? Runtime { get; set; }

    [JsonPropertyName("fingerprint")]
    public string? Fingerprint { get; set; }

    [JsonPropertyName("capabilities")]
    public string Capabilities { get; set; } = string.Empty;

    [JsonPropertyName("client_version_manual")]
    public bool ClientVersionManual { get; set; }

    [JsonPropertyName("runtime_manual")]
    public bool RuntimeManual { get; set; }

    [JsonPropertyName("fingerprint_manual")]
    public bool FingerprintManual { get; set; }

    [JsonPropertyName("capabilities_manual")]
    public bool CapabilitiesManual { get; set; }

    [JsonPropertyName("client_version_auto_cleared")] public bool ClientVersionAutoCleared { get; set; }
    [JsonPropertyName("runtime_auto_cleared")] public bool RuntimeAutoCleared { get; set; }
    [JsonPropertyName("fingerprint_auto_cleared")] public bool FingerprintAutoCleared { get; set; }
    [JsonPropertyName("capabilities_auto_cleared")] public bool CapabilitiesAutoCleared { get; set; }

    [JsonPropertyName("last_auto_client_version")]
    public string? LastAutoClientVersion { get; set; }

    [JsonPropertyName("last_auto_runtime")]
    public string? LastAutoRuntime { get; set; }

    [JsonPropertyName("last_auto_fingerprint")]
    public string? LastAutoFingerprint { get; set; }

    [JsonPropertyName("last_auto_capabilities")]
    public string? LastAutoCapabilities { get; set; }

    public static HostClientPackageState? Load(string path)
    {
        try
        {
            if (!File.Exists(path) || new FileInfo(path).Length > MaxFileBytes) return null;
            return JsonSerializer.Deserialize<HostClientPackageState>(File.ReadAllText(path));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
        {
            return null;
        }
    }

    public void Save(string path)
    {
        var bytes = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(this, Options) + Environment.NewLine);
        if (bytes.Length > MaxFileBytes) throw new InvalidDataException("Host client package state is too large.");

        var directory = Path.GetDirectoryName(Path.GetFullPath(path));
        var temporary = Path.Combine(
            string.IsNullOrEmpty(directory) ? "." : directory,
            Path.GetFileName(path) + ".tmp-" + Environment.ProcessId);
        File.WriteAllBytes(temporary, bytes);
        File.Move(temporary, path, overwrite: true);
    }
}
