using System;
using System.Buffers;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading;
using System.Threading.Tasks;

namespace CommonwealthOnline.Host.Services;

public sealed class AdminClient
{
    private const int MaxResponseBytes = 1_000_000;

    public static readonly TimeSpan DefaultTimeout = TimeSpan.FromSeconds(10);

    private readonly int _port;
    private readonly string _tokenPath;
    private readonly TimeSpan _timeout;

    public AdminClient(int adminPort, string tokenPath, TimeSpan? timeout = null)
    {
        _port = adminPort;
        _tokenPath = tokenPath;
        _timeout = timeout ?? DefaultTimeout;
    }

    public async Task<AdminResponse> SendAsync(JsonObject request, CancellationToken ct = default)
    {
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(ct);
        deadline.CancelAfter(_timeout);
        var token = deadline.Token;

        var adminToken = (await File.ReadAllTextAsync(_tokenPath, token).ConfigureAwait(false)).Trim();
        var authenticated = (JsonObject)request.DeepClone();
        authenticated["adminToken"] = adminToken;
        var payload = JsonSerializer.SerializeToUtf8Bytes(authenticated);

        using var client = new TcpClient();
        await client.ConnectAsync(IPAddress.Loopback, _port, token).ConfigureAwait(false);
        var stream = client.GetStream();
        await stream.WriteAsync(payload, token).ConfigureAwait(false);
        await stream.WriteAsync(new byte[] { (byte)'\n' }, token).ConfigureAwait(false);

        var line = await ReadLineAsync(stream, token).ConfigureAwait(false);
        return AdminResponse.Parse(JsonNode.Parse(line) as JsonObject);
    }

    private static async Task<byte[]> ReadLineAsync(Stream stream, CancellationToken ct)
    {
        var rented = ArrayPool<byte>.Shared.Rent(4096);
        try
        {
            using var buffer = new MemoryStream();
            while (buffer.Length < MaxResponseBytes)
            {
                var read = await stream.ReadAsync(rented, ct).ConfigureAwait(false);
                if (read == 0)
                {
                    break;
                }

                var newline = Array.IndexOf(rented, (byte)'\n', 0, read);
                buffer.Write(rented, 0, newline >= 0 ? newline : read);
                if (newline >= 0)
                {
                    break;
                }
            }

            return buffer.ToArray();
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(rented);
        }
    }

    public Task<AdminResponse> StatusAsync(CancellationToken ct = default) =>
        SendAsync(new JsonObject { ["cmd"] = "status" }, ct);

    public Task<AdminResponse> ClientsAsync(CancellationToken ct = default) =>
        SendAsync(new JsonObject { ["cmd"] = "clients" }, ct);

    public Task<AdminResponse> KickAsync(uint playerId, string reason, CancellationToken ct = default) =>
        SendAsync(new JsonObject { ["cmd"] = "kick", ["playerId"] = playerId, ["reason"] = reason }, ct);

    public Task<AdminResponse> BanAsync(uint playerId, string reason, CancellationToken ct = default) =>
        SendAsync(new JsonObject { ["cmd"] = "ban", ["playerId"] = playerId, ["reason"] = reason }, ct);

    public Task<AdminResponse> ShutdownAsync(CancellationToken ct = default) =>
        SendAsync(new JsonObject { ["cmd"] = "shutdown" }, ct);
}

public sealed class AdminResponse
{
    public bool Ok { get; init; }
    public string? Message { get; init; }
    public string? Error { get; init; }
    public JsonObject? Data { get; init; }

    public static AdminResponse Parse(JsonObject? root)
    {
        if (root is null)
        {
            return new AdminResponse { Ok = false, Error = "The admin server sent an unreadable reply." };
        }

        return new AdminResponse
        {
            Ok = root["ok"]?.GetValue<bool>() ?? false,
            Message = root["message"]?.GetValue<string>(),
            Error = root["error"]?.GetValue<string>(),
            Data = root["data"] as JsonObject,
        };
    }

    public string Describe() =>
        Ok
            ? (string.IsNullOrEmpty(Message) ? "ok" : Message!)
            : (string.IsNullOrEmpty(Error) ? "the server reported a failure" : Error!);
}
