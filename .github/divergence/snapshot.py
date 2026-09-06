"""Report custom patch ownership separately from current-upstream distance."""
import argparse
import json
from pathlib import Path
import subprocess
import report

ROOT = Path(__file__).resolve().parents[2]

def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args], text=True).strip()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worktree", action="store_true")
    args = parser.parse_args()
    registry = json.loads((ROOT / ".github/divergence/registry.json").read_text())
    base = registry["baseline"]["sha"]
    current = "upstream/" + registry["upstream"]["branch"]
    # Fail rather than measure an unrelated checkout or an unresolvable baseline.
    git("merge-base", "--is-ancestor", base, "HEAD")
    print("# Renderer fork divergence")
    print()
    print("Baseline: `" + base + "` (" + registry["baseline"]["ref"] + ").")
    print("Current upstream: `" + git("rev-parse", current) + "`.")
    print("Fork HEAD: `" + git("rev-parse", "HEAD") + "`.")
    behind, ahead = git("rev-list", "--left-right", "--count", current + "...HEAD").split()
    print("Graph distance: " + behind + " upstream-only commits; " + ahead + " fork-side commits.")
    print("Fork-side commits can include upstream release-branch commits, not just custom work.")
    print()
    entries = report.parse_numstat(git("diff", "--numstat", current, "HEAD", "--"))
    added = sum(e["added"] for e in entries)
    deleted = sum(e["deleted"] for e in entries)
    print("Current-upstream two-tree distance: " + str(len(entries)) + " files; +" + str(added) + " / -" + str(deleted) + " lines.")
    print("This mixes custom changes and upstream evolution; it is NOT the custom patch count.")
    if args.worktree:
        print("The graph/current-upstream numbers above describe committed HEAD; the ownership table below includes working-tree edits.")
    print()
    print("## Custom delta against the pinned upstream baseline")
    print()
    custom_args = ["diff", "--numstat", base]
    if not args.worktree:
        custom_args.append("HEAD")
    custom = git(*custom_args, "--") + "\n"
    if args.worktree:
        custom += report.untracked_numstat(ROOT)
    custom_entries = report.parse_numstat(custom)
    loaded_registry = report.load_registry(ROOT / ".github/divergence/registry.json")
    buckets, _ = report.classify(custom_entries, loaded_registry)
    measured = report.weigh(buckets, loaded_registry, custom_entries)
    implementation = [g for g in measured["groups"] if g["id"] != "fork-accounting"]
    print("Custom implementation subtotal (excluding documentation/accounting): "
          + str(sum(g["files"] for g in implementation)) + " files; "
          + str(sum(g["lines"] for g in implementation)) + " changed lines.")
    print()
    command = ["--repo", str(ROOT), "--base", base, "--base-label", registry["baseline"]["ref"]]
    if args.worktree:
        command.append("--worktree")
    return report.main(command)

if __name__ == "__main__":
    raise SystemExit(main())
