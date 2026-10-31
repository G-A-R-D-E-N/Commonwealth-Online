using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;
using CommonwealthOnline.Host.Services;
using CommonwealthOnline.Host.ViewModels;

static class PublicListingRegression
{
    public static void Run(string root)
    {
        var defaults = Path.Combine(root, "listing-defaults.json");
        File.WriteAllText(defaults, "{}\n");
        Require(ServerConfig.Load(defaults).PublicListing, "an empty config must list publicly by default");

        var aliasOff = Path.Combine(root, "listing-alias-off.json");
        File.WriteAllText(aliasOff, "{ \"publicListing\": false }\n");
        Require(!ServerConfig.Load(aliasOff).PublicListing, "the publicListing alias must keep an explicit false off");

        var explicitOn = Path.Combine(root, "listing-explicit-on.json");
        File.WriteAllText(explicitOn, "{ \"public_listing\": true }\n");
        Require(ServerConfig.Load(explicitOn).PublicListing, "an explicit public_listing true must list publicly");

        var offPath = Path.Combine(root, "listing-roundtrip-off.json");
        new ServerConfig { PublicListing = false }.Save(offPath);
        Require(!JsonDocument.Parse(File.ReadAllText(offPath)).RootElement.GetProperty("public_listing").GetBoolean(),
            "an explicit listing-off must be written to the native config");
        Require(!ServerConfig.Load(offPath).PublicListing, "an explicit listing-off must survive a reload");

        Require(new ServerConfig { ServerName = "   " }.Validate()
                .Any(error => error.Contains("server_name", StringComparison.Ordinal) && error.Contains("blank", StringComparison.Ordinal)),
            "a whitespace-only server name must be rejected while listing publicly");
        Require(new ServerConfig { ServerName = "   ", PublicListing = false }.Validate().Count == 0,
            "a whitespace-only name must still load while public listing is off");

        Require(new ServerConfig { ServerName = "bad\u0007name" }.Validate()
                .Any(error => error.Contains("server_name", StringComparison.Ordinal) && error.Contains("printable", StringComparison.Ordinal)),
            "control characters in the server name must be rejected while listing publicly");
        Require(new ServerConfig { ServerName = "bad\u0007name", PublicListing = false }.Validate().Count == 0,
            "control characters in the name must still load while public listing is off");
        Require(new ServerConfig { ServerName = "Name\t" }.Validate()
                .Any(error => error.Contains("printable", StringComparison.Ordinal)),
            "a trailing tab in the server name must be rejected like the directory");
        Require(new ServerConfig { ServerName = "Name\n" }.Validate()
                .Any(error => error.Contains("printable", StringComparison.Ordinal)),
            "a trailing newline in the server name must be rejected like the directory");
        Require(new ServerConfig { ServerName = "Name\t", PublicListing = false }.Validate().Count == 0 &&
                new ServerConfig { ServerName = "Name\n", PublicListing = false }.Validate().Count == 0,
            "tabs and newlines in the name must still load while public listing is off");

        Require(new ServerConfig { ServerDescription = "bad\u0001description" }.Validate()
                .Any(error => error.Contains("server_description", StringComparison.Ordinal) && error.Contains("printable", StringComparison.Ordinal)),
            "control characters in the description must be rejected while listing publicly");
        Require(new ServerConfig { ServerDescription = "Description\t" }.Validate()
                .Any(error => error.Contains("printable", StringComparison.Ordinal)),
            "a tab in the description must be rejected like the directory");
        Require(new ServerConfig { ServerDescription = "Description\n" }.Validate()
                .Any(error => error.Contains("printable", StringComparison.Ordinal)),
            "a newline in the description must be rejected like the directory");
        Require(new ServerConfig { ServerDescription = "bad\u0001description", PublicListing = false }.Validate().Count == 0 &&
                new ServerConfig { ServerDescription = "Description\t", PublicListing = false }.Validate().Count == 0 &&
                new ServerConfig { ServerDescription = "Description\n", PublicListing = false }.Validate().Count == 0,
            "control characters in the description must still load while public listing is off");

        var blankMessage = new ServerConfig { ServerName = "   " }.Validate().Single();
        Require(blankMessage.Contains("give the server a printable name", StringComparison.Ordinal) &&
                blankMessage.Contains("\"public_listing\": false", StringComparison.Ordinal),
            "the blank-name error must say how to fix it");
        var printableMessage = new ServerConfig { ServerName = "bad\u0007name" }.Validate()
            .Single(error => error.Contains("printable", StringComparison.Ordinal));
        Require(printableMessage.Contains("remove tabs", StringComparison.Ordinal) &&
                printableMessage.Contains("\"public_listing\": false", StringComparison.Ordinal),
            "the printable-name error must say how to fix it");

        Require(new ServerConfig { ServerName = new string('a', 64) }.Validate().Count == 0,
            "a 64-byte server name must validate for public listing");
        Require(new ServerConfig { ServerName = "  Padded name  " }.Validate().Count == 0,
            "a padded server name must be trimmed before public listing validation");
        Require(new ServerConfig { ServerDescription = new string('é', 128) }.Validate().Count == 0,
            "a 256-byte server description must validate for public listing");
        Require(new ServerConfig { ServerDescription = new string('é', 129) }.Validate().Count > 0,
            "a 257-byte server description must be rejected for public listing");

        var invalidLoad = Path.Combine(root, "listing-invalid-load.json");
        File.WriteAllText(invalidLoad, """
        {
          "server_name": "bad\u0007name"
        }
        """);
        RequireThrows<InvalidDataException>(() => ServerConfig.Load(invalidLoad),
            "a listed config with control characters must fail to load");
        var invalidOffLoad = Path.Combine(root, "listing-invalid-off-load.json");
        File.WriteAllText(invalidOffLoad, """
        {
          "server_name": "bad\u0007name",
          "public_listing": false
        }
        """);
        Require(ServerConfig.Load(invalidOffLoad).ServerName == "bad\u0007name",
            "the same config must still load while public listing is off");

        var blankLoad = Path.Combine(root, "listing-blank-load.json");
        File.WriteAllText(blankLoad, """
        {
          "server_name": "   ",
          "public_listing": true
        }
        """);
        RequireThrows<InvalidDataException>(() => ServerConfig.Load(blankLoad),
            "a listed config with a whitespace-only name must fail to load");

        var nonPublic = PublicListingText.DescribeError(
            "non_public_address (HTTP 422): Registration must come from the server's public IPv4 address.");
        Require(nonPublic.Contains("CGNAT", StringComparison.Ordinal) &&
                nonPublic.Contains("home router", StringComparison.Ordinal) &&
                nonPublic.Contains("STEAM P2P", StringComparison.Ordinal) &&
                nonPublic.Contains("direct connect", StringComparison.Ordinal),
            "non_public_address must explain CGNAT or a home router and suggest STEAM P2P or direct connect");
        Require(PublicListingText.Describe(new JsonObject
                {
                    ["state"] = "error",
                    ["error"] = "non_public_address (HTTP 422): x",
                }).StartsWith("Error: ", StringComparison.Ordinal),
            "a non_public_address error must still read as an error");
        Require(PublicListingText.DescribeError("ipv6_not_listening (HTTP 422): x")
                .Contains("IPv6", StringComparison.Ordinal),
            "an IPv6 registration rejection must be explained in plain language");
        Require(PublicListingText.DescribeError("something unusual (HTTP 500): raw")
                .Contains("something unusual", StringComparison.Ordinal),
            "an unknown listing error must keep its raw detail");
        Require(PublicListingText.Describe(new JsonObject { ["state"] = "disabled" }) == "Not listed",
            "a disabled listing must read as Not listed");
        Require(PublicListingText.PortForwardHint(7787).Contains("UDP port 7787", StringComparison.Ordinal),
            "the port-forward hint must name the configured game port");

        var configPath = Path.Combine(AppContext.BaseDirectory, "commonwealth-server.json");
        File.WriteAllText(configPath, "{}\n");
        try
        {
            using (var viewModel = new MainWindowViewModel())
            {
                Require(viewModel.ListPublicly, "the Host must open with public listing on by default");
                Require(viewModel.PublicListingHint.Contains("UDP port 7777", StringComparison.Ordinal),
                    "the Host must show the game-port forwarding hint");
                Require(viewModel.PublicListingHint.Contains("never changes", StringComparison.Ordinal),
                    "the forwarding hint must say the Host never changes the router or firewall");
                viewModel.ListPublicly = false;
                viewModel.SaveCommand.Execute(null);
                Require(!ServerConfig.Load(configPath).PublicListing,
                    "turning public listing off in the Host must persist an explicit false");
            }

            using (var reopened = new MainWindowViewModel())
            {
                Require(!reopened.ListPublicly, "the Host must reopen with public listing off after an explicit false");
            }
        }
        finally
        {
            File.Delete(configPath);
        }

        var blockedText = "{ \"server_name\": \"   \" }\n";
        File.WriteAllText(configPath, blockedText);
        try
        {
            using var blocked = new MainWindowViewModel();
            Require(blocked.StatusText == "Config error",
                "a listed config with a blank name must open as a config error, not silently list");
            Require(blocked.Log.Any(line => line.Contains("cannot be blank", StringComparison.Ordinal) &&
                                            line.Contains("\"public_listing\": false", StringComparison.Ordinal)),
                "the Host startup error must say how to fix a blank name");
            Require(blocked.Log.Any(line => line.Contains("public_listing", StringComparison.Ordinal) &&
                                            line.Contains("printable name", StringComparison.Ordinal)),
                "the Host startup guidance must name the config key and the name fix");
            Require(!blocked.SaveCommand.CanExecute(null),
                "Save must stay disabled until the rejected config is repaired by hand");
            Require(File.ReadAllText(configPath) == blockedText,
                "the Host must not rewrite a config it rejected at startup");
        }
        finally
        {
            File.Delete(configPath);
        }
    }

    private static void RequireThrows<TException>(Action action, string message) where TException : Exception
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

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
