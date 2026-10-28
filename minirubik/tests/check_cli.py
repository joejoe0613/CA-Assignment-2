from pathlib import Path
import subprocess
import sys

binary = sys.argv[1] if len(sys.argv) > 1 else './solver'

def run(args):
    return subprocess.run([binary, *args], capture_output=True)
for row in Path('tests/solutions.txt').read_text().splitlines():
    if not row or row.startswith('#'): continue
    state, expected = row.split('|')
    r = run([state]); moves = r.stdout.decode().strip().split()
    assert r.returncode == 0 and len(moves) == len(expected.split()), (state, r)
    assert r.stdout == (' '.join(moves)+'\n').encode(), r.stdout
    # Replay externally in Python using the original model.
    p = [int(x)-1 for x in state[:7]]; o = [int(x)-1 for x in state[7:]]
    src = [[1,4,2,0,3,5,6],[0,1,2,4,5,6,3],[0,2,5,3,1,4,6]]
    twist = [[1,2,0,2,1,0,0],[0,0,0,1,2,1,2],[0]*7]
    for m in moves:
        f = 'RBD'.index(m[0]); turns = {'':1,'2':2,"'":3}[m[1:]]
        for _ in range(turns):
            p, o = [p[j] for j in src[f]], [(o[j]+twist[f][i])%3 for i,j in enumerate(src[f])]
    assert p == list(range(7)) and o == [0]*7, state
bad = '1234567111111 123456711111111 02345671111111 82345671111111 12345671111110 12345671111114 1234567111111a 11345671111111 12345671111112'.split()
for args in [[], ['12345671111111']*2] + [[s] for s in bad]:
    assert run(args).returncode == 2, args
for arg in ['21345671111111','--self-test']:
    r = subprocess.run(
    ['sh', '-c', 'exec "$1" "$2" >&-', 'sh', binary, arg],
    capture_output=True)
    assert r.returncode == 1, r
print('PASS 8 vectors: solve + optimal length + formatting; invalid input; closed stdout')
