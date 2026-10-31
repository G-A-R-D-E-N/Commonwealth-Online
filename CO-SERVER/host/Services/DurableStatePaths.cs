using System;
using System.IO;

namespace CommonwealthOnline.Host.Services;

internal static class DurableStatePaths
{
    public static string Resolve(string path, string baseDirectory)
    {
        var value = ServerConfig.TrimNative(path);
        return Path.IsPathRooted(value)
            ? Path.GetFullPath(value)
            : Path.GetFullPath(value, baseDirectory);
    }

    public static string Validate(string path) => Validate(path, AppContext.BaseDirectory);

    public static string Validate(string path, string baseDirectory)
    {
        var value = ServerConfig.TrimNative(path);
        if (value.Length == 0) return "Choose a state file path.";

        try
        {
            var fullPath = Resolve(value, baseDirectory);
            if (Directory.Exists(fullPath)) return "The state path must be a file.";
            var parent = Path.GetDirectoryName(fullPath);
            if (string.IsNullOrEmpty(parent) || !Directory.Exists(parent)) return "The parent folder does not exist.";
            return $"Ready: {fullPath}";
        }
        catch (Exception ex) when (ex is ArgumentException or NotSupportedException or PathTooLongException)
        {
            return $"Invalid path: {ex.Message}";
        }
    }
}
