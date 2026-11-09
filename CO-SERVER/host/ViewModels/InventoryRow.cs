namespace CommonwealthOnline.Host.ViewModels;

public sealed class InventoryRow
{
    public string ItemId { get; init; } = string.Empty;
    public string Name { get; init; } = string.Empty;
    public int Count { get; init; }
}
