package mcradar;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.nio.charset.Charset;
import java.util.Calendar;
import java.util.function.LongSupplier;

/**
 * The agent's logger: stderr (it shows up in the game's own log, prefixed "[mcradar-agent]") plus esp_agent.log in the export folder.
 *
 * Same format as the overlay's esp_log.txt (src/Logger.h, docs/logging.md):
 *     HH:mm:ss.SSS [LEVEL] [agent] message          LEVEL = ERROR | WARN  | INFO  | DEBUG
 * Default level is INFO; the agent option verbose=1 (mcesp --verbose) adds DEBUG. One message is always ONE line (control characters become
 * spaces, long messages are cut). The file is capped at 1 MiB: when full it is renamed to esp_agent.1.log (replacing the old backup) and a
 * new one is started. The same message repeated within a few seconds is written once and then summarised ("last message repeated N times"),
 * so a failure that recurs at 120 Hz cannot fill the log.
 */
final class Log {
    static final long DEFAULT_MAX_BYTES = 1L << 20;
    static final int MAX_MESSAGE_CHARS = 4000;
    static final long REPEAT_WINDOW_NS = 5_000_000_000L;
    private static final Charset UTF8 = Charset.forName("UTF-8");

    static final int DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3;
    private static final String[] TAG = {"DEBUG", "INFO ", "WARN ", "ERROR"};

    private static File file;
    private static long maxBytes = DEFAULT_MAX_BYTES;
    private static long size;
    private static BufferedWriter out;
    private static int minLevel = INFO;

    // repeat suppression
    private static String lastKey;
    private static long lastNs;
    private static int suppressed;
    /** Time source, replaceable by tests. */
    static LongSupplier clock = new LongSupplier() { public long getAsLong() { return System.nanoTime(); } };

    private Log() { }

    /** Normal start-up: esp_agent.log in the export folder. */
    static void init(String exportDir, boolean verbose) {
        try {
            File dir = new File(exportDir);
            if (!dir.isDirectory()) dir.mkdirs();
            configure(new File(dir, "esp_agent.log"), DEFAULT_MAX_BYTES, verbose);
        } catch (Throwable ignored) { }
    }

    static void init(String exportDir) { init(exportDir, false); }

    /** Tests and tools: choose the file and the cap explicitly. */
    static synchronized void configure(File f, long cap, boolean verbose) {
        close();
        file = f;
        maxBytes = Math.max(256, cap);
        minLevel = verbose ? DEBUG : INFO;
        lastKey = null;
        suppressed = 0;
        try {
            size = f.length();
            if (size >= maxBytes) rotate();   // an oversized log from the last run
        } catch (Throwable ignored) { size = 0; }
    }

    static synchronized void close() {
        try { if (out != null) out.close(); } catch (IOException ignored) { }
        out = null;
    }

    static File backupOf(File f) {
        String n = f.getName();
        int dot = n.lastIndexOf('.');
        String b = dot < 0 ? n + ".1" : n.substring(0, dot) + ".1" + n.substring(dot);   // esp_agent.log -> esp_agent.1.log
        return new File(f.getParentFile(), b);
    }

    static boolean debugEnabled() { return minLevel <= DEBUG; }

    /** A second agent load (overlay restarted with or without --verbose) changes the level without touching the file. */
    static synchronized void setVerbose(boolean verbose) { minLevel = verbose ? DEBUG : INFO; }

    static void error(String msg) { write(ERROR, msg); }
    static void warn(String msg) { write(WARN, msg); }
    static void info(String msg) { write(INFO, msg); }
    static void debug(String msg) { write(DEBUG, msg); }

    /** One line: control characters -> ' ', cut at MAX_MESSAGE_CHARS, no trailing blanks. */
    static String sanitize(String m) {
        if (m == null) m = "null";
        if (m.length() > MAX_MESSAGE_CHARS) m = m.substring(0, MAX_MESSAGE_CHARS - 3) + "...";
        StringBuilder sb = new StringBuilder(m.length());
        for (int i = 0; i < m.length(); i++) {
            char c = m.charAt(i);
            sb.append(c < 0x20 || c == 0x7F ? ' ' : c);
        }
        int end = sb.length();
        while (end > 0 && sb.charAt(end - 1) == ' ') end--;
        return sb.substring(0, end);
    }

    /** The exact line written for a message (no I/O). */
    static String format(int level, String msg, int h, int mi, int s, int ms) {
        return String.format(java.util.Locale.ROOT, "%02d:%02d:%02d.%03d [%s] [agent] %s", h, mi, s, ms, TAG[level], sanitize(msg));
    }

    private static String line(int level, String msg) {
        Calendar c = Calendar.getInstance();
        return format(level, msg, c.get(Calendar.HOUR_OF_DAY), c.get(Calendar.MINUTE), c.get(Calendar.SECOND), c.get(Calendar.MILLISECOND));
    }

    private static synchronized void write(int level, String msg) {
        if (level < minLevel) return;
        String clean = sanitize(msg);
        String key = level + clean;
        long now = clock.getAsLong();
        if (key.equals(lastKey) && now - lastNs < REPEAT_WINDOW_NS) { suppressed++; return; }
        flushRepeats();
        lastKey = key;
        lastNs = now;
        emit(line(level, msg));
    }

    private static void flushRepeats() {
        if (suppressed > 0) {
            int n = suppressed;
            suppressed = 0;
            emit(line(INFO, "last message repeated " + n + " times"));
        }
    }

    private static void emit(String line) {
        System.err.println("[mcradar-agent] " + line);
        File f = file;
        if (f == null) return;
        try {
            if (out == null) out = new BufferedWriter(new OutputStreamWriter(new FileOutputStream(f, true), UTF8));
            out.write(line);
            out.write('\n');
            out.flush();                                   // a crash must not eat the last lines
            size += line.getBytes(UTF8).length + 1;
            if (size >= maxBytes) rotate();
        } catch (IOException ignored) { }
    }

    private static void rotate() {
        close();
        File f = file;
        if (f == null) return;
        File b = backupOf(f);
        //noinspection ResultOfMethodCallIgnored
        b.delete();
        if (!f.renameTo(b)) f.delete();                    // cannot rename (file locked?): at least do not grow without bound
        size = 0;
    }
}
