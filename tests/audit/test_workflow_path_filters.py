from pathlib import Path

import yaml


ROOT = Path(__file__).resolve().parents[2]

REQUIREMENTS = {
    "co-beta-ci.yml": {
        "push": {".gitmodules", "CO-BETA/plugin/lib/commonlibf4"},
        "pull_request": {".gitmodules", "CO-BETA/plugin/lib/commonlibf4"},
    },
    "co-beta-gns-client-bridge.yml": {
        "pull_request": {
            ".gitmodules",
            "CO-BETA/ThirdParty/GameNetworkingSockets-src",
            "CO-BETA/plugin/lib/commonlibf4",
        },
    },
    "co-client-protocol-acceptance.yml": {
        "push": {".gitmodules", "CO-BETA/ThirdParty/GameNetworkingSockets-src"},
        "pull_request": {".gitmodules", "CO-BETA/ThirdParty/GameNetworkingSockets-src"},
    },
    "co-native-ci.yml": {
        "push": {".gitmodules", "CO-BETA/plugin/lib/commonlibf4"},
        "pull_request": {".gitmodules", "CO-BETA/plugin/lib/commonlibf4"},
    },
    "co-server-acceptance.yml": {
        "push": {".gitmodules", "CO-BETA/ThirdParty/GameNetworkingSockets-src"},
        "pull_request": {".gitmodules", "CO-BETA/ThirdParty/GameNetworkingSockets-src"},
    },
    "co-server-package.yml": {
        "push": {".gitmodules", "CO-BETA/ThirdParty/GameNetworkingSockets-src"},
        "pull_request": {".gitmodules", "CO-BETA/ThirdParty/GameNetworkingSockets-src"},
    },
    "co-server-release-acceptance.yml": {
        "push": {".gitmodules", "CO-BETA/ThirdParty/GameNetworkingSockets-src"},
    },
    "co-ui-ci.yml": {
        "push": {".gitmodules", "CommonwealthOnlineUI/ThirdParty/CommonLibF4"},
        "pull_request": {".gitmodules", "CommonwealthOnlineUI/ThirdParty/CommonLibF4"},
    },
}


def workflow_paths(path: Path) -> dict[str, set[str]]:
    document = yaml.safe_load(path.read_text(encoding="utf-8"))
    trigger = document.get(True, document.get("on", {}))
    return {
        event: set(trigger.get(event, {}).get("paths", []))
        for event in ("push", "pull_request")
    }


for workflow_name, event_requirements in REQUIREMENTS.items():
    paths = workflow_paths(ROOT / ".github" / "workflows" / workflow_name)
    for event, required_paths in event_requirements.items():
        if not paths[event]:
            continue  # no path filter: the workflow runs on every change, dependencies included
        missing = required_paths - paths[event]
        assert not missing, f"{workflow_name} {event} is missing dependency path filters: {sorted(missing)}"
        assert "docs/**" not in paths[event], f"{workflow_name} {event} widened to unrelated docs-only changes"

CCACHE_SCRIPT = "scripts/ci-ccache-env.sh"
for workflow in sorted((ROOT / ".github" / "workflows").glob("*.yml")):
    if CCACHE_SCRIPT not in workflow.read_text(encoding="utf-8"):
        continue
    for event, event_paths in workflow_paths(workflow).items():
        if event_paths:
            assert CCACHE_SCRIPT in event_paths, f"{workflow.name} {event} runs {CCACHE_SCRIPT} but does not list it in its path filters"

print("workflow submodule path-filter contracts passed")
