using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using CommonwealthOnline.Host.Services;
using CommonwealthOnline.Host.ViewModels;

static class ServerPasswordRegression
{
    public static void Run(string root)
    {
        var path = Path.Combine(root, "server-password.json");
        var config = new ServerConfig { ServerPassword = "Secret pass" };
        config.Save(path);
        Require(ServerConfig.Load(path).ServerPassword == "Secret pass", "server password must round-trip");
        Require(JsonDocument.Parse(File.ReadAllText(path)).RootElement.GetProperty("server_password").GetString() == "Secret pass",
            "server_password must be written to the native config");

        new ServerConfig { ServerPassword = null }.Save(path);
        Require(!File.ReadAllText(path).Contains("server_password", StringComparison.Ordinal),
            "an absent password must omit the server_password key");

        var unknown = Path.Combine(root, "server-password-unknown.json");
        File.WriteAllText(unknown, """
        {
          "server_password": "Secret pass",
          "custom_flag": true
        }
        """);
        var loaded = ServerConfig.Load(unknown);
        loaded.ServerPassword = null;
        loaded.Save(unknown);
        var text = File.ReadAllText(unknown);
        Require(!text.Contains("server_password", StringComparison.Ordinal), "clearing a password must remove the key");
        Require(JsonDocument.Parse(text).RootElement.GetProperty("custom_flag").GetBoolean(),
            "unknown keys must survive a password save");

        Require(new ServerConfig { ServerPassword = "abc" }.Validate().Any(error => error.Contains("server_password", StringComparison.Ordinal)),
            "a 3-character password must be rejected");
        Require(new ServerConfig { ServerPassword = "pässword" }.Validate().Any(error => error.Contains("server_password", StringComparison.Ordinal)),
            "a non-ASCII password must be rejected");
        Require(new ServerConfig { ServerPassword = new string('a', 65) }.Validate().Any(error => error.Contains("server_password", StringComparison.Ordinal)),
            "a 65-character password must be rejected");
        Require(new ServerConfig { ServerPassword = "" }.Validate().Any(error => error.Contains("server_password", StringComparison.Ordinal)),
            "an empty password must be rejected");
        Require(new ServerConfig { ServerPassword = "abcd" }.Validate().Count == 0,
            "a 4-character password must be accepted");
        Require(new ServerConfig { ServerPassword = new string('a', 64) }.Validate().Count == 0,
            "a 64-character password must be accepted");

        var configPath = Path.Combine(AppContext.BaseDirectory, "commonwealth-server.json");
        File.WriteAllText(configPath, "{}\n");
        try
        {
            using var viewModel = new MainWindowViewModel();
            viewModel.ServerPassword = "Secret pass";
            Require(viewModel.IsPasswordProtected, "a set password must show Password protected");
            viewModel.SaveCommand.Execute(null);
            Require(ServerConfig.Load(configPath).ServerPassword == "Secret pass", "GUI password must be saved");

            viewModel.ServerPassword = "abc";
            viewModel.SaveCommand.Execute(null);
            Require(ServerConfig.Load(configPath).ServerPassword == "Secret pass",
                "an invalid GUI password must block save");

            viewModel.ServerPassword = "   ";
            Require(viewModel.IsPasswordProtected, "a 3-space password is still a set password");
            viewModel.SaveCommand.Execute(null);
            Require(ServerConfig.Load(configPath).ServerPassword == "Secret pass",
                "a 3-space GUI password must block save");

            viewModel.ServerPassword = "    ";
            viewModel.SaveCommand.Execute(null);
            Require(ServerConfig.Load(configPath).ServerPassword == "    ", "a 4-space GUI password must be saved");

            viewModel.ServerPassword = string.Empty;
            Require(!viewModel.IsPasswordProtected, "an empty GUI password must not show Password protected");
            viewModel.SaveCommand.Execute(null);
            Require(!File.ReadAllText(configPath).Contains("server_password", StringComparison.Ordinal),
                "an empty GUI password must omit the key and clear the earlier password");
        }
        finally
        {
            File.Delete(configPath);
        }

        var alias = Path.Combine(root, "server-password-alias.json");
        File.WriteAllText(alias, """
        {
          "serverPassword": "Secret pass"
        }
        """);
        ServerConfig.Load(alias).Save(alias);
        var aliasRoot = JsonDocument.Parse(File.ReadAllText(alias)).RootElement;
        Require(aliasRoot.GetProperty("server_password").GetString() == "Secret pass",
            "the serverPassword alias must re-save as server_password");
        Require(!aliasRoot.TryGetProperty("serverPassword", out _),
            "the serverPassword alias must not be written back");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
