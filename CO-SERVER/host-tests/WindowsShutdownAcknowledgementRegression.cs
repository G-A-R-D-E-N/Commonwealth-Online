using System.Diagnostics;
using System.Text;
using System.Text.Json;
using CommonwealthOnline.Host.Services;

static class WindowsShutdownAcknowledgementRegression
{
    public static void Run()
    {
        if (!OperatingSystem.IsWindows()) return;

        var root = Path.Combine(Path.GetTempPath(), "co-host-shutdown-race-" + Guid.NewGuid().ToString("N"));
        var project = Path.Combine(root, "child.csproj");
        var source = Path.Combine(root, "Program.cs");
        var output = Path.Combine(root, "publish");
        var config = Path.Combine(output, "commonwealth-server.json");
        var tokenPath = Path.Combine(output, ".admin-token");
        var state = Path.Combine(output, "shutdown-state.txt");
        Directory.CreateDirectory(output);

        try
        {
            File.WriteAllText(project, """
            <Project Sdk="Microsoft.NET.Sdk">
              <PropertyGroup>
                <OutputType>Exe</OutputType>
                <TargetFramework>net8.0</TargetFramework>
                <AssemblyName>CommonwealthOnline.Server</AssemblyName>
                <ImplicitUsings>enable</ImplicitUsings>
                <Nullable>enable</Nullable>
              </PropertyGroup>
            </Project>
            """);
            File.WriteAllText(source, """
            using System.Net;
            using System.Net.Sockets;
            using System.Text;
            using System.Text.Json;

            var configPath = args[Array.IndexOf(args, "--config") + 1];
            var root = JsonDocument.Parse(File.ReadAllText(configPath)).RootElement;
            var port = root.GetProperty("admin_port").GetInt32();
            var directory = Path.GetDirectoryName(Path.GetFullPath(configPath))!;
            var token = File.ReadAllText(Path.Combine(directory, ".admin-token")).TrimEnd('\r', '\n');
            var state = Path.Combine(directory, "shutdown-state.txt");
            using var listener = new TcpListener(IPAddress.Loopback, port);
            listener.Start();
            File.WriteAllText(state, "ready\n");
            using var client = await listener.AcceptTcpClientAsync();
            using var stream = client.GetStream();
            using var reader = new StreamReader(stream, Encoding.UTF8, leaveOpen: true);
            var line = await reader.ReadLineAsync();
            using var request = JsonDocument.Parse(line ?? string.Empty);
            var requestRoot = request.RootElement;
            if (requestRoot.GetProperty("adminToken").GetString() != token ||
                requestRoot.GetProperty("cmd").GetString() != "shutdown") return;
            File.WriteAllText(state, "authenticated\n");
            client.Close();
            listener.Stop();
            await Task.Delay(3000);
            File.WriteAllText(state, "natural\n");
            """);
            RunDotnet(project, output, root);
            var port = HostTestNetworking.FreeTcpPort();
            var adminPort = HostTestNetworking.FreeTcpPort();
            while (adminPort == port) adminPort = HostTestNetworking.FreeTcpPort();
            File.WriteAllText(config, $$"""
            {
              "host": "127.0.0.1",
              "port": {{port}},
              "admin_port": {{adminPort}},
              "lan_discovery_enabled": false
            }
            """);
            File.WriteAllText(tokenPath, "test-admin-token\n");
            File.WriteAllText(Path.Combine(output, "CommonwealthOnline.Server.native"), "native-cpp\n");
            File.WriteAllText(Path.Combine(output, "commonwealth_online_gns_bridge.dll"), "bridge\n");
            File.WriteAllText(Path.Combine(output, "NATIVE_RUNTIME.txt"), "native runtime\n");

            using var controller = new ServerController();
            controller.Start(output, config);
            var ready = SpinWait.SpinUntil(() => File.Exists(state) && File.ReadAllText(state) == "ready\n", TimeSpan.FromSeconds(10));
            Require(ready, "shutdown acknowledgement regression child did not become ready");
            controller.Stop();
            Require(File.Exists(state) && File.ReadAllText(state) == "natural\n",
                "Windows Stop force-killed a native server after its authenticated shutdown acknowledgement was lost");
        }
        finally
        {
            try { Directory.Delete(root, recursive: true); } catch { }
        }
    }

    private static void RunDotnet(string project, string output, string workingDirectory)
    {
        var info = new ProcessStartInfo
        {
            FileName = "dotnet",
            WorkingDirectory = workingDirectory,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        info.ArgumentList.Add("publish");
        info.ArgumentList.Add(project);
        info.ArgumentList.Add("-c");
        info.ArgumentList.Add("Release");
        info.ArgumentList.Add("-o");
        info.ArgumentList.Add(output);
        info.ArgumentList.Add("--nologo");
        using var process = Process.Start(info) ?? throw new InvalidOperationException("dotnet publish did not start");
        var stdout = process.StandardOutput.ReadToEndAsync();
        var stderr = process.StandardError.ReadToEndAsync();
        process.WaitForExit();
        if (process.ExitCode != 0)
            throw new InvalidOperationException($"shutdown acknowledgement child publish failed: {stdout.Result}\n{stderr.Result}");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
