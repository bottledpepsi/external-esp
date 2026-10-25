package mcradar;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.PrintStream;
import java.nio.charset.Charset;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;

/** Tests of the agent logger: format, levels, one line per message, size cap + backup, repeat suppression, threads. */
public final class LogTest {
    private static int fails = 0, checks = 0;

    private static void check(boolean ok, String what) {
        checks++;
        if (!ok) { fails++; System.out.println("FAIL: " + what); }
    }

    private static List<String> lines(File f) throws Exception {
        List<String> l = new ArrayList<String>();
        if (!f.exists()) return l;
        for (String s : new String(Files.readAllBytes(f.toPath()), Charset.forName("UTF-8")).split("\n", -1)) if (!s.isEmpty()) l.add(s);
        return l;
    }

    private static boolean lineOk(String l) {
        return l.matches("\\d\\d:\\d\\d:\\d\\d\\.\\d\\d\\d \\[(DEBUG|INFO |WARN |ERROR)\\] \\[agent\\] \\S.*");
    }

    public static void main(String[] args) throws Exception {
        PrintStream realErr = System.err;
        System.setErr(new PrintStream(new ByteArrayOutputStream()));     // the logger also writes to stderr: keep the test output readable
        try { run(); } finally { System.setErr(realErr); }
        System.out.println(fails == 0 ? "ALL OK (" + checks + " checks)" : fails + " FAILURES of " + checks);
        System.exit(fails == 0 ? 0 : 1);
    }

    private static void run() throws Exception {
        File dir = Files.createTempDirectory("esp-logtest").toFile();

        // ---- pure formatting ----
        check(Log.format(Log.INFO, "hello", 9, 5, 7, 42).equals("09:05:07.042 [INFO ] [agent] hello"), "INFO line");
        check(Log.format(Log.WARN, "w", 23, 59, 59, 999).equals("23:59:59.999 [WARN ] [agent] w"), "WARN line");
        check(Log.format(Log.ERROR, "e", 0, 0, 0, 0).equals("00:00:00.000 [ERROR] [agent] e"), "ERROR line");
        check(Log.format(Log.DEBUG, "d", 0, 0, 0, 0).equals("00:00:00.000 [DEBUG] [agent] d"), "DEBUG line");
        check(Log.sanitize("a\nb\r\nc\td\u0001e\u007Ff").equals("a b  c d e f"), "control characters -> spaces");
        check(Log.sanitize("tail   \n").equals("tail") && Log.sanitize(null).equals("null"), "trailing blanks, null");
        String longMsg = new String(new char[10000]).replace('\0', 'x');
        String cut = Log.sanitize(longMsg);
        check(cut.length() == Log.MAX_MESSAGE_CHARS && cut.endsWith("..."), "long message is cut");
        check(Log.sanitize(new String(new char[Log.MAX_MESSAGE_CHARS]).replace('\0', 'y')).length() == Log.MAX_MESSAGE_CHARS, "exactly the limit is untouched");

        // ---- levels and file content ----
        File a = new File(dir, "a.log");
        Log.configure(a, 1 << 20, false);
        check(!Log.debugEnabled(), "default level hides DEBUG");
        Log.debug("debug is off");
        Log.info("info 1");
        Log.warn("warn two");
        Log.error("error");
        Log.info("forged\n12:00:00.000 [ERROR] [agent] not a real line");
        Log.configure(a, 1 << 20, true);                                  // verbose
        check(Log.debugEnabled(), "verbose shows DEBUG");
        Log.debug("now visible");
        Log.close();
        List<String> la = lines(a);
        check(la.size() == 5, "5 lines, got " + la.size());
        boolean all = true;
        for (String l : la) all &= lineOk(l);
        check(all, "every line has the common format");
        check(la.size() == 5 && la.get(0).contains("[INFO ] [agent] info 1") && la.get(1).contains("[WARN ] [agent] warn two") && la.get(2).contains("[ERROR] [agent] error"), "levels in the file");
        check(la.size() == 5 && la.get(3).contains("forged 12:00:00.000 [ERROR] [agent] not a real line"), "injection stays ONE line");
        check(la.size() == 5 && la.get(4).contains("[DEBUG] [agent] now visible") && !la.toString().contains("debug is off"), "DEBUG only after verbose");

        // ---- size cap: one backup, consecutive, no half lines ----
        File b = new File(dir, "b.log");
        Log.configure(b, 4096, false);
        for (int i = 0; i < 1200; i++) Log.info(String.format("line number %04d with some padding to make it longer ................", i));
        Log.close();
        File bb = Log.backupOf(b);
        check(bb.getName().equals("b.1.log") && Log.backupOf(new File(dir, "esp_agent.log")).getName().equals("esp_agent.1.log"), "backup naming");
        check(b.exists() && bb.exists() && b.length() < 4096 + 200 && bb.length() < 4096 + 200, "both files stay at the cap: " + b.length() + " / " + bb.length());
        List<String> cur = lines(b), old = lines(bb);
        check(!cur.isEmpty() && !old.isEmpty() && cur.get(cur.size() - 1).contains("number 1199"), "newest line is in the current file");
        int lastOld = Integer.parseInt(old.get(old.size() - 1).substring(old.get(old.size() - 1).indexOf("number ") + 7).substring(0, 4));
        int firstCur = Integer.parseInt(cur.get(0).substring(cur.get(0).indexOf("number ") + 7).substring(0, 4));
        check(firstCur == lastOld + 1, "files are consecutive");
        all = true;
        for (String l : cur) all &= lineOk(l);
        for (String l : old) all &= lineOk(l);
        check(all, "rotation never cuts a line");

        // ---- an oversized log from the last run is rotated at start-up; a small one is appended to ----
        File c = new File(dir, "c.log");
        FileOutputStream fo = new FileOutputStream(c);
        fo.write(new byte[50000]);
        fo.close();
        Log.configure(c, 4096, false);
        Log.info("fresh");
        Log.close();
        check(Log.backupOf(c).exists() && Log.backupOf(c).length() == 50000 && lines(c).size() == 1, "oversized old log rotated at start");
        File d = new File(dir, "d.log");
        Log.configure(d, 1 << 20, false);
        Log.info("first run");
        Log.close();
        Log.configure(d, 1 << 20, false);
        Log.info("second run");
        Log.close();
        check(lines(d).size() == 2 && !Log.backupOf(d).exists(), "small log is appended to");

        // ---- the same message over and over is written once, then summarised ----
        final long[] t = {1_000_000_000L};
        Log.clock = new java.util.function.LongSupplier() { public long getAsLong() { return t[0]; } };
        File e = new File(dir, "e.log");
        Log.configure(e, 1 << 20, false);
        for (int i = 0; i < 1000; i++) { Log.error("Error in tick: boom"); t[0] += 8_000_000L; }       // 8 s of 125 Hz failures
        Log.info("something else");
        Log.close();
        List<String> le = lines(e);
        check(le.size() == 5, "1000 identical errors over 8 s -> the message at 0 s and at 5 s, a summary after each run, then the other message (5 lines), got " + le.size() + ": " + le);
        check(le.get(0).contains("Error in tick: boom"), "first occurrence is written");
        boolean summary = false;
        for (String l : le) summary |= l.matches(".*\\[INFO \\] \\[agent\\] last message repeated \\d+ times");
        check(summary, "a summary line says how many were suppressed: " + le);
        check(le.get(le.size() - 1).contains("something else"), "a different message is written at once");
        Log.configure(e, 1 << 20, false);                                                               // same text after the window: written again
        Log.error("again");
        t[0] += Log.REPEAT_WINDOW_NS + 1;
        Log.error("again");
        Log.close();
        check(lines(e).size() >= 5, "after the window the same message is logged again");
        Log.clock = new java.util.function.LongSupplier() { public long getAsLong() { return System.nanoTime(); } };

        // ---- four threads: every line whole ----
        File f = new File(dir, "f.log");
        Log.configure(f, 64L << 20, false);
        Thread[] th = new Thread[4];
        for (int i = 0; i < 4; i++) {
            final int id = i;
            th[i] = new Thread(new Runnable() { public void run() { for (int k = 0; k < 800; k++) Log.info("thread " + id + " message " + k + " payload payload payload"); } });
            th[i].start();
        }
        for (Thread x : th) x.join();
        Log.close();
        List<String> lf = lines(f);
        int[] next = new int[4];
        boolean ok = lf.size() == 3200;
        for (String l : lf) {
            java.util.regex.Matcher m = java.util.regex.Pattern.compile("\\[agent\\] thread (\\d) message (\\d+) payload payload payload$").matcher(l);
            if (!m.find()) { ok = false; continue; }
            ok &= Integer.parseInt(m.group(2)) == next[Integer.parseInt(m.group(1))]++;
        }
        check(ok && next[0] == 800 && next[3] == 800, "4 threads x 800 lines, whole and in per-thread order (" + lf.size() + " lines)");

        // ---- unwritable location: no exception ----
        Log.configure(new File(dir, "no/such/dir/g.log"), 1 << 20, false);
        Log.info("console only");
        Log.error("still fine");
        Log.close();
        check(true, "unwritable path");
    }
}
