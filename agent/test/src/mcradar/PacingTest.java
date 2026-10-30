package mcradar;

import java.io.File;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.nio.charset.Charset;
import java.util.Arrays;

/**
 * Pacing instrumentation (Linux numbers here only prove the plumbing; Windows behaviour is what docs/pacing.md tells the user to measure):
 * percentile / late-share maths on known data, Pacer semantics (hybrid never returns early; sleep is within the 1 ms floor), the agent's in-game
 * report file (agent option pacing=N) and a short PacingBench case.
 */
public final class PacingTest {
    private static int fails = 0, checks = 0;
    private static void check(boolean ok, String what) { checks++; if (!ok) { fails++; System.out.println("FAIL: " + what); } }
    private static boolean near(double a, double b, double tol) { return Math.abs(a - b) <= tol; }

    public static void main(String[] args) throws Exception {
        // ---- statistics on known data: intervals 1..100 ms
        PacingStats st = new PacingStats();
        long t = 1_000_000_000L;
        st.tick(t);
        for (int ms = 1; ms <= 100; ms++) { t += ms * 1_000_000L; st.tick(t); }
        PacingStats.Summary s = st.summary(20_000_000L);                  // period 20 ms: late = > 30 ms, very late = > 60 ms
        check(s.samples == 100 && near(s.meanMs, 50.5, 1e-9), "mean of 1..100 ms is 50.5, got " + s.meanMs);
        check(near(s.p50Ms, 50, 1e-9) && near(s.p95Ms, 95, 1e-9) && near(s.p99Ms, 99, 1e-9) && near(s.maxMs, 100, 1e-9), "nearest-rank p50/p95/p99/max = 50/95/99/100, got " + s.p50Ms + "/" + s.p95Ms + "/" + s.p99Ms + "/" + s.maxMs);
        check(near(s.lateShare, 0.70, 1e-9) && near(s.veryLateShare, 0.40, 1e-9), "late 70% (31..100) and very late 40% (61..100), got " + s.lateShare + "/" + s.veryLateShare);
        check(new PacingStats().summary(1).samples == 0, "an empty ring summarises to zero samples");
        PacingStats ring = new PacingStats();
        ring.tick(1);
        for (int i = 0; i < 40000; i++) ring.tick(2 + i * 1000L);        // more ticks than the ring holds
        check(ring.summary(1000).samples == 32768, "the ring keeps the last 32768 intervals");

        // ---- Pacer: absolute schedule at 100 Hz for 60 ticks
        for (boolean hybrid : new boolean[] {false, true}) {
            Pacer p = new Pacer(hybrid);
            long period = 10_000_000L, next = System.nanoTime();
            double[] over = new double[60];
            for (int i = 0; i < over.length; i++) {
                next += period;
                p.waitUntil(next);
                over[i] = (System.nanoTime() - next) / 1e6;
            }
            Arrays.sort(over);
            double min = over[0], median = over[over.length / 2];
            if (hybrid) check(min >= 0, "hybrid pacer never returns before the target (earliest " + min + " ms)");
            else check(min > -1.0, "sleep pacer is early by less than the 1 ms floor (earliest " + min + " ms)");
            check(median < (hybrid ? 1.0 : 3.0), (hybrid ? "hybrid" : "sleep") + " pacer median overshoot " + median + " ms is small");
        }
        check(!Pacer.of("sleep").hybrid && Pacer.of("hybrid").hybrid && !Pacer.of("nonsense").hybrid, "Pacer.of: only 'hybrid' selects hybrid");

        // ---- the agent's own report (in-process, fake Instrumentation, no game)
        File dir = new File(System.getProperty("java.io.tmpdir"), "esp-pacing-test-" + System.nanoTime()); dir.mkdirs();
        DatagramSocket sink = new DatagramSocket(0, InetAddress.getByName("127.0.0.1"));
        String ex = dir.getPath().replace('\\', '/');
        Agent.agentmain("port=" + sink.getLocalPort() + ";hz=100;pacing=1;export=" + ex, AgentReloadTest.fakeInst());
        File report = new File(dir, "esp_pacing.txt");
        for (int i = 0; i < 40 && !report.exists(); i++) Thread.sleep(100);
        check(report.exists(), "pacing=1 writes esp_pacing.txt after one second");
        if (report.exists()) {
            String txt = new String(java.nio.file.Files.readAllBytes(report.toPath()), Charset.forName("UTF-8"));
            check(txt.contains("configured: 100 Hz") && txt.contains("pacer sleep") && txt.contains("p99") && txt.contains("late(>1.5x)"), "report has the configuration and the percentiles:\n" + txt);
            java.util.regex.Matcher m = java.util.regex.Pattern.compile("samples (\\d+)").matcher(txt);
            check(m.find() && Integer.parseInt(m.group(1)) >= 60 && Integer.parseInt(m.group(1)) <= 130, "about 100 samples in the first second: " + (m.find(0) ? m.group(1) : "none"));
        }
        // reload with the hybrid pacer: logged, and the worker keeps going
        Agent.agentmain("port=" + sink.getLocalPort() + ";hz=100;pacer=hybrid;export=" + ex, AgentReloadTest.fakeInst());
        Thread.sleep(500);
        String log = new String(java.nio.file.Files.readAllBytes(new File(dir, "esp_agent.log").toPath()), Charset.forName("UTF-8"));
        check(log.contains("pacer sleep -> hybrid") && log.contains("Pacer: hybrid"), "reload to the hybrid pacer is logged:\n" + log);

        // ---- one short bench case per primitive
        String sleepRow = PacingBench.runCase("sleep", 120, 0.4), spinRow = PacingBench.runCase("spin", 120, 0.4), parkRow = PacingBench.runCase("park", 120, 0.4), hybRow = PacingBench.runCase("hybrid", 120, 0.4);
        check(sleepRow.contains("120 Hz") && parkRow.contains("park") && hybRow.contains("hybrid"), "bench rows are labelled: " + sleepRow);
        check(cpu(spinRow) > 50, "spin burns most of a core, row: " + spinRow);
        check(cpu(sleepRow) < 30, "sleep uses little CPU, row: " + sleepRow);
        System.out.println(fails == 0 ? "ALL OK (" + checks + " checks)" : fails + " FAILURES of " + checks);
        System.exit(fails == 0 ? 0 : 1);
    }

    private static double cpu(String row) {
        java.util.regex.Matcher m = java.util.regex.Pattern.compile("thread CPU\\s+([0-9.]+)%").matcher(row);
        return m.find() ? Double.parseDouble(m.group(1)) : -1;
    }
}
