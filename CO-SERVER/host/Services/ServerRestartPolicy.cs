using System;

namespace CommonwealthOnline.Host.Services;

public static class ServerRestartPolicy
{
    // Must match kGnsPollRestartExitCode in CO-SERVER/server-cpp/include/ServerGnsPollRetry.h.
    public const int GnsPollRestartExitCode = 75;
    public const int MaxConsecutiveRestarts = 5;
    public static readonly TimeSpan RestartDelay = TimeSpan.FromSeconds(5);
    public static readonly TimeSpan StableRun = TimeSpan.FromMinutes(1);

    public static bool CanRestart(bool stopRequested, int consecutiveRestarts) =>
        !stopRequested && consecutiveRestarts < MaxConsecutiveRestarts;

    public static bool ShouldRestart(int exitCode, bool stopRequested, int consecutiveRestarts) =>
        exitCode == GnsPollRestartExitCode && CanRestart(stopRequested, consecutiveRestarts);

    public static int NextConsecutiveRestarts(int consecutiveRestarts, TimeSpan runtime) =>
        runtime >= StableRun ? 0 : consecutiveRestarts;
}
