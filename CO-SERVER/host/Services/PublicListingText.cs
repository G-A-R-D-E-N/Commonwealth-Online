using System;
using System.Text.Json.Nodes;

namespace CommonwealthOnline.Host.Services;

internal static class PublicListingText
{
    public static string Describe(JsonObject? listing)
    {
        var state = listing?["state"]?.GetValue<string>() ?? "disabled";
        var address = listing?["publicAddress"]?.GetValue<string>() ?? string.Empty;
        var error = listing?["error"]?.GetValue<string>() ?? string.Empty;
        return state switch
        {
            "registering" => "Registering…",
            "registered" => address.Length > 0 ? $"Registered as {address}" : "Registered",
            "error" => error.Length > 0
                ? $"Error: {DescribeError(error)}"
                : "Error: the directory reported an unspecified error.",
            _ => "Not listed",
        };
    }

    public static string DescribeError(string error)
    {
        if (error.Contains("non_public_address", StringComparison.Ordinal))
            return "the directory cannot use a public address for this server, so it cannot be listed. " +
                   "A server behind CGNAT, or behind a home router without a public address, cannot be listed. " +
                   "Forward the game UDP port on the router, or use STEAM P2P or direct connect instead.";
        if (error.Contains("ipv6_not_listening", StringComparison.Ordinal))
            return "the directory reached this server over IPv6, but the server does not listen on IPv6. Bind the server to :: or register over IPv4.";
        if (error.Contains("invalid_metadata", StringComparison.Ordinal))
            return "the directory rejected the server name or description. Use a name of 1-64 printable UTF-8 bytes and a description of at most 256, with no control characters.";
        if (error.Contains("unsupported_address", StringComparison.Ordinal))
            return "public listings require a public IPv4 or IPv6 address.";
        if (error.Contains("directory_full", StringComparison.Ordinal))
            return "the public directory is full right now; the Host keeps retrying.";
        if (error.Contains("too_many_servers", StringComparison.Ordinal))
            return "too many servers are already listed from this address.";
        if (error.Contains("address_not_allowed", StringComparison.Ordinal))
            return "the directory takes the public address from the request itself; remove any manual address from the registration.";
        if (error.Contains("Directory unreachable", StringComparison.Ordinal))
            return "the directory cannot be reached right now; the Host keeps retrying.";
        return error;
    }

    public static string PortForwardHint(int port) =>
        $"Forward UDP port {port} on your router so players can reach this server. " +
        "The Host never changes your router or firewall.";
}
