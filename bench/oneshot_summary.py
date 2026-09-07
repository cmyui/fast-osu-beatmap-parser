import csv
import collections
import statistics
import sys

rows = list(csv.DictReader(open(sys.argv[1])))
groups = collections.defaultdict(lambda: collections.defaultdict(list))
for row in rows:
    groups[row['variant']][row['file']].append(row)
for variant, files in groups.items():
    best = [min(v, key=lambda r: int(r['wall_ns'])) for v in files.values()]
    values = sorted(int(r['wall_ns']) / 1000 for r in best)
    all_values = [int(r['wall_ns']) / 1000 for v in files.values() for r in v]
    print(variant, 'files', len(files), 'best/file mean %.2f p50 %.2f p90 %.2f p99 %.2f us' %
          (statistics.mean(values), statistics.median(values), values[int(.9*len(values))], values[int(.99*len(values))]),
          'all runs mean %.2f median %.2f us' % (statistics.mean(all_values), statistics.median(all_values)),
          'minor faults %.1f' % statistics.mean(int(r['minor_faults']) for r in best))
