import argparse
import json
import re
import subprocess
import sys

MERGE_SUBJECT = re.compile(r"Merge pull request #(\d+) from (\S+)")
SQUASH_SUBJECT = re.compile(r"(.+?) \(#(\d+)\)")
RELEASE_TAG = re.compile(r"[0-9]{1,9}")
SHA = re.compile(r"[0-9a-f]{40}")
TC = re.compile(r"(\d+/)?\d+(\.\d+)?(\+\d+(\.\d+)?)?")
BOOK = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,99}\.(pgn|epd)")
COUNT_FIELDS = ("test", "wins", "draws", "losses", "threads", "hash")
NUMBER_FIELDS = ("elo", "eloError", "los", "llr", "elo0", "elo1")


class NotesError(Exception):
    pass


def git(*args):
    return subprocess.run(["git", *args], check=True, capture_output=True, text=True).stdout


def ordinal(number):
    if 10 <= number % 100 <= 20:
        suffix = "th"
    else:
        suffix = {1: "st", 2: "nd", 3: "rd"}.get(number % 10, "th")
    return f"{number}{suffix}"


def isCount(value):
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def isNumber(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def colourCounts(value):
    return isinstance(value, list) and len(value) == 3 and all(isCount(item) for item in value)


def parseStrength(text, previous, previousSha):
    try:
        strength = json.loads(text)
    except json.JSONDecodeError as error:
        raise NotesError(f"strength is not JSON: {error}") from None
    if not isinstance(strength, dict):
        raise NotesError("strength must be a JSON object")
    for name in COUNT_FIELDS:
        if not isCount(strength.get(name)):
            raise NotesError(f"strength.{name} must be a non-negative integer")
    for name in NUMBER_FIELDS:
        if not isNumber(strength.get(name)):
            raise NotesError(f"strength.{name} must be a number")
    if strength.get("base") != previous:
        raise NotesError(f"the match was played against {strength.get('base')!r}, the last release is {previous}")
    if strength.get("baseSha") != previousSha:
        raise NotesError(f"the match base {strength.get('baseSha')!r} is not release {previous} ({previousSha})")
    if not isinstance(strength.get("tc"), str) or not TC.fullmatch(strength["tc"]):
        raise NotesError("strength.tc is not a time control")
    if not isinstance(strength.get("book"), str) or not BOOK.fullmatch(strength["book"]):
        raise NotesError("strength.book is not an opening book name")
    colours = strength.get("colors")
    if not isinstance(colours, dict) or not all(colourCounts(colours.get(side)) for side in ("white", "black")):
        raise NotesError("strength.colors needs white and black win, draw and loss counts")
    if strength["wins"] + strength["draws"] + strength["losses"] == 0:
        raise NotesError("the match has no games")
    if strength["elo0"] >= strength["elo1"]:
        raise NotesError("strength.elo0 must be below strength.elo1")
    return strength


def oneLine(text):
    return " ".join(text.split())


def describeCommit(sha, subject, body):
    merge = MERGE_SUBJECT.fullmatch(subject)
    if merge:
        lines = [line for line in body.splitlines() if line.strip()]
        title = oneLine(lines[0]) if lines else merge.group(2)
        return f"{title} #{merge.group(1)}"
    squash = SQUASH_SUBJECT.fullmatch(subject)
    if squash:
        return f"{oneLine(squash.group(1))} #{squash.group(2)}"
    return f"{oneLine(subject)} {sha[:7]}"


def changes(previous, sha):
    output = git("log", "--first-parent", "--reverse", "--format=%H%x1f%s%x1f%b%x1e", f"{previous}..{sha}")
    items = []
    for record in output.split("\x1e"):
        record = record.strip("\n")
        if not record:
            continue
        commitSha, subject, body = (record.split("\x1f") + ["", ""])[:3]
        items.append(describeCommit(commitSha, subject, body))
    return items


def score(wins, draws, losses):
    games = wins + draws + losses
    return 100.0 * (wins + draws / 2) / games if games else 0.0


def strengthSection(version, previous, strength):
    wins, draws, losses = strength["wins"], strength["draws"], strength["losses"]
    games = wins + draws + losses
    white = strength["colors"]["white"]
    black = strength["colors"]["black"]
    elo0 = f"{strength['elo0']:g}"
    elo1 = f"{strength['elo1']:g}"
    threads = "1 thread" if strength["threads"] == 1 else f"{strength['threads']} threads"
    lines = [
        "### Strength",
        "",
        f"**Kurgan {version}** passed an SPRT `[{elo0}, {elo1}]` against **Kurgan {previous}** at "
        f"`{strength['tc']}`. Both engines used {threads} and a {strength['hash']} MB hash, and the "
        f"openings came from `{strength['book']}` with every opening played with both colours.",
        "",
        f"| Games | Kurgan {version} | Score | Elo difference | LOS |",
        "|---|---|---|---|---|",
        f"| {games:,} | {wins} W, {draws} D, {losses} L | **{score(wins, draws, losses):.2f}%** | "
        f"**{strength['elo']:+.1f} ± {strength['eloError']:.1f}** | {strength['los']:.2f}% |",
        "",
        f"Kurgan {version} scored {score(*white):.1f}% as White and {score(*black):.1f}% as Black. "
        f"The SPRT ended with LLR {strength['llr']:.2f}.",
    ]
    return lines


def releaseNotes(version, previous, sha, strength):
    lines = [f"This is the {ordinal(version)} release of Kurgan.", "", "### Changes", ""]
    items = changes(previous, sha)
    lines += [f"{index}. {item}" for index, item in enumerate(items, 1)] or ["No changes since the last release."]
    lines += [""] + strengthSection(version, previous, strength)
    return "\n".join(lines) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description="Write the release notes of a Kurgan release")
    parser.add_argument("--previous", required=True, help="tag of the last release")
    parser.add_argument("--sha", required=True, help="commit being released")
    parser.add_argument("--strength", required=True, help="release match result as JSON")
    parser.add_argument("--output", help="file for the notes, standard output when missing")
    args = parser.parse_args(argv)
    try:
        if not RELEASE_TAG.fullmatch(args.previous):
            raise NotesError(f"{args.previous!r} is not a release tag")
        if not SHA.fullmatch(args.sha):
            raise NotesError(f"{args.sha!r} is not a full commit hash")
        previousSha = git("rev-parse", "--verify", f"refs/tags/{args.previous}^{{commit}}").strip()
        strength = parseStrength(args.strength, args.previous, previousSha)
        notes = releaseNotes(int(args.previous) + 1, args.previous, args.sha, strength)
    except (NotesError, subprocess.CalledProcessError) as error:
        print(f"release notes: {error}", file=sys.stderr)
        return 1
    if args.output:
        with open(args.output, "w", encoding="utf-8") as handle:
            handle.write(notes)
    else:
        sys.stdout.write(notes)
    return 0


if __name__ == "__main__":
    sys.exit(main())
