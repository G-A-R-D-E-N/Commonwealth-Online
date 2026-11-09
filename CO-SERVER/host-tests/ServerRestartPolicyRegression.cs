using CommonwealthOnline.Host.Services;

static class ServerRestartPolicyRegression
{
    public static void Run()
    {
        Require(ServerRestartPolicy.ShouldRestart(ServerRestartPolicy.GnsPollRestartExitCode, false, 0),
            "a GNS poll restart exit must restart the native server");
        Require(!ServerRestartPolicy.ShouldRestart(0, false, 0), "a clean native exit must not restart the server");
        Require(!ServerRestartPolicy.ShouldRestart(1, false, 0), "a crash exit must not restart the server");
        Require(!ServerRestartPolicy.ShouldRestart(ServerRestartPolicy.GnsPollRestartExitCode, true, 0),
            "an operator stop must not restart the native server");
        Require(!ServerRestartPolicy.ShouldRestart(ServerRestartPolicy.GnsPollRestartExitCode, false,
            ServerRestartPolicy.MaxConsecutiveRestarts),
            "native restarts must be bounded");
        Require(ServerRestartPolicy.CanRestart(false, ServerRestartPolicy.MaxConsecutiveRestarts - 1),
            "a failed native restart must be retried below the limit");
        Require(!ServerRestartPolicy.CanRestart(false, ServerRestartPolicy.MaxConsecutiveRestarts),
            "a failed native restart must stop at the limit");
        Require(!ServerRestartPolicy.CanRestart(true, 0), "an operator stop must end a pending native restart");
        Require(ServerRestartPolicy.NextConsecutiveRestarts(3, TimeSpan.FromSeconds(10)) == 3,
            "a quick exit must keep the consecutive restart count");
        Require(ServerRestartPolicy.NextConsecutiveRestarts(3, TimeSpan.FromMinutes(5)) == 0,
            "a stable run must reset the consecutive restart count");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
