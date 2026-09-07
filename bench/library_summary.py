import collections
import csv
import statistics
import sys

groups = collections.defaultdict(lambda: collections.defaultdict(list))
for row in csv.DictReader(open(sys.argv[1])):
    groups[(row['variant'], row['reuse'])][row['file']].append(row)
for (variant, reuse), files in groups.items():
    minima = [min(int(r['wall_ns']) for r in runs) / 1000 for runs in files.values()]
    all_runs = [int(r['wall_ns']) / 1000 for runs in files.values() for r in runs]
    total_bytes = sum(int(runs[0]['bytes']) for runs in files.values())
    print(variant, 'reuse', reuse, 'files', len(files),
          'mean minima %.3f us p50 %.3f us all mean %.3f us %.1f MB/s' %
          (statistics.mean(minima), statistics.median(minima), statistics.mean(all_runs), total_bytes / sum(minima)))
