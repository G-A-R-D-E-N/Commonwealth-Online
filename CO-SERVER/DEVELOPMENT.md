# Development

## Layout

| Path | What it is |
| --- | --- |
| `server-cpp/` | the authoritative native server, its unit tests and live GNS smokes |
| `../co-core/` | engine-independent behaviour shared with the F4SE client |
| `server/native_transport/` | the C bridge over GameNetworkingSockets |
| `server/scripts/` | bridge builds and the repository runtime policy |
| `host/` | the Avalonia host GUI |
| `host-tests/` | Host launch contract and native config tests |

## Tests

```bash
cd server-cpp
xmake f -y -m debug
xmake test -y
```

Debug matters: the suites are `assert`-based, so a release build silently skips
their checks.

The live GNS smokes are not part of `xmake test`. They need a running server
and are driven by `.github/workflows/co-server-acceptance.yml`, which starts
one and runs each smoke against it. `scripts/verify-native-acceptance-matrix.sh`
fails the build if a smoke exists but nothing runs it.

## Guards

| Script | Enforces |
| --- | --- |
| `scripts/verify-production-transport.py` | client and server stay GNS-only |
| `server-cpp/scripts/verify-native-acceptance-matrix.sh` | every acceptance target is built and run |
| `server-cpp/scripts/verify-native-release-path.sh` | the release path builds from native sources |
| `server/scripts/verify-no-legacy-runtime.sh` | the retired interpreted runtime and its dependency surface stay out |

They run in the `Production Transport Contract` workflow.

## Protocol references

- [`../docs/server/PROTOCOL_V2.md`](../docs/server/PROTOCOL_V2.md)
- [`../docs/server/IDENTITY.md`](../docs/server/IDENTITY.md)
- [`../docs/server/PERSISTENCE.md`](../docs/server/PERSISTENCE.md)

These describe the wire contract the native server implements. They predate the
native rewrite but the formats they document are unchanged.
