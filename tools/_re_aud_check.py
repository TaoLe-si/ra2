"""RE: AUD file format. Read NSWEEP.AUD header and verify against Westwood docs."""
import sys, struct

with open(r'D:\westwood\RA2YR\ra2.mix', 'rb') as f:
    data = f.read()

n_files = struct.unpack('<I', data[0:4])[0]
print(f'ra2.mix top-level: {n_files} entries')

target = 0x34FCEF29  # NSWEEP.AUD

# Recursive MIX search
def find_in(data, target_id, max_depth=8, _depth=0):
    if _depth >= max_depth:
        return None
    # Read first 4 bytes as n_files (must be reasonable)
    n = struct.unpack('<I', data[0:4])[0]
    if not (0 < n < 2000000):
        return None
    hdr_size = 32 + n * 12
    if len(data) < hdr_size:
        return None
    # Search entries
    for i in range(n):
        off = 32 + i*12
        crid, file_off, size = struct.unpack('<III', data[off:off+12])
        if crid == target_id:
            return (file_off, size)
        # Try as nested MIX
        if 0 < file_off < len(data) and 0 < size < 500_000_000:
            nested = data[file_off:file_off+size]
            r = find_in(nested, target_id, max_depth, _depth+1)
            if r:
                return r
    return None

r = find_in(data, target)
if r:
    file_off, size = r
    print(f'Found NSWEEP.AUD at offset {file_off:#x}, size {size}')
    chunk = data[file_off:file_off+32]
    print('First 32 bytes:', ' '.join(f'{b:02X}' for b in chunk))
    if size >= 14:
        magic = chunk[0]
        data_size = struct.unpack('<H', chunk[1:3])[0]
        sample_rate = struct.unpack('<H', chunk[3:5])[0]
        compression = chunk[5]
        channels_minus1 = chunk[6]
        reserved = struct.unpack('<H', chunk[7:9])[0]
        print(f'  magic=0x{magic:02X} data_size={data_size} sample_rate={sample_rate}Hz compression={compression} channels={channels_minus1+1}')
        # Save raw bytes for inspection
        with open(r'build\_nsweep.aud', 'wb') as f:
            f.write(data[file_off:file_off+size])
        print(f'  Saved to build/_nsweep.aud')
else:
    print('Not found in ra2.mix')