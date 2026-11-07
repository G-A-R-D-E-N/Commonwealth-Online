using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json.Nodes;
using CommonwealthOnline.Host.Services;
using CommonwealthOnline.Host.ViewModels;

static class AdminCommandRegression
{
    private static readonly string Token = Guid.NewGuid().ToString("N");

    public static void Run(string root)
    {
        var tokenPath = Path.Combine(root, "admin-command-token");
        File.WriteAllText(tokenPath, Token + "\n");

        Require(AdminClient.IsValidItemId("0002079E"), "a complete eight-digit ItemId must be accepted");
        Require(AdminClient.IsValidItemId("0002079e"), "lower-case hex in an ItemId must be accepted");
        Require(!AdminClient.IsValidItemId("0002079"), "a seven-digit ItemId must be rejected");
        Require(!AdminClient.IsValidItemId("0002079EE"), "a nine-digit ItemId must be rejected");
        Require(!AdminClient.IsValidItemId("0002079G"), "a non-hex ItemId must be rejected");
        Require(!AdminClient.IsValidItemId("00000000"), "an all-zero ItemId must be rejected");
        Require(!AdminClient.IsValidItemId(null), "a missing ItemId must be rejected");
        foreach (var preset in ItemPreset.All)
        {
            Require(AdminClient.IsValidItemId(preset.ItemId), $"preset {preset.Name} must carry a valid ItemId");
        }

        ExpectRequest(tokenPath, admin => admin.SpawnObjectAsync(3, "0002079E", 1),
            "{\"cmd\":\"spawn_object\",\"playerId\":3,\"itemId\":\"0002079E\",\"count\":1}");
        ExpectRequest(tokenPath, admin => admin.GiveItemAsync(3, "0000000F", 25),
            "{\"cmd\":\"give_item\",\"playerId\":3,\"itemId\":\"0000000F\",\"count\":25}");
        ExpectRequest(tokenPath, admin => admin.TakeItemAsync(3, "0000000F", 25),
            "{\"cmd\":\"take_item\",\"playerId\":3,\"itemId\":\"0000000F\",\"count\":25}");
        ExpectRequest(tokenPath, admin => admin.RequestInventoryAsync(3),
            "{\"cmd\":\"request_inventory\",\"playerId\":3}");
        ExpectRequest(tokenPath, admin => admin.GetInventoryAsync(3),
            "{\"cmd\":\"get_inventory\",\"playerId\":3}");
        ExpectRequest(tokenPath, admin => admin.GetOpResultAsync(3),
            "{\"cmd\":\"get_op_result\",\"playerId\":3}");

        using var viewModel = new MainWindowViewModel();
        viewModel.Players.Add(new PlayerRow { PlayerId = 3 });
        viewModel.Players.Add(new PlayerRow { PlayerId = 4 });
        viewModel.Inventory.Add(new InventoryRow { ItemId = "0000000F", Name = "Bottle Caps", Count = 5 });
        viewModel.SpawnerPlayer = viewModel.Players[0];
        Require(viewModel.Inventory.Count == 0, "changing the spawner player must clear the inventory panel");

        Require(viewModel.Categories[0] == "All" && viewModel.Categories.Distinct().Count() == viewModel.Categories.Count,
            "the category list must start with All and hold each category once");
        Require(viewModel.FilteredPresets.SequenceEqual(ItemPreset.All), "All must list every preset");
        foreach (var category in viewModel.Categories.Skip(1))
        {
            viewModel.SelectedCategory = category;
            var expected = ItemPreset.All.Where(preset => preset.Category == category).ToList();
            Require(viewModel.FilteredPresets.SequenceEqual(expected), $"category {category} must list only its own presets");
        }
        viewModel.SelectedCategory = "All";
        Require(viewModel.FilteredPresets.SequenceEqual(ItemPreset.All), "All must list every preset again");
        viewModel.SpawnerSearch = "10mm";
        viewModel.SelectedCategory = "Ammo";
        Require(viewModel.FilteredPresets.Count == 1 && viewModel.FilteredPresets[0].Name == "10mm Round",
            "the search text and the category must both apply");
        RequireSpawnerSurvivesPlayerRefresh();
    }

    private static void RequireSpawnerSurvivesPlayerRefresh()
    {
        using var viewModel = new MainWindowViewModel();
        viewModel.ApplyClients(JsonNode.Parse("{\"clients\":[{\"player_id\":4,\"name\":\"Nomad\",\"address\":\"203.0.113.4\",\"port\":7777,\"connected_at\":1700000000.5}]}")!.AsObject());
        viewModel.SpawnerPlayer = viewModel.Players[0];
        viewModel.FillInventory(4, JsonNode.Parse("{\"items\":[{\"itemId\":\"0000000F\",\"name\":\"Bottle Caps\",\"count\":5}]}")!.AsObject());
        Require(viewModel.Inventory.Count == 1, "the filled inventory must hold its rows");

        viewModel.ApplyClients(JsonNode.Parse("{\"clients\":[{\"player_id\":4,\"address\":\"203.0.113.4\",\"port\":7777,\"connected_at\":1700000000.5}]}")!.AsObject());
        Require(viewModel.Inventory.Count == 1, "a same-id refresh must keep the inventory rows");
        Require(viewModel.SpawnerPlayer?.PlayerId == 4u, "a same-id refresh must keep the spawner player");
        Require(ReferenceEquals(viewModel.SpawnerPlayer, viewModel.Players[0]), "the spawner must be the rebuilt row for player 4");
        Require(viewModel.SpawnerPlayer!.DisplayName == "Player #4", "an unnamed player must show as Player #4");

        viewModel.ApplyClients(JsonNode.Parse("{\"clients\":[{\"player_id\":3,\"name\":\"Other\",\"address\":\"203.0.113.3\",\"port\":7777,\"connected_at\":1700000000.5}]}")!.AsObject());
        Require(viewModel.SpawnerPlayer is null, "a refresh without player 4 must clear the spawner");
        Require(viewModel.Inventory.Count == 0, "a refresh without player 4 must clear the inventory");
    }

    private static void ExpectRequest(string tokenPath, Func<AdminClient, Task<AdminResponse>> send, string expected)
    {
        var port = HostTestNetworking.FreeTcpPort();
        var listener = new TcpListener(IPAddress.Loopback, port);
        listener.Start();
        try
        {
            var pending = send(new AdminClient(port, tokenPath, TimeSpan.FromSeconds(10)));
            var accepted = listener.AcceptTcpClientAsync();
            Require(accepted.Wait(TimeSpan.FromSeconds(10)), $"admin request {expected} never reached the listener");
            using var client = accepted.Result;
            client.ReceiveTimeout = 10_000;
            var stream = client.GetStream();
            using var reader = new StreamReader(stream, Encoding.UTF8, leaveOpen: true);
            var request = JsonNode.Parse(reader.ReadLine() ?? string.Empty) as JsonObject;
            Require(request?["adminToken"]?.GetValue<string>() == Token, "admin requests must carry the admin token");
            request!.Remove("adminToken");
            Require(request.ToJsonString() == expected,
                $"admin request must serialize as {expected} but was {request.ToJsonString()}");
            stream.Write(Encoding.UTF8.GetBytes("{\"ok\":true,\"message\":\"ok\"}\n"));
            Require(pending.GetAwaiter().GetResult().Ok, "the admin reply must read as ok");
        }
        finally
        {
            listener.Stop();
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
