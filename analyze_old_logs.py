import re, os

log_dir = '/mnt/c/Users/vasis/Desktop/frc-openjdk-roborio-shenandoah/gc_logs'

PAUSE_RE = re.compile(r'\[(\d+\.\d+)s\]\[info\]\[gc\s*\]\s+GC\(\d+\) Pause.*?(\d+\.\d+)ms$')
UPTIME_RE = re.compile(r'\[(\d+\.\d+)s\]')

for fname in sorted(os.listdir(log_dir)):
    if not fname.endswith('.log'):
        continue
    path = os.path.join(log_dir, fname)
    pauses = []
    max_uptime = 0.0
    min_uptime = None
    with open(path) as f:
        for line in f:
            m = UPTIME_RE.search(line)
            if m:
                t = float(m.group(1))
                if t > max_uptime:
                    max_uptime = t
                if min_uptime is None:
                    min_uptime = t
            p = PAUSE_RE.search(line)
            if p:
                pauses.append((float(p.group(1)), float(p.group(2))))
    runtime_s = max_uptime - (min_uptime or 0)
    total_ms = sum(p for _, p in pauses)
    overhead = total_ms / runtime_s if runtime_s > 0 else 0
    usable = "USABLE" if runtime_s > 30 else "too short"
    print(f"{fname}: {len(pauses)} pauses, runtime={runtime_s:.1f}s, total={total_ms:.2f}ms, overhead={overhead:.3f}ms/s  [{usable}]")
