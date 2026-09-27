"""Parse metro_eng beat logs and compute timing statistics.

Usage: python analyze_precision.py <logfile> [expected_period_us]
Extracts ts= microseconds from beat log lines, computes intervals,
mean error vs expected period, max jitter, cumulative drift.
"""
import re
import sys

path = sys.argv[1]
expected = int(sys.argv[2]) if len(sys.argv) > 2 else 500000  # 120 BPM

text = open(path, "rb").read().decode("utf-8", "replace")
# 只匹配引擎拍点行的 ts=（避免误匹配 "beats=4" 等字段）
ts_list = [int(m) for m in re.findall(r"beat=\d+ acc=\d+ mute=\d+ ts=(\d+)", text)]

if len(ts_list) < 10:
    print(f"not enough samples: {len(ts_list)}")
    sys.exit(1)

# 去掉开机第一拍（start 立即发拍，与第二个拍间隔同周期但起点特殊，保留亦可；先全量统计）
ivals = [b - a for a, b in zip(ts_list, ts_list[1:])]
n = len(ivals)
mean = sum(ivals) / n
err = [i - expected for i in ivals]
abs_err = [abs(e) for e in err]
max_abs = max(abs_err)
min_i, max_i = min(ivals), max(ivals)

# 累积漂移：实测总时长与理论总时长之差
total_us = ts_list[-1] - ts_list[0]
theory_us = expected * (len(ts_list) - 1)
drift_us = total_us - theory_us

# 相邻主拍误差标准差
var = sum(e * e for e in err) / n
std = var ** 0.5

print(f"samples:        {len(ts_list)} beats ({n} intervals)")
print(f"expected:       {expected} us")
print(f"mean interval:  {mean:.1f} us (err {mean - expected:+.1f} us)")
print(f"std dev:        {std:.1f} us")
print(f"min/max:        {min_i} / {max_i} us")
print(f"max |err|:      {max_abs} us")
print(f"total span:     {total_us} us over {theory_us} us theory")
print(f"cumulative drift: {drift_us:+d} us over {total_us / 1e6:.1f} s "
      f"({drift_us / (len(ts_list) - 1):+.3f} us/beat)")
