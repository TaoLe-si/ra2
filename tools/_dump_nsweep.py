"""Direct read of NSWEEP.AUD header from ra2.mix top-level."""
import struct

with open(r'D:\westwood\RA2YR\ra2.mix', 'rb') as f:
    data = f.read()

n_files = struct.unpack('<I', data[:4])[0]
hdr_size = 32 + n_files * 12
target = 0x34FCEF29  # NSWEEP.AUD

for i in range(n_files):
    off = 32 + i*12
    if off + 12 > len(data):
        break
    crid, file_off, size = struct.unpack('<III', data[off:off+12])
    if crid == target:
        print(f'NSWEEP.AUD at file offset {file_off:#x}, size {size}')
        chunk = data[file_off:file_off+32]
        print('First 32 bytes:')
        print(' '.join(f'{b:02X}' for b in chunk))
        # Try parsing as AUD
        if size >= 14:
            magic = chunk[0]
            data_size = struct.unpack('<H', chunk[1:3])[0]
            sample_rate = struct.unpack('<H', chunk[3:5])[0]
            comp = chunk[5]
            ch = chunk[6] + 1
            reserved = struct.unpack('<H', chunk[7:9])[0]
            print(f'AUD: magic=0x{magic:02X} data_size={data_size} rate={sample_rate} comp={comp} channels={ch} reserved={reserved}')
        break
else:
    print('NSWEEP.AUD NOT FOUND in top-level ra2.mix')