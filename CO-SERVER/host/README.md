# Commonwealth Online - Server Host (Avalonia)

Cross-platform C# GUI (.NET 8 + Avalonia) for managing the packaged native
Commonwealth Online C++ server.

## Run

```text
dotnet run --project CO-SERVER/host/CommonwealthOnline.Host.csproj
```

The Host launches the native apphost beside its own executable. A valid package
contains:

- `CommonwealthOnline.Server` on Linux or `CommonwealthOnline.Server.exe` on Windows
- `CommonwealthOnline.Server.native` containing `native-cpp`
- the packaged GNS bridge (`libcommonwealth_online_gns_bridge.so` or `commonwealth_online_gns_bridge.dll`)
- `NATIVE_RUNTIME.txt`
- `commonwealth-server.json`

The Host sets the package directory as the server working directory and passes
`--config` with the configuration path. Managed server DLLs and source
checkouts are not launch targets.

## Features

- Edit and save the native `commonwealth-server.json` schema.
- Start and stop the native server while streaming its output.
- View connected players and use the token-authenticated admin port for Kick and Ban.

## Publish

Self-contained, single-file builds need no .NET runtime on the target. Publish
the Host and native server together using the repository package workflow.
