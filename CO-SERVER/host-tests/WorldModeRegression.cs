using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using CommonwealthOnline.Host.Services;
using CommonwealthOnline.Host.ViewModels;

static class WorldModeRegression
{
    public static void Run(string root)
    {
        var path = Path.Combine(root, "world-mode.json");
        new ServerConfig { WorldMode = GameModes.FreeRoam }.Save(path);
        var saved = JsonDocument.Parse(File.ReadAllText(path)).RootElement;
        Require(saved.GetProperty("worldMode").GetString() == "free-roam", "worldMode must be written to the native config");
        var loaded = ServerConfig.Load(path);
        Require(loaded.WorldMode == "free-roam", "worldMode must round-trip");

        var defaults = Path.Combine(root, "world-mode-defaults.json");
        new ServerConfig().Save(defaults);
        var defaultsRoot = JsonDocument.Parse(File.ReadAllText(defaults)).RootElement;
        Require(defaultsRoot.GetProperty("worldMode").GetString() == "coop-story", "a new config must default to coop-story");
        Require(!defaultsRoot.TryGetProperty("doorsShared", out _), "the Host must not write a doorsShared key");
        Require(new ServerConfig().Validate().Count == 0, "the shipped world mode defaults must validate");

        var omitted = Path.Combine(root, "world-mode-omitted.json");
        File.WriteAllText(omitted, "{}\n");
        var omittedConfig = ServerConfig.Load(omitted);
        Require(omittedConfig.WorldMode == "coop-story", "an omitted worldMode must default to coop-story");

        var legacyDoors = Path.Combine(root, "world-mode-legacy-doors.json");
        File.WriteAllText(legacyDoors, """
        {
          "worldMode": "free-roam",
          "doorsShared": false
        }
        """);
        ServerConfig.Load(legacyDoors).Save(legacyDoors);
        var legacyDoorsRoot = JsonDocument.Parse(File.ReadAllText(legacyDoors)).RootElement;
        Require(legacyDoorsRoot.TryGetProperty("doorsShared", out var preservedDoors)
                && preservedDoors.ValueKind == JsonValueKind.False,
            "a hand-added doorsShared must be preserved verbatim through UnknownKeys");

        var alias = Path.Combine(root, "world-mode-alias.json");
        File.WriteAllText(alias, """
        {
          "world_mode": " Free-Roam ",
          "custom_flag": true
        }
        """);
        var aliasConfig = ServerConfig.Load(alias);
        Require(aliasConfig.WorldMode == "free-roam", "the world_mode alias must be accepted and normalized");
        aliasConfig.Save(alias);
        var aliasRoot = JsonDocument.Parse(File.ReadAllText(alias)).RootElement;
        Require(aliasRoot.GetProperty("worldMode").GetString() == "free-roam", "the world_mode alias must re-save as worldMode");
        Require(!aliasRoot.TryGetProperty("world_mode", out _), "the world_mode alias must not be written back");
        Require(aliasRoot.GetProperty("custom_flag").GetBoolean(), "unknown keys must survive a world mode save");

        var questAlias = Path.Combine(root, "world-mode-quest-alias.json");
        File.WriteAllText(questAlias, """
        {
          "quest_mode": "Free Roam"
        }
        """);
        var questAliasConfig = ServerConfig.Load(questAlias);
        Require(questAliasConfig.WorldMode == "free-roam", "the quest_mode alias must be accepted and normalized");
        Require(questAliasConfig.Validate().Count == 0, "the quest_mode alias must validate");
        questAliasConfig.Save(questAlias);
        var questAliasRoot = JsonDocument.Parse(File.ReadAllText(questAlias)).RootElement;
        Require(questAliasRoot.GetProperty("worldMode").GetString() == "free-roam", "the quest_mode alias must re-save as worldMode");
        Require(!questAliasRoot.TryGetProperty("quest_mode", out _), "the quest_mode alias must not be written back");

        Require(GameModes.IsKnown(GameModes.CoopStory) && GameModes.IsKnown(GameModes.FreeRoam),
            "coop-story and free-roam must be the known modes of the native parser");
        Require(!GameModes.IsKnown("sandbox"), "an unknown mode must not be known");
        Require(!GameModes.IsKnown("faction-war") && !GameModes.IsKnown("instanced-story"),
            "the native parser rejects faction-war and instanced-story, so the Host must too");
        Require(!GameModes.Describe("coop-story").Contains("quest", StringComparison.OrdinalIgnoreCase),
            "quests are not in 2.0, so the Co-op story description must not mention them");
        Require(GameModes.LabelForMode("") == "Co-op story", "an empty mode must label as Co-op story");
        Require(GameModes.ModeForLabel("Free roam") == "free-roam", "the Free roam label must map to free-roam");
        Require(GameModes.ModeForLabel("Nonsense") == "coop-story", "an unknown label must map to coop-story");
        Require(GameModes.Normalize("Free Roam") == "free-roam", "the UI spelling 'Free Roam' must normalize to free-roam");
        Require(GameModes.Normalize("free roam") == "free-roam", "the UI spelling 'free roam' must normalize to free-roam");
        Require(GameModes.Normalize("Co-op") == "coop-story", "the UI spelling 'Co-op' must normalize to coop-story");
        Require(GameModes.Normalize("Co-op story") == "coop-story", "the UI spelling 'Co-op story' must normalize to coop-story");
        Require(GameModes.Normalize("Faction War") == "faction war", "an unsupported UI spelling must stay unrecognised");
        Require(new ServerConfig { WorldMode = "Free Roam" }.Validate().Count == 0,
            "a hand-edited 'Free Roam' must be accepted, as the UI accepts it");
        Require(GameModes.SelectableLabels.Count() == 2, "2.0.0 must offer exactly two selectable modes");
        Require(GameModes.SelectableLabels.Contains("Co-op story") && GameModes.SelectableLabels.Contains("Free roam"),
            "the selector must offer Co-op story and Free roam");
        Require(!GameModes.SelectableLabels.Contains("Faction war") && !GameModes.SelectableLabels.Contains("Instanced story"),
            "unfinished modes must not be offered yet");

        var invalid = new ServerConfig { WorldMode = "sandbox" };
        Require(invalid.Validate().Any(error => error.Contains("worldMode must be one of coop-story, free-roam", StringComparison.Ordinal)),
            "an unknown world mode must be rejected with the list of real modes");
        Require(new ServerConfig { WorldMode = "faction-war" }.Validate().Count > 0, "faction-war must be rejected");
        Require(new ServerConfig { WorldMode = "instanced-story" }.Validate().Count > 0, "instanced-story must be rejected");

        var spelled = Path.Combine(root, "world-mode-spelled.json");
        new ServerConfig { WorldMode = "Free Roam" }.Save(spelled);
        Require(JsonDocument.Parse(File.ReadAllText(spelled)).RootElement.GetProperty("worldMode").GetString() == "free-roam",
            "Save must write the normalized worldMode for a code-set 'Free Roam'");
        Require(new ServerConfig { WorldMode = "free_roam" }.Validate().Count > 0, "a snake_case world mode must be rejected");

        var configPath = Path.Combine(AppContext.BaseDirectory, "commonwealth-server.json");
        File.WriteAllText(configPath, "{}\n");
        try
        {
            using var viewModel = new MainWindowViewModel();
            Require(viewModel.GameModeLabel == "Co-op story", "the Host must open on Co-op story");
            Require(viewModel.WorldMode == "coop-story", "the Host must open on coop-story");
            Require(viewModel.GameModeDescription.Contains("story", StringComparison.OrdinalIgnoreCase),
                "the Host must describe the selected mode");

            viewModel.GameModeLabel = "Free roam";
            Require(viewModel.WorldMode == "free-roam", "selecting Free roam must set worldMode");
            Require(viewModel.GameModeDescription.Contains("no quests", StringComparison.Ordinal),
                "Free roam must show its one-line description");
            viewModel.SaveCommand.Execute(null);
            var savedConfig = ServerConfig.Load(configPath);
            Require(savedConfig.WorldMode == "free-roam", "the GUI world mode must be saved");

            using var reopened = new MainWindowViewModel();
            Require(reopened.GameModeLabel == "Free roam", "the Host must reopen on the last chosen mode");
        }
        finally
        {
            File.Delete(configPath);
        }

        foreach (var unsupported in new[] { "sandbox", "faction-war", "instanced-story" })
        {
            var untouched = "{ \"worldMode\": \"" + unsupported + "\" }\n";
            File.WriteAllText(configPath, untouched);
            try
            {
                using var blocked = new MainWindowViewModel();
                Require(blocked.StatusText == "Config error", $"a config with world mode '{unsupported}' must report a config error");
                Require(blocked.Log.Any(line => line.Contains("worldMode must be one of coop-story, free-roam", StringComparison.Ordinal)),
                    $"the host must show why world mode '{unsupported}' is rejected");
                Require(!blocked.SaveCommand.CanExecute(null), "Save must stay disabled until the unknown world mode is repaired");
                Require(File.ReadAllText(configPath) == untouched,
                    $"world mode '{unsupported}' must not be silently rewritten to coop-story by opening the Host");
            }
            finally
            {
                File.Delete(configPath);
            }
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
