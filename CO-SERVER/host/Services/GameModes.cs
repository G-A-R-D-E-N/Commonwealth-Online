using System;
using System.Collections.Generic;

namespace CommonwealthOnline.Host.Services;

public static class GameModes
{
    public const string CoopStory = "coop-story";
    public const string FreeRoam = "free-roam";

    public static readonly string[] SelectableLabels = { "Co-op story", "Free roam" };

    public static readonly string[] SelectableModes = { CoopStory, FreeRoam };

    private static readonly HashSet<string> KnownModes = new(StringComparer.Ordinal)
    {
        CoopStory, FreeRoam,
    };

    private static readonly Dictionary<string, string> LabelByMode = new(StringComparer.Ordinal)
    {
        [CoopStory] = "Co-op story",
        [FreeRoam] = "Free roam",
    };

    private static readonly Dictionary<string, string> DescriptionByMode = new(StringComparer.Ordinal)
    {
        [CoopStory] = "Play the story together: story characters and companions are shared.",
        [FreeRoam] = "Free roam: no quests, no story characters or companions. Vendors, town NPCs, enemies and animals stay.",
    };

    public static string ModeForLabel(string? label)
    {
        var index = Array.IndexOf(SelectableLabels, ServerConfig.TrimNative(label));
        return index < 0 ? CoopStory : SelectableModes[index];
    }

    public static string LabelForMode(string? mode) =>
        LabelByMode.TryGetValue(Normalize(mode), out var label) ? label : LabelByMode[CoopStory];

    public static string Describe(string? mode) =>
        DescriptionByMode.TryGetValue(Normalize(mode), out var description) ? description : DescriptionByMode[CoopStory];

    public static bool IsKnown(string? mode) => KnownModes.Contains(Normalize(mode));

    private static readonly Dictionary<string, string> SpellingByAlias = new(StringComparer.Ordinal)
    {
        ["co-op story"] = CoopStory,
        ["co-op"] = CoopStory,
        ["free roam"] = FreeRoam,
    };

    public static string Normalize(string? mode)
    {
        var trimmed = ServerConfig.TrimNative(mode);
        if (trimmed.Length == 0) return CoopStory;
        var lowered = trimmed.ToLowerInvariant();
        return SpellingByAlias.TryGetValue(lowered, out var canonical) ? canonical : lowered;
    }
}
