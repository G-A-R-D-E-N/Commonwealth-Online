using System;
using System.Diagnostics;
using System.IO;

namespace CommonwealthOnline.Host.Services;

public sealed class ServerController : IDisposable
{
    private Process? _process;
    private int? _adminPort;
    private string? _adminTokenPath;

    public bool IsRunning => _process is { HasExited: false };

    public event Action<string>? LogReceived;
    public event Action<bool>? RunningChanged;

    public void Start(string serverDir, string configPath)
    {
        if (IsRunning)
        {
            return;
        }

        _process?.Dispose();
        _process = null;

        var startInfo = ResolveLaunch(serverDir, configPath);
        var config = ServerConfig.Load(configPath);
        _adminPort = config.AdminPort;
        _adminTokenPath = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(configPath))!, ".admin-token");
        var process = new Process { StartInfo = startInfo, EnableRaisingEvents = true };
        process.OutputDataReceived += (_, e) => Emit(e.Data);
        process.ErrorDataReceived += (_, e) => Emit(e.Data);
        process.Exited += (_, _) => RunningChanged?.Invoke(false);

        var started = false;
        try
        {
            if (!process.Start())
            {
                throw new InvalidOperationException($"Could not start native server at {startInfo.FileName}.");
            }

            started = true;
            _process = process;
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
            RunningChanged?.Invoke(true);
            if (process.HasExited)
            {
                RunningChanged?.Invoke(false);
            }
        }
        catch
        {
            if (started)
            {
                try
                {
                    if (!process.HasExited) process.Kill(entireProcessTree: true);
                }
                catch (Exception)
                {
                }
            }

            if (ReferenceEquals(_process, process)) _process = null;
            process.Dispose();
            throw;
        }
    }

    public void Stop()
    {
        if (_process is not { } process)
        {
            return;
        }

        try
        {
            if (!process.HasExited)
            {
                _ = TryRequestGracefulStop(process);
                if (!process.WaitForExit(5000))
                {
                    if (!process.HasExited) process.Kill(entireProcessTree: true);
                    process.WaitForExit(5000);
                }
            }
        }
        catch (Exception)
        {
        }
    }

    public void Dispose()
    {
        Stop();
        _process?.Dispose();
        _process = null;
    }

    private void Emit(string? line)
    {
        if (!string.IsNullOrEmpty(line))
        {
            LogReceived?.Invoke(line);
        }
    }

    internal static ProcessStartInfo ResolveLaunch(string serverDir, string configPath)
    {
        var exeName = OperatingSystem.IsWindows()
            ? "CommonwealthOnline.Server.exe"
            : "CommonwealthOnline.Server";
        var apphost = Path.Combine(serverDir, exeName);
        RequireArtifact(apphost, "native server executable");
        RequireArtifact(Path.Combine(serverDir, "commonwealth-server.json"), "native package configuration");
        RequireArtifact(Path.Combine(serverDir, "CommonwealthOnline.Server.native"), "native package marker");
        RequireArtifact(Path.Combine(serverDir, NativeBridgeName()), "native GNS bridge");
        RequireArtifact(Path.Combine(serverDir, "NATIVE_RUNTIME.txt"), "native runtime layout");

        var marker = Path.Combine(serverDir, "CommonwealthOnline.Server.native");
        if (File.ReadAllText(marker).TrimEnd('\r', '\n') != "native-cpp")
        {
            throw new InvalidOperationException($"Invalid native package marker at {marker}.");
        }

        var info = new ProcessStartInfo
        {
            FileName = apphost,
            WorkingDirectory = serverDir,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };

        info.ArgumentList.Add("--config");
        info.ArgumentList.Add(configPath);
        return info;
    }

    private static string NativeBridgeName() => OperatingSystem.IsWindows()
        ? "commonwealth_online_gns_bridge.dll"
        : "libcommonwealth_online_gns_bridge.so";

    private static void RequireArtifact(string path, string description)
    {
        if (!File.Exists(path) || new FileInfo(path).Length == 0)
        {
            throw new InvalidOperationException($"Native package is missing {description} at {path}.");
        }
    }

    private bool TryRequestGracefulStop(Process process)
    {
        if (TryRequestAdminShutdown()) return true;
        if (OperatingSystem.IsWindows()) return false;

        var signal = new ProcessStartInfo
        {
            FileName = "kill",
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        signal.ArgumentList.Add("-TERM");
        signal.ArgumentList.Add(process.Id.ToString());

        try
        {
            using var request = Process.Start(signal);
            return request is not null && request.WaitForExit(1000) && request.ExitCode == 0;
        }
        catch (Exception)
        {
            return false;
        }
    }

    private bool TryRequestAdminShutdown()
    {
        if (_adminPort is not { } port || _adminTokenPath is not { } tokenPath) return false;
        try
        {
            var response = new AdminClient(port, tokenPath, TimeSpan.FromSeconds(2))
                .ShutdownAsync()
                .GetAwaiter()
                .GetResult();
            return response.Ok;
        }
        catch (Exception)
        {
            return false;
        }
    }
}
