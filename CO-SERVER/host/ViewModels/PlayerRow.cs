using System;

namespace CommonwealthOnline.Host.ViewModels;

// Mirrors one entry of the native server's "clients" reply. The server reports
// identity and timing, not per-connection packet counters.
public sealed class PlayerRow
{
    public uint PlayerId { get; init; }
    public string Address { get; init; } = string.Empty;
    public int Port { get; init; }
    public double ConnectedAtUnixSeconds { get; init; }
    public double? LastPacketAtUnixSeconds { get; init; }

    public string Endpoint => Port > 0 ? $"{Address}:{Port}" : Address;

    public string ConnectedAtDisplay => Describe(ConnectedAtUnixSeconds);

    public string LastPacketDisplay => DescribeOptional(LastPacketAtUnixSeconds);

    public string Display =>
        $"#{PlayerId}   {Endpoint}   connected {Describe(ConnectedAtUnixSeconds)}   last packet {DescribeOptional(LastPacketAtUnixSeconds)}";

    private static string Describe(double unixSeconds) =>
        unixSeconds > 0
            ? DateTimeOffset.FromUnixTimeMilliseconds((long)(unixSeconds * 1000)).ToLocalTime().ToString("HH:mm:ss")
            : "unknown";

    private static string DescribeOptional(double? unixSeconds) =>
        unixSeconds is { } value ? Describe(value) : "none";
}
