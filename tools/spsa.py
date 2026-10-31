import argparse
import json
import os
import random
import re
import subprocess
import sys
import tempfile
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SEARCH_SOURCE = os.path.join(ROOT, 'src', 'search.cpp')
PARAMS_HEADER = os.path.join(ROOT, 'src', 'tuned_params.h')

PRUNING_PARAMS = [
    'RFP Margin',
    'Razor Margin',
    'NMP Eval Div',
    'ProbCut Margin',
    'LMP Base',
    'History Prune Margin',
    'Futility Base',
    'Futility Margin',
    'SEE Quiet Margin',
    'SEE Capture Margin',
    'Capture Futility Base',
    'Capture Futility Margin',
    'QS Futility Margin',
]

ALPHA = 0.602
GAMMA = 0.101
A_RATIO = 0.1


def parseArgs():
    parser = argparse.ArgumentParser(description='Tune search parameters with SPSA through cutechess-cli.')
    parser.add_argument('--engine', default=os.path.join(ROOT, 'kurgan'), help='engine binary to tune')
    parser.add_argument('--book', required=True, help='opening book in epd format')
    parser.add_argument('--state', default='spsa_state.json', help='state file used to save and resume the run')
    parser.add_argument('--pairs', type=int, default=4000, help='total game pairs')
    parser.add_argument('--workers', type=int, default=max(1, (os.cpu_count() or 2) // 2), help='concurrent game pairs')
    parser.add_argument('--tc', default='8+0.08', help='time control for every game')
    parser.add_argument('--hash', type=int, default=16, help='hash size in MB')
    parser.add_argument('--params', default='pruning', help='"pruning", "all" or a comma separated list of UCI names')
    parser.add_argument('--c-ratio', type=float, default=0.1, help='final perturbation as a share of the start value')
    parser.add_argument('--r-end', type=float, default=0.005, help='final learning rate')
    parser.add_argument('--cutechess', default='cutechess-cli', help='cutechess-cli executable')
    parser.add_argument('--apply', action='store_true', help='write the tuned values into src/tuned_params.h')
    return parser.parse_args()


def readDefinitions():
    with open(SEARCH_SOURCE) as handle:
        source = handle.read()
    with open(PARAMS_HEADER) as handle:
        header = handle.read()

    starts = {name: int(value) for name, value in re.findall(r'inline int ([A-Z0-9_]+) = (-?\d+);', header)}
    definitions = {}
    for name, variable, low, high in re.findall(r'\{"([^"]+)", &tuned::([A-Z0-9_]+), (-?\d+), (-?\d+)\}', source):
        if variable in starts:
            definitions[name] = {'variable': variable, 'start': starts[variable], 'min': int(low), 'max': int(high)}
    return definitions


def selectParams(args, definitions):
    if args.params == 'all':
        names = list(definitions)
    elif args.params == 'pruning':
        names = PRUNING_PARAMS
    else:
        names = [name.strip() for name in args.params.split(',') if name.strip()]

    params = []
    for name in names:
        if name not in definitions:
            sys.exit('unknown tuning parameter: ' + name)
        definition = dict(definitions[name])
        definition['name'] = name
        definition['cEnd'] = max(1.0, abs(definition['start']) * args.c_ratio)
        params.append(definition)
    return params


class Tuner:
    def __init__(self, args, params):
        self.args = args
        self.params = params
        self.lock = threading.Lock()
        self.state = self.loadState()

    def loadState(self):
        names = [p['name'] for p in self.params]
        if os.path.exists(self.args.state):
            with open(self.args.state) as handle:
                state = json.load(handle)
            if sorted(state['theta']) != sorted(names):
                sys.exit('state file belongs to a different parameter set')
            return state
        return {'iteration': 0, 'completed': 0, 'wins': 0, 'losses': 0, 'draws': 0,
                'theta': {p['name']: float(p['start']) for p in self.params}}

    def saveState(self):
        temporary = self.args.state + '.tmp'
        with open(temporary, 'w') as handle:
            json.dump(self.state, handle, indent=1)
        os.replace(temporary, self.args.state)

    def clamp(self, param, value):
        return max(param['min'], min(param['max'], value))

    def schedule(self, param, k):
        pairs = self.args.pairs
        bigA = A_RATIO * pairs
        c = param['cEnd'] * pairs ** GAMMA
        a = self.args.r_end * param['cEnd'] ** 2 * (bigA + pairs) ** ALPHA
        ck = c / (k + 1) ** GAMMA
        ak = a / (bigA + k + 1) ** ALPHA
        return ck, ak / (ck * ck)

    def initString(self, values):
        return '\\n'.join('setoption name %s value %d' % (name, value) for name, value in values.items())

    def playPair(self, plus, minus):
        command = [
            self.args.cutechess,
            '-engine', 'cmd=' + os.path.abspath(self.args.engine), 'name=plus', 'initstr=' + self.initString(plus),
            '-engine', 'cmd=' + os.path.abspath(self.args.engine), 'name=minus', 'initstr=' + self.initString(minus),
            '-each', 'proto=uci', 'tc=' + self.args.tc, 'option.Hash=%d' % self.args.hash, 'option.Threads=1',
            '-openings', 'file=' + os.path.abspath(self.args.book), 'format=epd', 'order=random',
            '-repeat', '-games', '2', '-rounds', '1',
            '-draw', 'movenumber=40', 'movecount=8', 'score=10',
            '-resign', 'movecount=4', 'score=800',
            '-recover',
        ]
        with tempfile.TemporaryDirectory() as directory:
            output = subprocess.run(command, capture_output=True, text=True, cwd=directory).stdout
        scores = re.findall(r'Score of plus vs minus: (\d+) - (\d+) - (\d+)', output)
        if not scores:
            return None
        wins, losses, draws = map(int, scores[-1])
        return (wins, losses, draws) if wins + losses + draws == 2 else None

    def work(self):
        while True:
            with self.lock:
                k = self.state['iteration']
                if k >= self.args.pairs:
                    return
                self.state['iteration'] = k + 1
                steps = {p['name']: self.schedule(p, k) for p in self.params}
                signs = {p['name']: random.choice((-1, 1)) for p in self.params}
                plus, minus = {}, {}
                for p in self.params:
                    name = p['name']
                    theta = self.state['theta'][name]
                    ck = steps[name][0]
                    plus[name] = int(round(self.clamp(p, theta + ck * signs[name])))
                    minus[name] = int(round(self.clamp(p, theta - ck * signs[name])))

            result = self.playPair(plus, minus)

            with self.lock:
                if result is None:
                    print('pair %d failed' % k, flush=True)
                    continue
                wins, losses, draws = result
                self.state['completed'] += 1
                self.state['wins'] += wins
                self.state['losses'] += losses
                self.state['draws'] += draws
                for p in self.params:
                    name = p['name']
                    ck, rk = steps[name]
                    theta = self.state['theta'][name] + rk * ck * (wins - losses) * signs[name]
                    self.state['theta'][name] = self.clamp(p, theta)
                self.saveState()

    def progress(self):
        with self.lock:
            values = ' '.join('%s=%.1f' % (name, value) for name, value in self.state['theta'].items())
            print('%s pairs %d/%d  W %d L %d D %d  %s' % (time.strftime('%H:%M:%S'), self.state['completed'], self.args.pairs,
                                                       self.state['wins'], self.state['losses'], self.state['draws'], values),
                  flush=True)

    def run(self):
        if self.state['iteration'] > self.state['completed']:
            self.state['iteration'] = self.state['completed']
        threads = [threading.Thread(target=self.work, daemon=True) for _ in range(self.args.workers)]
        for thread in threads:
            thread.start()
        last = time.time()
        try:
            while any(thread.is_alive() for thread in threads):
                time.sleep(1)
                if time.time() - last >= 60:
                    last = time.time()
                    self.progress()
        except KeyboardInterrupt:
            with self.lock:
                self.saveState()
            sys.exit('interrupted, state saved to ' + self.args.state)
        self.progress()


def applyValues(params, theta):
    with open(PARAMS_HEADER) as handle:
        header = handle.read()
    for p in params:
        value = int(round(theta[p['name']]))
        header, count = re.subn(r'(inline int %s = )-?\d+;' % p['variable'], r'\g<1>%d;' % value, header)
        if count != 1:
            sys.exit('could not update ' + p['variable'])
    with open(PARAMS_HEADER, 'w') as handle:
        handle.write(header)


def main():
    args = parseArgs()
    params = selectParams(args, readDefinitions())
    tuner = Tuner(args, params)
    tuner.run()

    print('tuned values:')
    for p in params:
        print('  %-26s %6d -> %6d' % (p['name'], p['start'], int(round(tuner.state['theta'][p['name']]))))
    if args.apply:
        applyValues(params, tuner.state['theta'])
        print('updated ' + PARAMS_HEADER)


if __name__ == '__main__':
    main()
