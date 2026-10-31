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
            "error" => error.Length > 0 ? $"Error: {error}" : "Error",
            _ => "Not listed",
        };
    }
}
