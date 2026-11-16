"""Write independent exact-real binary64 verifier cases using Python Fraction.

The sole command-line argument is a fresh JSON output path. Each owned row
records source/decoded/tolerance bit words, the exact-real bound decision, and
accepted upward maximum / nearest single-value RMS results. Consumed by the
C++ fixed-accuracy checks; generation is offline and does not modify fixtures.
"""
import json, math, random, struct, sys
from fractions import Fraction
from pathlib import Path

def value(bits):
    """Return the binary64 value with unsigned 64-bit pattern ``bits``.

    The integer is an input bit pattern, not a numeric value to round.
    """
    # Pack the integer word, then reinterpret those exact little-endian bytes.
    return struct.unpack('<d', struct.pack('<Q', bits))[0]
def bits(x):
    """Return the sixteen-digit hexadecimal binary64 representation of input x.

    Signed zero and every stored bit are retained; the returned text is owned.
    """
    # Reinterpret the double bytes as an unsigned word and format fixed width.
    return f'{struct.unpack("<Q", struct.pack("<d", x))[0]:016x}'
# Seeded local generator making the adversarial corpus reproducible.
rng = random.Random(20261108)
# Owned output rows, independent of C++ verifier implementation.
entries = []
# Exact boundary values spanning zeros, subnormals, ULP ties and finite extremes.
edge = [0., -0., value(1), value(2), value((1 << 52) - 1), value(1 << 52),
        1., math.nextafter(1., 2.), -1., -2**-53, value(0x7fefffffffffffff),
        -value(0x7fefffffffffffff), math.ldexp(1., 843)]
# Cartesian edge pairs; x/y are source/decoded values.
pairs = [(x, y) for x in edge for y in edge]
# Add deterministic finite random pairs; the loop ordinal is intentionally unused.
for _ in range(1400):
    # Source bit pattern masked to exclude NaN/infinity encodings.
    x = value(rng.getrandbits(64) & 0xffefffffffffffff)
    # Decoded bit pattern, independently generated and finite.
    y = value(rng.getrandbits(64) & 0xffefffffffffffff)
    pairs.append((x, y))
# For each source/decoded pair, construct a mathematically exact error.
for x, y in pairs:
    # Exact nonnegative real difference without floating subtraction rounding.
    difference = abs(Fraction(x) - Fraction(y))
    # Nearest binary64 error, with overflow represented explicitly as infinity.
    try: nearest = float(difference)
    except OverflowError: nearest = math.inf
    # Baseline positive tolerances spanning the representable range.
    tolerances = [1., value(1), math.ldexp(1., 843), value(0x7fefffffffffffff)]
    if math.isfinite(nearest) and nearest > 0:
        tolerances += [nearest, math.nextafter(nearest, 0.), math.nextafter(nearest, math.inf)]
    # Check each tolerance, including neighbors around a representable error boundary.
    for tau in tolerances:
        if not math.isfinite(tau) or tau <= 0: continue
        # Exact-real comparison result used as the independent oracle.
        accept = difference <= Fraction(tau)
        # Owned case containing raw bit patterns and admission verdict.
        row = dict(source_bits=bits(x), decoded_bits=bits(y), tau_bits=bits(tau), accept=accept)
        if accept:
            # Reported maximum rounded upward only when nearest rounds below the exact error.
            upper = nearest
            if Fraction(upper) < difference: upper = math.nextafter(upper, math.inf)
            row.update(max_bits=bits(upper), rms_bits=bits(nearest))
        entries.append(row)
# Caller-selected output path; parent creation does not overwrite corpus inputs.
path = Path(sys.argv[1]); path.parent.mkdir(parents=True, exist_ok=True)
# Serialize compact deterministic case data; source comments do not regenerate it.
path.write_text(json.dumps(entries, separators=(',', ':')) + '\n', encoding='utf-8')
print(f'Independent Fraction oracle cases={len(entries)} path={path}')
