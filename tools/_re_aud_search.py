"""RE: find ALL .aud files in nested MIX tree, with their file sizes."""
import os, sys, struct
sys.path.insert(0, r'E:\ra2source\tools')

def walk(data, depth=0, max_depth=4, path=''):
    if depth > max_depth:
        return []
    if len(data) < 32:
        return []
    n = struct.unpack('<I', data[:4])[0]
    if not (0 < n < 1000000):
        return []
    hdr_size = 32 + n * 12
    if len(data) < hdr_size:
        return []
    results = []
    for i in range(n):
        off = 32 + i*12
        crid, file_off, size = struct.unpack('<III', data[off:off+12])
        if file_off + size > len(data):
            continue
        # Try as nested MIX
        chunk = data[file_off:file_off+size]
        if 32 + 12 <= len(chunk):
            inner_n = struct.unpack('<I', chunk[:4])[0]
            if 0 < inner_n < 100000:
                # Nested MIX
                sub_results = walk(chunk, depth+1, max_depth, path + f'/{crid:08X}')
                results.extend(sub_results)
                continue
        # Not a MIX - probably a leaf file
        results.append((path + f'/{crid:08X}', crid, size, file_off))
    return results

results = []
for mixfile in [r'D:\westwood\RA2YR\ra2.mix', r'D:\westwood\RA2YR\ra2md.mix',
                r'D:\westwood\RA2YR\expandmd01.mix']:
    with open(mixfile, 'rb') as f:
        data = f.read(50*1024*1024)
    r = walk(data, path=os.path.basename(mixfile))
    if r:
        results.extend(r)

# Print AUD-like (small, common audio size range 100-200000)
import os
aud_results = [r for r in results if 100 < r[2] < 200000 and r[2] % 2 == 0]
print(f'Total leaves found: {len(results)}')
print(f'AUD-like (100-200000 bytes, even): {len(aud_results)}')
for r in aud_results[:30]:
    print(f'  {r[0]} size={r[2]}')