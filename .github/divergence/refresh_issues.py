"""Refresh only registered issues in the explicitly pinned fork repository."""
import json
from pathlib import Path
import subprocess
import report

root = Path(__file__).resolve().parents[2]
registry = json.loads((root / ".github/divergence/registry.json").read_text())
repo = registry["fork"]["repo"]
for group in registry["groups"]:
    number = group.get("issue")
    if not number:
        raise SystemExit("Missing issue for " + group["id"])
    existing = subprocess.check_output(
        ["gh", "issue", "view", str(number), "-R", repo, "--json", "body", "--jq", ".body"],
        text=True,
    )
    import tempfile
    with tempfile.TemporaryDirectory() as directory:
        body = Path(directory) / "body.md"
        body.write_text(existing)
        status = report.main([
            "--repo", str(root), "--base", registry["baseline"]["sha"],
            "--base-label", "pinned upstream baseline " + registry["baseline"]["ref"],
            "--head-label", registry["fork"]["branch"],
            "--issue-body", group["id"], "--existing-body", str(body), "--out", str(body),
        ])
        if status:
            raise SystemExit(status)
        if body.read_text().rstrip() != existing.rstrip():
            subprocess.run(["gh", "issue", "edit", str(number), "-R", repo,
                            "--body-file", str(body)], check=True)
