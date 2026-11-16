# int16check, step 4: compares the recorded ranges (int16_ranges.txt) with
# what a 16-bit int holds. Exit code 1 if any expression left that range.
import sys
sites = {}
for l in open(sys.argv[1]):
    i, loc, k, why, expr = l.rstrip('\n').split('\t')
    sites[int(i)] = (loc, k, why, expr)
bad, seen = [], set()
for l in open(sys.argv[2]):
    i, k, a, b = l.split()
    i, a, b = int(i), int(a), int(b)
    seen.add(i)
    if (k == 'S' and (a < -32768 or b > 32767)) or (k == 'U' and b > 65535):
        bad.append((i, a, b))
print("int16check: %d expressions computed in int and then compared, shifted, divided or widened" % len(sites))
print("int16check: %d reached by the game tests, %d left the 16-bit range" % (len(seen), len(bad)))
for i, a, b in bad:
    print("  OUT OF RANGE %s: %s, values %d .. %d: %s" % (sites[i][0], sites[i][2], a, b, sites[i][3]))
never = [i for i in sites if i not in seen]
if never:
    print("int16check: not reached by the tests (check by hand):")
    for i in never:
        print("  %s: %s: %s" % (sites[i][0], sites[i][2], sites[i][3]))
sys.exit(1 if bad else 0)
