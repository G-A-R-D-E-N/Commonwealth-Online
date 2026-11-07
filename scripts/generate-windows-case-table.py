import ctypes
import locale
import sys

if sys.platform != "win32" or sys.getwindowsversion().build != 26200 or locale.getlocale()[0] != "English_United States":
    raise SystemExit("Requires Windows build 26200 with the en-US locale")

kernel = ctypes.WinDLL("kernel32", use_last_error=True)
name = ctypes.create_unicode_buffer(85)
if not kernel.GetUserDefaultLocaleName(name, len(name)) or name.value != "en-US" or kernel.GetThreadLocale() != 0x0409:
    raise SystemExit("Requires the en-US Windows user and thread locales")

lower = ctypes.WinDLL("user32", use_last_error=True).CharLowerBuffW
lower.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
lower.restype = ctypes.c_uint32
pairs = []
for code in range(0x10000):
    if 0xD800 <= code <= 0xDFFF:
        continue
    buffer = (ctypes.c_uint16 * 1)(code)
    if lower(buffer, 1) != 1:
        raise SystemExit("CharLowerBuffW failed")
    if buffer[0] != code:
        pairs.append((code, buffer[0]))

for start in range(0x10000, 0x110000, 4096):
    end = min(start + 4096, 0x110000)
    units = []
    for code in range(start, end):
        offset = code - 0x10000
        units.extend((0xD800 + (offset >> 10), 0xDC00 + (offset & 1023)))
    buffer = (ctypes.c_uint16 * len(units))(*units)
    if lower(buffer, len(units)) != len(units):
        raise SystemExit("CharLowerBuffW failed")
    for index, code in enumerate(range(start, end)):
        high, low = buffer[index * 2], buffer[index * 2 + 1]
        if not (0xD800 <= high <= 0xDBFF and 0xDC00 <= low <= 0xDFFF):
            raise SystemExit("CharLowerBuffW returned an invalid surrogate pair")
        result = 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00)
        if result != code:
            pairs.append((code, result))

index = 0
ranges = []
while index < len(pairs):
    first, result = pairs[index]
    delta = result - first
    best, stride = 1, 1
    for step in (1, 2):
        count = 1
        while (index + count < len(pairs) and pairs[index + count][0] == first + count * step
               and pairs[index + count][1] - pairs[index + count][0] == delta):
            count += 1
        if count > best:
            best, stride = count, step
    last = pairs[index + best - 1][0]
    ranges.append((first, last, stride, delta))
    index += best

expanded = [(code, code + delta) for first, last, stride, delta in ranges for code in range(first, last + 1, stride)]
mapping = dict(pairs)
if expanded != pairs or any(mapping.get(result, result) != result for _, result in pairs):
    raise SystemExit("Compressed mapping is not exact and idempotent")
for first, last, stride, delta in ranges:
    print(f"\tRange{{ 0x{first:X}, 0x{last:X}, {stride}, {delta} }},")
