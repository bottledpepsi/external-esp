package mcradar;

import java.util.Arrays;
import java.util.Locale;

/** Achieved intervals between consecutive ticks: a bounded ring (the last 32768), summarised as mean / percentiles / max and the share of late ticks. */
final class PacingStats {
    private final long[] ring = new long[32768];
    private int n, pos;
    private long last, first, total;
    private long count;

    /** Call once per tick with System.nanoTime() of the tick's sample instant. */
    synchronized void tick(long nowNs) {
        if (last != 0) {
            long d = nowNs - last;
            ring[pos] = d; pos = (pos + 1) % ring.length; if (n < ring.length) n++;
            total += d; count++;
        } else first = nowNs;
        last = nowNs;
    }

    static final class Summary {
        long samples; double seconds, meanMs, p50Ms, p95Ms, p99Ms, maxMs, rateHz, lateShare, veryLateShare;
        public String toString() {
            return String.format(Locale.ROOT, "samples %d over %.1f s | interval ms: mean %.2f p50 %.2f p95 %.2f p99 %.2f max %.2f | %.1f Hz | late(>1.5x) %.1f%% very late(>3x) %.1f%%",
                    samples, seconds, meanMs, p50Ms, p95Ms, p99Ms, maxMs, rateHz, lateShare * 100, veryLateShare * 100);
        }
    }

    /** `periodNs` is the configured interval, for the late shares. */
    synchronized Summary summary(long periodNs) {
        Summary s = new Summary();
        s.samples = n;
        if (n == 0) return s;
        long[] a = Arrays.copyOf(ring, n);
        Arrays.sort(a);
        long sum = 0;
        int late = 0, very = 0;
        for (long v : a) { sum += v; if (v > periodNs * 3 / 2) late++; if (v > periodNs * 3) very++; }
        s.meanMs = sum / (double) n / 1e6;
        s.p50Ms = pct(a, 0.50); s.p95Ms = pct(a, 0.95); s.p99Ms = pct(a, 0.99);
        s.maxMs = a[n - 1] / 1e6;
        s.seconds = sum / 1e9;
        s.rateHz = s.meanMs > 0 ? 1000.0 / s.meanMs : 0;
        s.lateShare = late / (double) n; s.veryLateShare = very / (double) n;
        return s;
    }

    /** Nearest-rank percentile of a sorted array, in ms. */
    static double pct(long[] sorted, double q) {
        if (sorted.length == 0) return 0;
        int i = (int) Math.ceil(q * sorted.length) - 1;
        return sorted[Math.max(0, Math.min(sorted.length - 1, i))] / 1e6;
    }
}
