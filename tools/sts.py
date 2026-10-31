import argparse
import glob
import json
import logging
import os
import queue
import re
import sys
import threading

try:
    import chess
    import chess.engine
except ImportError:
    sys.exit('python-chess is required: pip install python-chess')

SUITE_NAMES = {
    1: 'Undermining',
    2: 'Open Files and Diagonals',
    3: 'Knight Outposts',
    4: 'Square Vacancy',
    5: 'Bishop vs Knight',
    6: 'Recapturing',
    7: 'Offer of Simplification',
    8: 'Advancement of f/g/h Pawns',
    9: 'Advancement of a/b/c Pawns',
    10: 'Simplification',
    11: 'Activity of the King',
    12: 'Center Control',
    13: 'Pawn Play in the Center',
    14: 'Queens and Rooks to the 7th Rank',
    15: 'Avoid Pointless Exchange',
}


def parseArgs():
    parser = argparse.ArgumentParser(description='Run the Strategic Test Suite against a UCI engine.')
    parser.add_argument('engine', help='path to the engine binary')
    parser.add_argument('suites', nargs='+', help='STS epd files or directories that contain them')
    parser.add_argument('--movetime', type=float, default=1.0, help='seconds per position')
    parser.add_argument('--workers', type=int, default=max(1, (os.cpu_count() or 2) // 2), help='parallel engine processes')
    parser.add_argument('--hash', type=int, default=64, help='hash size in MB for each engine')
    parser.add_argument('--threads', type=int, default=1, help='search threads for each engine')
    parser.add_argument('--output', help='write per-position results to this json file')
    return parser.parse_args()


def suiteFiles(paths):
    files = []
    for path in paths:
        if os.path.isdir(path):
            files.extend(glob.glob(os.path.join(glob.escape(path), '*.epd')))
        else:
            files.append(path)
    return sorted(set(files), key=suiteNumber)


def suiteNumber(path):
    match = re.search(r'(\d+)\.epd$', os.path.basename(path))
    return int(match.group(1)) if match else 0


def stripSan(san):
    return san.rstrip('+#!?')


def parseEpd(line):
    board, ops = chess.Board.from_epd(line)
    scores = {}
    c0 = ops.get('c0')
    if isinstance(c0, str):
        for part in c0.split(','):
            match = re.match(r'\s*(\S+)=(\d+)\s*$', part)
            if match:
                scores[stripSan(match.group(1))] = int(match.group(2))
    for move in ops.get('bm') or []:
        scores.setdefault(stripSan(board.san(move)), 10)
    return board, scores, ops.get('id', '')


def loadTasks(files):
    tasks = queue.Queue()
    for path in files:
        with open(path, encoding='utf-8', errors='replace') as handle:
            for line in handle:
                line = line.strip()
                if line:
                    tasks.put((suiteNumber(path), line))
    return tasks


def runWorker(args, tasks, results, lock):
    engine = chess.engine.SimpleEngine.popen_uci(args.engine)
    try:
        engine.configure({'Hash': args.hash, 'Threads': args.threads})
        while True:
            try:
                suite, line = tasks.get_nowait()
            except queue.Empty:
                return
            board, scores, positionId = parseEpd(line)
            engine.protocol.send_line('ucinewgame')
            played = engine.play(board, chess.engine.Limit(time=args.movetime), game=object())
            san = stripSan(board.san(played.move))
            points = scores.get(san, 0)
            with lock:
                results.append({'suite': suite, 'id': positionId, 'move': san, 'points': points, 'best': points == 10})
    finally:
        engine.quit()


def report(results):
    bySuite = {}
    for result in results:
        entry = bySuite.setdefault(result['suite'], [0, 0, 0])
        entry[0] += result['points']
        entry[1] += 1
        entry[2] += result['best']
    for suite in sorted(bySuite):
        points, count, best = bySuite[suite]
        name = SUITE_NAMES.get(suite, '')
        print('STS%-3d %-34s %5d / %5d  best %3d / %3d' % (suite, name, points, count * 10, best, count))
    total = sum(v[0] for v in bySuite.values())
    count = sum(v[1] for v in bySuite.values())
    best = sum(v[2] for v in bySuite.values())
    percent = 100.0 * total / max(1, count * 10)
    print('TOTAL  %-34s %5d / %5d  best %3d / %3d  (%.1f%%)' % ('', total, count * 10, best, count, percent))


def main():
    args = parseArgs()
    logging.getLogger('chess.engine').setLevel(logging.CRITICAL)
    files = suiteFiles(args.suites)
    if not files:
        sys.exit('no epd files found')

    tasks = loadTasks(files)
    results = []
    lock = threading.Lock()
    threads = [threading.Thread(target=runWorker, args=(args, tasks, results, lock)) for _ in range(args.workers)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    if args.output:
        with open(args.output, 'w') as handle:
            json.dump(results, handle, indent=1)
    report(results)


if __name__ == '__main__':
    main()
