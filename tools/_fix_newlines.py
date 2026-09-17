"""_fix_newlines.py -- one-shot: repair raw newlines inside C string literals."""
import sys

p = 'E:/ra2source/src/game/GameShell.cpp'
d = open(p, 'rb').read()

BAD = set([0x22, 0x0D, 0x0A, 0x5C])  # " CR LF backslash

i = 0
fixes = 0
while True:
    j = d.find(b'\n");', i)
    if j < 0:
        break
    k = j - 1
    while k >= 0 and d[k] == 0x0D:
        k -= 1
    if k < 0:
        i = j + 4
        continue
    c = d[k]
    if c not in BAD and not (48 <= c <= 57):  # not quote/CR/LF/backslash/digit
        d = d[:k + 1] + b'\\n");' + d[j + 4:]
        fixes += 1
        i = k + 6
    else:
        i = j + 4

print('fixed:', fixes)
open(p, 'wb').write(d)
