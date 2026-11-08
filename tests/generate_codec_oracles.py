"""Independent exact-real binary64 decisions and rounding using Python Fraction."""
import json, math, random, struct, sys
from fractions import Fraction
from pathlib import Path

def value(bits): return struct.unpack('<d', struct.pack('<Q', bits))[0]
def bits(x): return f'{struct.unpack("<Q", struct.pack("<d", x))[0]:016x}'
rng = random.Random(20261108)
entries = []
edge = [0., -0., value(1), value(2), value((1 << 52) - 1), value(1 << 52),
        1., math.nextafter(1., 2.), -1., -2**-53, value(0x7fefffffffffffff),
        -value(0x7fefffffffffffff), math.ldexp(1., 843)]
pairs = [(x, y) for x in edge for y in edge]
for _ in range(1400):
    x = value(rng.getrandbits(64) & 0xffefffffffffffff)
    y = value(rng.getrandbits(64) & 0xffefffffffffffff)
    pairs.append((x, y))
for x, y in pairs:
    difference = abs(Fraction(x) - Fraction(y))
    try: nearest = float(difference)
    except OverflowError: nearest = math.inf
    tolerances = [1., value(1), math.ldexp(1., 843), value(0x7fefffffffffffff)]
    if math.isfinite(nearest) and nearest > 0:
        tolerances += [nearest, math.nextafter(nearest, 0.), math.nextafter(nearest, math.inf)]
    for tau in tolerances:
        if not math.isfinite(tau) or tau <= 0: continue
        accept = difference <= Fraction(tau)
        row = dict(source_bits=bits(x), decoded_bits=bits(y), tau_bits=bits(tau), accept=accept)
        if accept:
            upper = nearest
            if Fraction(upper) < difference: upper = math.nextafter(upper, math.inf)
            row.update(max_bits=bits(upper), rms_bits=bits(nearest))
        entries.append(row)
path = Path(sys.argv[1]); path.parent.mkdir(parents=True, exist_ok=True)
path.write_text(json.dumps(entries, separators=(',', ':')) + '\n', encoding='utf-8')
print(f'Independent Fraction oracle cases={len(entries)} path={path}')
