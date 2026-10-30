package mcradar;

import java.io.File;
import java.io.FileOutputStream;
import java.lang.management.ManagementFactory;
import java.lang.management.ThreadMXBean;
import java.util.Locale;
import java.util.concurrent.locks.LockSupport;

/**
 * Standalone measurement: how well can THIS JVM on THIS machine hit 60 / 120 / 240 Hz with each way of waiting?
 *   java -cp esp-agent.jar mcradar.PacingBench [seconds-per-case (default 3)] [output-file (default esp_pacing_bench.txt)]
 * Prints a table and writes the same text to the file. Run it on the machine that has the problem (Windows included) and send the file.
 * Caveat in the output: this runs in a fresh JVM where nothing else asked for a faster timer; inside Minecraft other threads may change the picture, which
 * is why the agent can also measure itself in the game (agent option pacing=SECONDS, docs/pacing.md).
 */
public final class PacingBench {
    private PacingBench() { }

    interface Wait { void until(long target) throws InterruptedException; }

    static String row(String prim, int hz, PacingStats.Summary s, double cpuPct) {
        return String.format(Locale.ROOT, "%-7s %3d Hz | mean %6.2f p50 %6.2f p95 %6.2f p99 %6.2f max %7.2f ms | late %5.1f%% very late %4.1f%% | thread CPU %5.1f%%",
                prim, hz, s.meanMs, s.p50Ms, s.p95Ms, s.p99Ms, s.maxMs, s.lateShare * 100, s.veryLateShare * 100, cpuPct);
    }

    static Wait waiter(String prim) {
        if (prim.equals("sleep")) return new Wait() { final Pacer p = new Pacer(false); public void until(long t) throws InterruptedException { p.waitUntil(t); } };
        if (prim.equals("hybrid")) return new Wait() { final Pacer p = new Pacer(true); public void until(long t) throws InterruptedException { p.waitUntil(t); } };
        if (prim.equals("park")) return new Wait() { public void until(long t) { long r; while ((r = t - System.nanoTime()) > 0) LockSupport.parkNanos(r); } };
        return new Wait() { public void until(long t) { while (t - System.nanoTime() > 0) Pacer.spinHint(); } };       // spin
    }

    /** Runs one case for `seconds`; returns the report row. Package-private so tests can run a short one. */
    static String runCase(String prim, int hz, double seconds) throws Exception {
        ThreadMXBean mx = ManagementFactory.getThreadMXBean();
        boolean cpu = mx.isCurrentThreadCpuTimeSupported();
        final long period = 1_000_000_000L / hz;
        PacingStats st = new PacingStats();
        Wait w = waiter(prim);
        long cpu0 = cpu ? mx.getCurrentThreadCpuTime() : 0, t0 = System.nanoTime(), next = t0;
        while (System.nanoTime() - t0 < seconds * 1e9) {
            st.tick(System.nanoTime());
            next += period;
            long now = System.nanoTime();
            if (next < now - 4 * period) next = now;                   // same resynchronisation rule as the agent's loop
            w.until(next);
        }
        double wall = System.nanoTime() - t0;
        double cpuPct = cpu ? (mx.getCurrentThreadCpuTime() - cpu0) / wall * 100 : Double.NaN;
        return row(prim, hz, st.summary(period), cpuPct);
    }

    public static void main(String[] args) throws Exception {
        double seconds = args.length > 0 ? Double.parseDouble(args[0]) : 3;
        File out = new File(args.length > 1 ? args[1] : "esp_pacing_bench.txt");
        StringBuilder b = new StringBuilder();
        b.append("esp-agent pacing bench\n");
        b.append("java ").append(GameInfo.java()).append("\nos ").append(System.getProperty("os.name")).append(' ').append(System.getProperty("os.version"))
                .append(' ').append(System.getProperty("os.arch")).append(", ").append(Runtime.getRuntime().availableProcessors()).append(" logical processors\n");
        b.append("seconds per case: ").append(seconds).append("\n");
        b.append("NOTE: a fresh JVM where nothing else asked for a faster timer. Late = interval over 1.5x the period. Lower thread CPU is better; spin shows the cost of precision.\n\n");
        for (int hz : new int[] {60, 120, 240}) {
            for (String prim : new String[] {"sleep", "park", "hybrid", "spin"}) {
                String r = runCase(prim, hz, seconds);
                b.append(r).append('\n');
                System.out.println(r);
            }
            b.append('\n');
        }
        FileOutputStream f = new FileOutputStream(out);
        try { f.write(b.toString().getBytes("UTF-8")); } finally { f.close(); }
        System.out.println("written: " + out.getAbsolutePath());
    }
}
