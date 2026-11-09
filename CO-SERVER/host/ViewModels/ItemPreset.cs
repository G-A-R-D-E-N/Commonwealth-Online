using System.Collections.Generic;

namespace CommonwealthOnline.Host.ViewModels;

public enum ItemPresetKind
{
    Spawn,
    Item,
}

public sealed record ItemPreset(string Name, string ItemId, ItemPresetKind Kind, string Category)
{
    public static IReadOnlyList<ItemPreset> All { get; } = new ItemPreset[]
    {
        new("Power Armor Frame", "0002079E", ItemPresetKind.Spawn, "Power Armor"),
        new("Bottle Caps", "0000000F", ItemPresetKind.Item, "Currency"),
        new("Stimpak", "00023736", ItemPresetKind.Item, "Aid"),
        new("10mm Pistol", "00004822", ItemPresetKind.Item, "Weapons"),
        new("10mm Round", "0001F276", ItemPresetKind.Item, "Ammo"),
        new("Fusion Cell", "000C1897", ItemPresetKind.Item, "Ammo"),
    };
}
