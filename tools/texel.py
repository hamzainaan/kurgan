import argparse
import glob
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE_DIR = os.path.join(ROOT, 'src')
HELPER = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'texel.cpp')
HEADER = os.path.join(SOURCE_DIR, 'tuned_params.h')

ENGINE_SOURCES = ['evaluate.cpp', 'position.cpp', 'movegen.cpp', 'endgame.cpp', 'nnue.cpp', 'stats.cpp', 'tt.cpp', 'see.cpp']
DECLARATION = re.compile(r'constexpr\s+(std::array<[^=;]*?>|int)\s+([A-Z0-9_]+)\s*=\s*(.*?);', re.DOTALL)
KING_SAFETY_START = 'KING SAFETY BLOCK'
KING_SAFETY_END = 'END KING SAFETY BLOCK'
RESULT_TEXT = {'1-0': 1.0, '0-1': 0.0, '1/2-1/2': 0.5}


def parseArgs():
    parser = argparse.ArgumentParser(description='Tune the hand-crafted evaluation with Texel tuning.')
    parser.add_argument('datasets', nargs='+', help='labeled positions, one fen and result per line')
    parser.add_argument('--limit', type=int, default=0, help='randomly sample at most this many positions')
    parser.add_argument('--seed', type=int, default=1, help='seed for sampling')
    parser.add_argument('--threads', type=int, default=os.cpu_count() or 1, help='processes used for linearization')
    parser.add_argument('--rounds', type=int, default=2, help='linearize and optimize this many times')
    parser.add_argument('--epochs', type=int, default=400, help='Adam epochs per round')
    parser.add_argument('--lr', type=float, default=0.5, help='Adam learning rate in evaluation units')
    parser.add_argument('--step', type=int, default=4, help='finite difference step used for linearization')
    parser.add_argument('--k', type=float, default=0.0, help='sigmoid scale, fitted when zero')
    parser.add_argument('--params', default='', help='comma separated parameter names to tune, default all')
    parser.add_argument('--king-safety', action='store_true', help='also tune the king safety block')
    parser.add_argument('--header-out', default=HEADER, help='where to write the tuned header')
    parser.add_argument('--dry-run', action='store_true', help='report the result without writing a header')
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'), help='C++ compiler')
    return parser.parse_args()


def parseResult(text):
    match = re.search(r'1/2-1/2|1-0|0-1', text)
    if match:
        return RESULT_TEXT[match.group(0)]
    match = re.search(r'\[\s*([01](?:\.\d+)?)\s*\]', text)
    if match:
        return float(match.group(1))
    match = re.search(r'(?:^|[\s;|,])([01](?:\.\d+)?)\s*;?\s*$', text)
    if match:
        return float(match.group(1))
    return None


def parseLine(line):
    fields = line.split()
    if len(fields) < 5 or '/' not in fields[0] or fields[1] not in ('w', 'b'):
        return None
    result = parseResult(' '.join(fields[4:]))
    if result is None:
        return None
    clocks = fields[4:6] if len(fields) >= 6 and fields[4].isdigit() and fields[5].isdigit() else ['0', '1']
    return result, ' '.join(fields[:4] + clocks)


def prepareData(args, path):
    rng = random.Random(args.seed)
    kept = []
    seen = 0
    for dataset in args.datasets:
        with open(dataset, encoding='utf-8', errors='replace') as handle:
            for line in handle:
                parsed = parseLine(line.strip())
                if parsed is None:
                    continue
                seen += 1
                if args.limit <= 0 or len(kept) < args.limit:
                    kept.append(parsed)
                else:
                    slot = rng.randrange(seen)
                    if slot < args.limit:
                        kept[slot] = parsed
    if not kept:
        sys.exit('no labeled positions found')
    with open(path, 'w') as handle:
        for result, fen in kept:
            handle.write('%g %s\n' % (result, fen))
    print('positions read %d, used %d' % (seen, len(kept)), flush=True)


def kingSafetyRange(header):
    start = header.find(KING_SAFETY_START)
    end = header.find(KING_SAFETY_END)
    if start < 0 or end < 0:
        return (len(header), len(header))
    return (start, end)


def declarations(header):
    return [(match.group(2), match) for match in DECLARATION.finditer(header)]


def selectParams(args, header):
    low, high = kingSafetyRange(header)
    wanted = {name.strip() for name in args.params.split(',') if name.strip()}
    names = []
    for name, match in declarations(header):
        if wanted and name not in wanted:
            continue
        if not args.king_safety and low <= match.start() < high:
            continue
        names.append(name)
    missing = wanted - set(names)
    if missing:
        sys.exit('unknown or excluded parameters: ' + ', '.join(sorted(missing)))
    if not names:
        sys.exit('no parameters selected')
    return names


def buildHelper(args, workDir, names):
    for path in glob.glob(os.path.join(SOURCE_DIR, '*.h')):
        shutil.copy(path, workDir)
    sources = []
    for name in ENGINE_SOURCES:
        path = os.path.join(SOURCE_DIR, name)
        if os.path.exists(path):
            shutil.copy(path, workDir)
            sources.append(os.path.join(workDir, name))

    with open(HEADER) as handle:
        header = handle.read()
    mutable = DECLARATION.sub(lambda m: m.group(0).replace('constexpr', 'inline', 1), header)
    with open(os.path.join(workDir, 'tuned_params.h'), 'w') as handle:
        handle.write(mutable)

    entries = ',\n'.join(
        '    {"%s", reinterpret_cast<int *>(&tuned::%s), static_cast<int>(sizeof(tuned::%s) / sizeof(int))}' % (n, n, n)
        for n in names)
    with open(os.path.join(workDir, 'texel_params.h'), 'w') as handle:
        handle.write('#pragma once\n\n#include "tuned_params.h"\n\nstruct TexelParam\n{\n    const char *name;\n'
                     '    int *data;\n    int count;\n};\n\ninline TexelParam texelParams[] = {\n' + entries + '\n};\n')

    shutil.copy(HELPER, workDir)
    binary = os.path.join(workDir, 'texel')
    command = [args.cxx, '-std=c++23', '-O3', '-march=native', '-DNDEBUG', '-I', workDir,
               os.path.join(workDir, 'texel.cpp')] + sources + ['-o', binary, '-pthread']
    print('building the tuning helper', flush=True)
    build = subprocess.run(command, capture_output=True, text=True)
    if build.returncode != 0:
        sys.exit(build.stderr)
    return binary


def readValues(path):
    values = {}
    with open(path) as handle:
        for line in handle:
            fields = line.split()
            if fields:
                values[fields[0]] = [int(v) for v in fields[1:]]
    return values


def rewriteHeader(header, values):
    changed = 0
    pieces = []
    last = 0
    for name, match in declarations(header):
        if name not in values:
            continue
        numbers = iter(values[name])
        initializer = match.group(3)
        tokens = re.findall(r'-?\d+', initializer)
        if len(tokens) != len(values[name]):
            sys.exit('value count mismatch for ' + name)

        def replace(token):
            nonlocal changed
            value = next(numbers)
            if int(token.group(2)) != value:
                changed += 1
            width = len(token.group(0))
            return str(value).rjust(width) if token.group(1) else str(value)

        updated = re.sub(r'([ \t]*)(-?\d+)', replace, initializer)
        start, end = match.span(3)
        pieces.append(header[last:start])
        pieces.append(updated)
        last = end
    pieces.append(header[last:])
    return ''.join(pieces), changed


def main():
    args = parseArgs()
    with open(HEADER) as handle:
        header = handle.read()
    names = selectParams(args, header)

    workDir = tempfile.mkdtemp(prefix='kurgan_texel_')
    try:
        dataPath = os.path.join(workDir, 'data.txt')
        prepareData(args, dataPath)
        binary = buildHelper(args, workDir, names)
        outPath = os.path.join(workDir, 'values.txt')
        command = [binary, dataPath, outPath, workDir, str(args.threads), str(args.rounds), str(args.epochs),
                   str(args.lr), str(args.step), str(args.k), 'run']
        if subprocess.run(command).returncode != 0:
            sys.exit('tuning failed')

        updated, changed = rewriteHeader(header, readValues(outPath))
        print('%d values changed' % changed)
        if args.dry_run:
            return
        with open(args.header_out, 'w') as handle:
            handle.write(updated)
        print('wrote ' + args.header_out)
    finally:
        shutil.rmtree(workDir, ignore_errors=True)


if __name__ == '__main__':
    main()
