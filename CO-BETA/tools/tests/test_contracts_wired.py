from pathlib import Path
import unittest

REPO_ROOT = Path(__file__).resolve().parents[3]
CONTRACT_DIR = REPO_ROOT / "CO-BETA" / "plugin" / "tests"
CI_TEXT_SOURCES = [*(REPO_ROOT / ".github" / "workflows").glob("*.yml"), REPO_ROOT / "scripts" / "windows-compile-ci.py"]
KNOWN_UNWIRED = {
    "F4TClientManifestContract.py", "F4TContainerChangedHubContract.py", "F4TDropMirrorRefsContract.py",
    "F4TGnsComponentReceiveContract.py", "F4THitFallbackTimeWrapContract.py", "F4TInventoryHooksContract.py",
    "F4TInventoryPickupGateContract.py", "F4TInventorySyncContract.py",
    "F4TObjectSyncContract.py", "F4TProxyMovementApproachClockContract.py",
    "F4TSessionLifecycleContract.py", "F4TWorldPolicyContract.py",
}


class ContractWiringTests(unittest.TestCase):
    def test_new_contract_scripts_are_run_by_ci_and_wired_ones_leave_the_known_list(self):
        ci = "\n".join(path.read_text(encoding="utf-8") for path in CI_TEXT_SOURCES)
        unwired = {path.name for path in CONTRACT_DIR.glob("*.py") if path.name not in ci}
        self.assertEqual(sorted(unwired ^ KNOWN_UNWIRED), [])


if __name__ == "__main__":
    unittest.main()
