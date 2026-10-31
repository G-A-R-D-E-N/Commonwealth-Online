from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / "CommonwealthOnlineUI"
PAUSE = UI / "ui/views/CommonwealthOnline/pause-menu"
QUEST = UI / "ui/views/CommonwealthOnline/quests"
VIEW = UI / "src/F4TQuestView.cpp"
REMOVED_GATE = ("HostAvailable", "PushMenuAvailability")


def slice_between(source: str, start_anchor: str, end_anchor: str) -> str:
    start = source.find(start_anchor)
    assert start >= 0, f"{start_anchor.strip()} is missing"
    end = source.find(end_anchor, start)
    assert end > start, f"{end_anchor.strip()} must follow {start_anchor.strip()}"
    return source[start:end]


def main():
    html = (PAUSE / "index.html").read_text(encoding="utf-8")
    script = (PAUSE / "script.js").read_text(encoding="utf-8")
    view = VIEW.read_text(encoding="utf-8")
    window = (QUEST / "quests.js").read_text(encoding="utf-8")

    row = re.search(r"<button[^>]*data-action=\"quests\"[^>]*>", html)
    assert row, "the pause menu must carry a QUESTS row"
    assert "hidden" not in row.group(0), (
        "the QUESTS row must be visible before the plugin registers quest callbacks"
    )

    menu = slice_between(html, '<nav class="menu" id="menu"', "</nav>")
    ordinals = [int(value) for value in re.findall(r'data-index="(\d+)"', menu)]
    assert ordinals == list(range(len(ordinals))), (
        "the pause menu rows must carry data-index equal to their display order, "
        f"got {ordinals}"
    )

    for name, text in (
        ("pause-menu/index.html", html),
        ("pause-menu/script.js", script),
        ("F4TQuestView.cpp", view),
    ):
        assert "setQuestMenuAvailable" not in text, (
            f"{name} must not toggle the QUESTS row; the window reports availability"
        )

    unavailable = slice_between(window, "if (s.enabled !== true) {", "if (trimmed(s.mode)")
    assert "Quests are not available on this server" in unavailable
    assert 'data-action="close"' in unavailable, (
        "the not-available panel must be dismissable by pointer, not only by Escape"
    )

    for name in REMOVED_GATE:
        assert name not in view, f"F4TQuestView.cpp:{name} was the removed row gate"
        for base in ("src", "include"):
            for path in sorted((UI / base).rglob("*")):
                if path.suffix not in (".cpp", ".h", ".hpp"):
                    continue
                assert name not in path.read_text(encoding="utf-8"), (
                    f"{path.relative_to(ROOT).as_posix()} still references {name}"
                )

    notify = slice_between(view, "void NotifyStateChanged()", "bool IsVisible()")
    assert "PushState()" in notify, (
        "F4TQuestView.cpp:NotifyStateChanged must refresh an open window when the plugin reports a change"
    )

    print("Quest menu availability contract passed")


def test_quest_menu_availability():
    main()


if __name__ == "__main__":
    main()
