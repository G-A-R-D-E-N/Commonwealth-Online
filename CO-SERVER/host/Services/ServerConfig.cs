using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace CommonwealthOnline.Host.Services;

public sealed class ServerConfig
{
    [JsonPropertyName("host")]
    public string Host { get; set; } = "0.0.0.0";

    [JsonPropertyName("port")]
    public int Port { get; set; } = 7777;

    [JsonPropertyName("server_name")]
    public string ServerName { get; set; } = "Commonwealth Online Server";

    [JsonPropertyName("server_description")]
    public string ServerDescription { get; set; } = string.Empty;

    [JsonPropertyName("max_players")]
    public int MaxPlayers { get; set; } = 16;

    [JsonPropertyName("admin_port")]
    public int AdminPort { get; set; } = 7779;

    [JsonPropertyName("require_v2_identity")]
    public bool RequireV2Identity { get; set; } = true;

    [JsonPropertyName("enable_persistence")]
    public bool EnablePersistence { get; set; }

    [JsonIgnore]
    public string DurableStatePath { get; set; } = "durable-state.json";

    [JsonPropertyName("required_client_version")]
    public string? RequiredClientVersion { get; set; }

    [JsonPropertyName("required_fallout_runtime_version")]
    public string? RequiredFalloutRuntimeVersion { get; set; }

    [JsonPropertyName("required_content_manifest_fingerprint")]
    public string? RequiredContentManifestFingerprint { get; set; }

    [JsonPropertyName("required_capabilities")]
    public string[] RequiredCapabilities { get; set; } = Array.Empty<string>();

    [JsonPropertyName("resume_token_lifetime_hours")]
    public int ResumeTokenLifetimeHours { get; set; } = 720;

    [JsonPropertyName("resume_token_lifetime_seconds")]
    public int? ResumeTokenLifetimeSeconds { get; set; }

    [JsonPropertyName("lan_discovery_enabled")]
    public bool LanDiscoveryEnabled { get; set; } = true;

    [JsonPropertyName("lan_discovery_port")]
    public int LanDiscoveryPort { get; set; } = 7778;

    public const string DefaultDirectoryUrl = "https://oeajjfquvmdxqqtiotfa.supabase.co/functions/v1/server-directory";

    [JsonPropertyName("public_listing")]
    public bool PublicListing { get; set; }

    [JsonPropertyName("directory_url")]
    public string DirectoryUrl { get; set; } = DefaultDirectoryUrl;

    [JsonPropertyName("server_password")]
    public string? ServerPassword { get; set; }

    [JsonPropertyName("worldMode")]
    public string WorldMode { get; set; } = GameModes.CoopStory;

    [JsonIgnore]
    public IReadOnlyDictionary<string, JsonElement> UnknownKeys { get; set; } =
        new Dictionary<string, JsonElement>();

    private static readonly JsonSerializerOptions Options = new()
    {
        WriteIndented = true,
    };

    public static ServerConfig Load(string path)
    {
        if (!File.Exists(path)) return new ServerConfig();

        try
        {
            using var document = JsonDocument.Parse(File.ReadAllText(path));
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object)
                throw new InvalidDataException("Server config root must be a JSON object.");

            var config = new ServerConfig
            {
                Host = ReadString(root, "host") ?? "0.0.0.0",
                Port = ReadInt(root, "port") ?? 7777,
                ServerName = ReadString(root, "server_name", "serverName") ?? "Commonwealth Online Server",
                ServerDescription = ReadString(root, "server_description", "serverDescription") ?? string.Empty,
                MaxPlayers = ReadInt(root, "max_players", "maxPlayers") ?? 16,
                AdminPort = ReadInt(root, "admin_port", "adminPort") ?? 7779,
                RequireV2Identity = ReadBool(root, "require_v2_identity", "requireV2Identity") ?? true,
                EnablePersistence = ReadBool(root, "enable_persistence", "enablePersistence") ?? true,
                DurableStatePath = ReadString(root, "durable_state_path", "durableStatePath") ?? "durable-state.json",
                RequiredClientVersion = ReadOptionalString(root, "required_client_version", "requiredClientVersion"),
                RequiredFalloutRuntimeVersion = ReadOptionalString(root, "required_fallout_runtime_version", "requiredFalloutRuntimeVersion"),
                RequiredContentManifestFingerprint = ReadOptionalString(root, "required_content_manifest_fingerprint", "requiredContentManifestFingerprint"),
                RequiredCapabilities = ReadStrings(root, "required_capabilities", "requiredCapabilities") ?? Array.Empty<string>(),
                ResumeTokenLifetimeHours = ReadInt(root, "resume_token_lifetime_hours", "resumeTokenLifetimeHours") ?? 720,
                ResumeTokenLifetimeSeconds = ReadInt(root, "resume_token_lifetime_seconds", "resumeTokenLifetimeSeconds"),
                LanDiscoveryEnabled = ReadBool(root, "lan_discovery_enabled", "lanDiscoveryEnabled") ?? true,
                LanDiscoveryPort = ReadInt(root, "lan_discovery_port", "lanDiscoveryPort") ?? 7778,
                PublicListing = ReadBool(root, "public_listing", "publicListing") ?? false,
                DirectoryUrl = ReadOptionalString(root, "directory_url", "directoryUrl") ?? DefaultDirectoryUrl,
                ServerPassword = ReadString(root, "server_password", "serverPassword"),
                WorldMode = GameModes.Normalize(ReadString(root, "worldMode", "world_mode", "questMode", "quest_mode")),
                UnknownKeys = CollectUnknownKeys(root),
            };

            var errors = config.Validate();
            if (errors.Count > 0)
                throw new InvalidDataException("Invalid server config: " + string.Join("; ", errors));
            return config;
        }
        catch (InvalidDataException)
        {
            throw;
        }
        catch (JsonException ex)
        {
            throw new InvalidDataException($"Server config is not valid JSON: {ex.Message}", ex);
        }
    }

    public IReadOnlyList<string> Validate()
    {
        var errors = new List<string>();
        if (TrimNative(Host).Length == 0) errors.Add("host cannot be empty");
        if (Port is < 1 or > 65535) errors.Add("port must be 1-65535");
        if (AdminPort is < 1 or > 65535) errors.Add("admin_port must be 1-65535");
        else if (AdminPort == Port) errors.Add("admin_port must differ from the game port");
        if (string.IsNullOrEmpty(ServerName) || Encoding.UTF8.GetByteCount(ServerName) > 64) errors.Add("server_name must be 1-64 UTF-8 bytes");
        if (Encoding.UTF8.GetByteCount(ServerDescription) > 256) errors.Add("server_description must be at most 256 UTF-8 bytes");
        if (MaxPlayers is < 1 or > 16) errors.Add("max_players must be 1-16");
        if (!IsExplicitIpv4(Host))
            errors.Add("GNS requires an explicit IPv4 host such as 0.0.0.0 or 127.0.0.1");
        if (!RequireV2Identity) errors.Add("require_v2_identity can no longer be disabled; Protocol V2 identity is mandatory");
        if (EnablePersistence && TrimNative(DurableStatePath).Length == 0) errors.Add("durable state path cannot be empty when persistence is enabled");
        if (ResumeTokenLifetimeHours is < 1 or > 8760) errors.Add("resume token lifetime must be 1-8760 hours");
        if (ResumeTokenLifetimeSeconds is { } seconds && seconds is < 1 or > 31_536_000)
            errors.Add("resume_token_lifetime_seconds must be 1-31536000");
        if (LanDiscoveryEnabled)
        {
            if (LanDiscoveryPort is < 1 or > 65535) errors.Add("lan_discovery_port must be 1-65535");
            else if (LanDiscoveryPort == Port)
                errors.Add("lan_discovery_port must differ from the game port");
        }
        if (PublicListing && !IsAllowedDirectoryUrl(DirectoryUrl))
            errors.Add("directory_url must be an https:// URL (plain http only for 127.0.0.1 or localhost)");
        if (ServerPassword is not null && (ServerPassword.Length is < 4 or > 64 || ServerPassword.Any(ch => ch is < ' ' or > '~')))
            errors.Add("server_password must be 4-64 printable ASCII characters when set");
        if (!GameModes.IsKnown(WorldMode))
            errors.Add("worldMode must be one of coop-story, free-roam");
        ValidateOptionalVersion(RequiredClientVersion, "required client version", errors);
        ValidateOptionalVersion(RequiredFalloutRuntimeVersion, "required Fallout runtime", errors);

        if (TrimNative(RequiredContentManifestFingerprint).Length > 0)
        {
            var fingerprint = TrimNative(RequiredContentManifestFingerprint);
            if (fingerprint.Length != 64 || fingerprint.Any(ch => !Uri.IsHexDigit(ch)))
                errors.Add("content manifest fingerprint must be a 64-character SHA-256 hex string");
        }

        if (RequiredCapabilities.Length > 64) errors.Add("required capabilities cannot exceed 64 entries");
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var capability in RequiredCapabilities)
        {
            if (TrimNative(capability).Length == 0 || Encoding.UTF8.GetByteCount(capability) > 64)
                errors.Add("required capability names must be 1-64 non-whitespace characters");
            else if (!seen.Add(capability))
                errors.Add($"duplicate required capability '{capability}'");
        }
        return errors;
    }

    public void Save(string path)
    {
        var errors = Validate();
        if (errors.Count > 0) throw new InvalidDataException("Invalid server config: " + string.Join("; ", errors));

        var native = new Dictionary<string, object?>
        {
            ["host"] = Host,
            ["port"] = Port,
            ["admin_port"] = AdminPort,
            ["server_name"] = ServerName,
            ["server_description"] = ServerDescription,
            ["max_players"] = MaxPlayers,
            ["require_v2_identity"] = RequireV2Identity,
            ["enable_persistence"] = EnablePersistence,
            ["durable_state_path"] = DurableStatePath,
            ["required_client_version"] = NormalizeOptional(RequiredClientVersion),
            ["required_fallout_runtime_version"] = NormalizeOptional(RequiredFalloutRuntimeVersion),
            ["required_content_manifest_fingerprint"] = NormalizeOptional(RequiredContentManifestFingerprint),
            ["required_capabilities"] = RequiredCapabilities,
            ["resume_token_lifetime_hours"] = ResumeTokenLifetimeHours,
            ["lan_discovery_enabled"] = LanDiscoveryEnabled,
            ["lan_discovery_port"] = LanDiscoveryPort,
            ["public_listing"] = PublicListing,
            ["directory_url"] = DirectoryUrl,
            ["worldMode"] = GameModes.Normalize(WorldMode),
        };
        if (ResumeTokenLifetimeSeconds is { } seconds)
        {
            native["resume_token_lifetime_seconds"] = seconds;
        }
        if (ServerPassword is not null)
        {
            native["server_password"] = ServerPassword;
        }

        foreach (var (key, value) in UnknownKeys)
        {
            if (!native.ContainsKey(key))
            {
                native[key] = value;
            }
        }

        var text = JsonSerializer.Serialize(native, Options) + Environment.NewLine;
        var directory = Path.GetDirectoryName(Path.GetFullPath(path));
        var temporary = Path.Combine(
            string.IsNullOrEmpty(directory) ? "." : directory,
            Path.GetFileName(path) + ".tmp-" + Environment.ProcessId.ToString(CultureInfo.InvariantCulture));
        File.WriteAllText(temporary, text);
        File.Move(temporary, path, overwrite: true);
    }

    private static readonly HashSet<string> KnownKeys = new(StringComparer.Ordinal)
    {
        "host", "port", "server_name", "serverName", "server_description", "serverDescription",
        "max_players", "maxPlayers", "admin_port", "adminPort",
        "require_v2_identity", "requireV2Identity", "enable_persistence", "enablePersistence",
        "durable_state_path", "durableStatePath",
        "required_client_version", "requiredClientVersion",
        "required_fallout_runtime_version", "requiredFalloutRuntimeVersion",
        "required_content_manifest_fingerprint", "requiredContentManifestFingerprint",
        "required_capabilities", "requiredCapabilities",
        "resume_token_lifetime_hours", "resumeTokenLifetimeHours",
        "resume_token_lifetime_seconds", "resumeTokenLifetimeSeconds",
        "lan_discovery_enabled", "lanDiscoveryEnabled",
        "lan_discovery_port", "lanDiscoveryPort",
        "public_listing", "publicListing",
        "directory_url", "directoryUrl",
        "server_password", "serverPassword",
        "worldMode", "world_mode", "questMode", "quest_mode",
    };

    private static Dictionary<string, JsonElement> CollectUnknownKeys(JsonElement root)
    {
        var extra = new Dictionary<string, JsonElement>(StringComparer.Ordinal);
        foreach (var property in root.EnumerateObject())
        {
            if (!KnownKeys.Contains(property.Name))
            {
                extra[property.Name] = property.Value.Clone();
            }
        }

        return extra;
    }

    private static JsonElement? Find(JsonElement root, params string[] names)
    {
        foreach (var name in names)
            if (root.TryGetProperty(name, out var value) && value.ValueKind != JsonValueKind.Null) return value;
        return null;
    }

    private static string? ReadString(JsonElement root, params string[] names)
    {
        var value = Find(root, names);
        if (value is null || value.Value.ValueKind == JsonValueKind.Null) return null;
        if (value.Value.ValueKind != JsonValueKind.String)
            throw new InvalidDataException($"{names[0]} must be a string or null");
        return value.Value.GetString();
    }

    private static string? ReadOptionalString(JsonElement root, params string[] names)
    {
        var value = TrimNative(ReadString(root, names));
        return string.IsNullOrEmpty(value) ? null : value;
    }

    private static int? ReadInt(JsonElement root, params string[] names)
    {
        var value = Find(root, names);
        if (value is null || value.Value.ValueKind == JsonValueKind.Null) return null;
        if (value.Value.ValueKind == JsonValueKind.Number && value.Value.TryGetInt32(out var number)) return number;
        if (value.Value.ValueKind == JsonValueKind.String && TryParseNativeInt32(value.Value.GetString(), out number)) return number;
        throw new InvalidDataException($"{names[0]} must be an integer");
    }

    private static bool? ReadBool(JsonElement root, params string[] names)
    {
        var value = Find(root, names);
        if (value is null || value.Value.ValueKind == JsonValueKind.Null) return null;
        if (value.Value.ValueKind is JsonValueKind.True or JsonValueKind.False) return value.Value.GetBoolean();
        if (value.Value.ValueKind == JsonValueKind.Number && value.Value.TryGetInt32(out var number) && number is 0 or 1) return number == 1;
        if (value.Value.ValueKind == JsonValueKind.String)
        {
            return TrimNative(value.Value.GetString()).ToLowerInvariant() switch
            {
                "1" or "true" or "yes" or "on" => true,
                "0" or "false" or "no" or "off" or "" => false,
                _ => throw new InvalidDataException($"{names[0]} must be a boolean"),
            };
        }
        throw new InvalidDataException($"{names[0]} must be a boolean");
    }

    private static string[]? ReadStrings(JsonElement root, params string[] names)
    {
        var value = Find(root, names);
        if (value is null || value.Value.ValueKind == JsonValueKind.Null) return null;
        if (value.Value.ValueKind != JsonValueKind.Array)
            throw new InvalidDataException($"{names[0]} must be an array of strings");
        var result = new List<string>();
        foreach (var item in value.Value.EnumerateArray())
        {
            if (item.ValueKind != JsonValueKind.String || item.GetString() is not { } text)
                throw new InvalidDataException($"{names[0]} must contain only strings");
            result.Add(TrimNative(text));
        }
        return result.ToArray();
    }

    private static void ValidateOptionalVersion(string? value, string field, List<string> errors)
    {
        if (value is null) return;
        if (TrimNative(value).Length == 0 || Encoding.UTF8.GetByteCount(value) > 64 || value.Any(ch => ch < 0x20 || ch == 0x7f))
            errors.Add($"{field} must be 1-64 printable characters when set");
    }

    private static string? NormalizeOptional(string? value)
    {
        var normalized = TrimNative(value);
        return string.IsNullOrEmpty(normalized) ? null : normalized;
    }

    internal static string TrimNative(string? value)
    {
        if (string.IsNullOrEmpty(value)) return string.Empty;
        var start = 0;
        var end = value.Length;
        while (start < end && IsNativeWhitespace(value[start])) start++;
        while (end > start && IsNativeWhitespace(value[end - 1])) end--;
        return value[start..end];
    }

    private static bool IsNativeWhitespace(char value) => value is '\t' or '\n' or '\v' or '\f' or '\r' or ' ';

    internal static bool IsAllowedDirectoryUrl(string? value)
    {
        if (string.IsNullOrEmpty(value) || value.Length > 512) return false;
        if (value.Any(ch => ch <= 0x20 || ch >= 0x7f)) return false;
        var schemeEnd = value.IndexOf("://", StringComparison.Ordinal);
        if (schemeEnd < 0) return false;
        var afterScheme = value[(schemeEnd + 3)..];
        var authorityEnd = afterScheme.IndexOfAny(new[] { '/', '?', '#' });
        var authority = authorityEnd < 0 ? afterScheme : afterScheme[..authorityEnd];
        if (authority.Contains('@')) return false;
        var colon = authority.IndexOf(':');
        var host = colon < 0 ? authority : authority[..colon];
        return value[..schemeEnd] switch
        {
            "https" => host.Length > 0,
            "http" => host is "127.0.0.1" or "localhost",
            _ => false,
        };
    }

    private static bool IsExplicitIpv4(string value)
    {
        var parts = TrimNative(value).Split('.', StringSplitOptions.None);
        if (parts.Length != 4) return false;

        foreach (var part in parts)
        {
            if (part.Length == 0) return false;
            var octet = 0;
            foreach (var character in part)
            {
                if (character is < '0' or > '9') return false;
                octet = octet * 10 + character - '0';
                if (octet > 255) return false;
            }
        }

        return true;
    }

    private static bool TryParseNativeInt32(string? raw, out int value)
    {
        value = 0;
        var text = TrimNative(raw);
        if (string.IsNullOrEmpty(text) || text[0] == '+') return false;
        if (text[0] == '-')
        {
            if (text.Length == 1 || !long.TryParse(text.AsSpan(1), NumberStyles.None, CultureInfo.InvariantCulture, out var magnitude) || magnitude > 2_147_483_648)
                return false;
            value = magnitude == 2_147_483_648 ? int.MinValue : -(int)magnitude;
            return true;
        }

        return int.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out value);
    }
}
