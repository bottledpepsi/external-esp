package mcradar;

import java.io.File;

/**
 * Command-line self-test of a running Minecraft, without the overlay:
 *   java --add-modules jdk.attach -cp esp-agent.jar mcradar.SelfTest <pid> [seconds (default 20)]
 * Attaches the agent (or, if it is already loaded - by the overlay, say - tells it to run the self-test; its port and token are not touched), waits for the
 * report, prints it and exits with its EXIT code (SelfTestRun): 0 all passed, 1 passed with skips, 2 warnings, 3 failures, 10 not a Minecraft JVM, 11 not in a
 * world, 14 attach failed, 15 no report in time.
 */
public final class SelfTest {
    private SelfTest() { }

    public static void main(String[] a) throws Exception {
        if (a.length < 1) { System.err.println("usage: java --add-modules jdk.attach -cp esp-agent.jar mcradar.SelfTest <pid> [seconds]"); System.exit(2); }
        int secs = a.length > 1 ? Math.max(1, Math.min(600, Integer.parseInt(a[1]))) : 20;
        File jar = new File(SelfTest.class.getProtectionDomain().getCodeSource().getLocation().toURI());
        File out = new File(System.getProperty("java.io.tmpdir"), "esp-selftest-" + a[0] + "-" + System.nanoTime() + ".txt");
        String tmp = System.getProperty("java.io.tmpdir").replace('\\', '/');
        String opts = "selftest=" + secs + ";selftestout=" + out.getPath().replace('\\', '/') + ";export=" + tmp;     // port / hz / token are NOT passed: a running agent keeps its own
        int rc = Attach.load(a[0], jar.getPath(), opts, System.err, System.err);
        if (rc != 0) System.exit(14);
        System.err.println("self-test running for " + secs + " s: look around and drop an item meanwhile ...");
        long end = System.currentTimeMillis() + (secs + 20) * 1000L;
        while (System.currentTimeMillis() < end && !out.exists()) Thread.sleep(200);
        if (!out.exists()) { System.err.println("no report arrived: the agent loaded but did not finish (is the game paused or frozen?)"); System.exit(15); }
        Thread.sleep(200);
        String report = new String(java.nio.file.Files.readAllBytes(out.toPath()), "UTF-8");
        System.out.print(report);
        out.delete();
        java.util.regex.Matcher m = java.util.regex.Pattern.compile("(?m)^EXIT (\\d+)$").matcher(report);
        System.exit(m.find() ? Integer.parseInt(m.group(1)) : 15);
    }
}
