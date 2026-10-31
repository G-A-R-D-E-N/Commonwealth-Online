"""Contract for the public-listing default flip and the opt-outs that keep tests private.

The default is now on (co-core, the shipped native config and the Host), so anything
that starts a real server must say otherwise unless it deliberately lists. This file
pins both sides: the default, the --no-public-listing flag, and the launch sites whose
flags or scratch configs keep CI, package smokes and Steam P2P off the production
directory.

Workflows are read from .github/workflows, where the public CI export places them, and
fall back to .github/public-ci/workflows for a private checkout.
"""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def read_workflow(name: str) -> str:
    for relative in (f".github/workflows/{name}", f".github/public-ci/workflows/{name}"):
        if (ROOT / relative).is_file():
            return read(relative)
    raise AssertionError(f"workflow not found: {name}")


def body(text: str, signature: str) -> str:
    start = text.index(signature)
    open_brace = text.index("{", start)
    depth = 0
    for index in range(open_brace, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return text[open_brace : index + 1]
    raise AssertionError(f"unterminated body for {signature}")


def test_core_options_default_is_on() -> None:
    header = read("co-core/include/F4TServerConfiguration.h")
    assert "bool publicListing{ true };" in header, "Options::publicListing must default to true"


def test_core_parse_defaults_on_and_keeps_explicit_false() -> None:
    cpp = read("co-core/src/config/F4TServerConfiguration.cpp")
    assert (
        'ReadBoolean(Find(a_root, "public_listing", "publicListing"), true, "public_listing")' in cpp
    ), "Parse must fall back to true for a missing public_listing"
    assert "options.publicListing = " in cpp, "Parse must write the publicListing option"
    tests = read("CO-SERVER/server-cpp/tests/ServerConfigurationTests.cpp")
    assert "listingDefaults.publicListing" in tests and "listingOptedOut.publicListing" in tests, (
        "ServerConfigurationTests must cover the default-on and explicit-off cases"
    )


def test_core_validate_checks_listed_metadata() -> None:
    cpp = read("co-core/src/config/F4TServerConfiguration.cpp")
    validate = body(cpp, "std::vector<std::string> Validate")
    for message in (
        "server_name cannot be blank while public_listing is on",
        "server_name must be 1-64 printable UTF-8 bytes while public_listing is on",
        "server_description must be at most 256 printable UTF-8 bytes while public_listing is on",
    ):
        assert message in validate, f"Validate must reject {message!r} while listing is on"


def test_bootstrap_off_flag_and_help() -> None:
    bootstrap = read("CO-SERVER/server-cpp/src/ServerBootstrap.cpp")
    off = bootstrap.index('if (argument == "--no-public-listing")')
    assert "a_out.options.publicListing = false;" in bootstrap[off : off + 160], (
        "--no-public-listing must turn the listing off"
    )
    on = bootstrap.index('if (argument == "--list-publicly")')
    assert "a_out.options.publicListing = true;" in bootstrap[on : on + 160], (
        "--list-publicly must stay a working opt-in"
    )
    help_text = bootstrap[bootstrap.index("Usage: CommonwealthOnlineServerCpp") : bootstrap.index("return 0;", bootstrap.index('if (argument == "--help")'))]
    assert "--no-public-listing" in help_text, "the --help text must list --no-public-listing"


def test_shipped_native_config_lists() -> None:
    config = json.loads(read("CO-SERVER/server-cpp/commonwealth-server-native.json"))
    assert config.get("public_listing") is True, "the shipped native config must list by default"


def test_host_default_is_on_from_one_place() -> None:
    host = read("CO-SERVER/host/Services/ServerConfig.cs")
    assert "public const bool DefaultPublicListing = true;" in host, (
        "the Host must hold the default in one true constant"
    )
    assert "public bool PublicListing { get; set; } = DefaultPublicListing;" in host, (
        "the property initializer must use the shared default"
    )
    assert 'ReadBool(root, "public_listing", "publicListing") ?? DefaultPublicListing' in host, (
        "Load must fall back to the shared default, not false"
    )


def test_steam_p2p_launch_never_lists() -> None:
    launcher = read("CO-BETA/plugin/src/F4TLocalServerLauncher.cpp")
    ensure = body(launcher, "std::string EnsureRunning(")
    assert 'commandLine += L" --no-public-listing";' in ensure, (
        "the Steam P2P launch command line must pass --no-public-listing"
    )
    assert ensure.index("--no-public-listing") < ensure.index("CreateProcessW("), (
        "the flag must be in the command line handed to CreateProcessW"
    )


def test_ci_direct_server_starts_never_list() -> None:
    workflow = read_workflow("co-server-acceptance.yml")
    lines = workflow.splitlines()
    starts = [index for index, line in enumerate(lines) if "CommonwealthOnlineServerCpp \\" in line]
    assert len(starts) == 9, f"expected the 9 acceptance server starts, found {len(starts)}"
    for index in starts:
        block = "\n".join(lines[index : index + 8])
        assert "--no-public-listing" in block, (
            f"co-server-acceptance.yml:{index + 1} starts a real server without --no-public-listing"
        )


def test_client_protocol_acceptance_server_never_lists() -> None:
    workflow = read_workflow("co-client-protocol-acceptance.yml")
    args = workflow[workflow.index("local args=(") :]
    args = args[: args.index(")")]
    assert "--no-public-listing" in args, "the client acceptance server args must include --no-public-listing"


def test_package_smokes_never_list() -> None:
    package = read("CO-SERVER/server-cpp/scripts/smoke-native-linux-package.sh")
    assert package.count('"public_listing": false') == 1, (
        "smoke-native-linux-package.sh must write public_listing false in its scratch config"
    )
    release = read("CO-SERVER/server-cpp/scripts/smoke-native-linux-release.sh")
    assert release.count('"public_listing": false') == 3, (
        "smoke-native-linux-release.sh must write public_listing false in every scratch config"
    )
    windows = read("CO-SERVER/server-cpp/scripts/smoke-native-windows-package.ps1")
    assert "public_listing" in windows and "$false" in windows, (
        "smoke-native-windows-package.ps1 must write public_listing false in its scratch config"
    )


def test_benchmark_server_never_lists() -> None:
    benchmark = read("scripts/run-server-persistence-benchmark.py")
    assert '"public_listing": False' in benchmark, (
        "the persistence/player-load benchmark config must set public_listing false"
    )


def test_host_package_smoke_never_lists() -> None:
    host_tests = read("CO-SERVER/host-tests/Program.cs")
    assert "smokeConfig.PublicListing = false;" in host_tests, (
        "the packaged Host controller smoke must set PublicListing = false"
    )


def test_deliberate_live_directory_script_still_lists() -> None:
    script = read("scripts/verify-public-directory-e2e.py")
    assert '"--list-publicly"' in script, (
        "verify-public-directory-e2e.py deliberately lists and must keep --list-publicly"
    )


def test_core_options_public_listing_lock() -> None:
    header = read("co-core/include/F4TServerConfiguration.h")
    assert "bool publicListingLocked{};" in header, "Options must carry the P2P listing lock"
    cpp = read("co-core/src/config/F4TServerConfiguration.cpp")
    assert "publicListingLocked =" not in cpp, "the config parser must never set the lock"


def test_bootstrap_flag_sets_and_clears_the_lock() -> None:
    bootstrap = read("CO-SERVER/server-cpp/src/ServerBootstrap.cpp")
    off = body(bootstrap, 'if (argument == "--no-public-listing")')
    assert "a_out.options.publicListing = false;" in off, "--no-public-listing must turn listing off"
    assert "a_out.options.publicListingLocked = true;" in off, "--no-public-listing must set the lock"
    on = body(bootstrap, 'if (argument == "--list-publicly")')
    assert "a_out.options.publicListing = true;" in on, "--list-publicly must stay a working opt-in"
    assert "a_out.options.publicListingLocked = false;" in on, "--list-publicly must clear the lock"


def test_live_apply_public_listing_lock_guard() -> None:
    cpp = read("CO-SERVER/server-cpp/src/ServerLiveSettings.cpp")
    apply = body(cpp, "AdminActionResult ApplyServerLiveSettings")
    try:
        guard = apply.index("if (options.publicListingLocked)")
    except ValueError as exc:
        raise AssertionError("ApplyServerLiveSettings must check the P2P listing lock") from exc
    force = apply.index("requested.publicListing = false;", guard)
    directory_url = apply.index('if (requested.publicListing && requested.directoryUrl.empty())')
    assert guard < force < directory_url, (
        "the lock must force public_listing false before the directory URL check"
    )


def test_config_reply_reports_public_listing_lock() -> None:
    link = read("CO-SERVER/server-cpp/src/ServerLiveSettingsLink.inc")
    assert '"publicListingLocked", options.publicListingLocked' in link, (
        "the running-config reply must carry the top-level publicListingLocked sibling"
    )
    assert '"document", runningSettingsDocument()' in link, (
        "the reply document must stay the canonical running settings document"
    )


def main() -> int:
    failures: list[str] = []
    tests = [(name, value) for name, value in sorted(globals().items()) if name.startswith("test_") and callable(value)]
    for name, func in tests:
        try:
            func()
        except AssertionError as exc:
            failures.append(f"{name}: {exc}")
        except Exception as exc:  # noqa: BLE001 - report, don't hide, unexpected errors
            failures.append(f"{name}: unexpected {type(exc).__name__}: {exc}")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print(f"{Path(__file__).stem}: {len(tests)} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
