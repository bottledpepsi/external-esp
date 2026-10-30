package mcradar;

import java.io.InputStream;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Facts about the game that need no Minecraft class name: where the version can be read from (in this order)
 *   1. version.json, a resource of the game's own jar (vanilla, and most launchers keep it there): {"id":"1.21.1", ...}
 *   2. --version on the JVM command line (the vanilla launcher and most others pass it)
 * Returns "unknown" when neither exists (the overlay adds the window title as a third hint).
 */
final class GameInfo {
    private GameInfo() { }

    private static final Pattern ID = Pattern.compile("\"id\"\\s*:\\s*\"([^\"]{1,64})\"");

    static String minecraftVersion(ClassLoader loader) {
        String v = fromVersionJson(loader);
        if (v == null) v = fromCommandLine(System.getProperty("sun.java.command"));
        return v == null ? "unknown" : v;
    }

    static String fromVersionJson(ClassLoader loader) {
        for (ClassLoader l : new ClassLoader[] {loader, ClassLoader.getSystemClassLoader()}) {
            if (l == null) continue;
            InputStream in = null;
            try {
                in = l.getResourceAsStream("version.json");
                if (in == null) continue;
                byte[] buf = new byte[8192];
                int n = 0, r;
                while (n < buf.length && (r = in.read(buf, n, buf.length - n)) > 0) n += r;
                Matcher m = ID.matcher(new String(buf, 0, n, "UTF-8"));
                if (m.find()) return m.group(1);
            } catch (Throwable ignored) {
            } finally {
                if (in != null) try { in.close(); } catch (Throwable ignored) { }
            }
        }
        return null;
    }

    /** "--version 1.21.1" in the command line text; null if absent. Other arguments (access tokens!) are never looked at. */
    static String fromCommandLine(String cmd) {
        if (cmd == null) return null;
        String[] t = cmd.split("\\s+");
        for (int i = 0; i + 1 < t.length; i++)
            if (t[i].equals("--version") && t[i + 1].matches("[0-9A-Za-z._+-]{1,40}")) return t[i + 1];
        return null;
    }

    /** "a1b2c3d4 (284 KB, modified 2026-10-26)": a short SHA-256 of the agent jar this code was loaded from - which build is really in the game. */
    static String agentBuild() {
        try {
            java.security.CodeSource cs = GameInfo.class.getProtectionDomain().getCodeSource();
            if (cs == null) return "unknown (no code source)";
            java.io.File f = new java.io.File(cs.getLocation().toURI());
            if (!f.isFile()) return "classes directory";
            java.security.MessageDigest md = java.security.MessageDigest.getInstance("SHA-256");
            java.io.InputStream in = new java.io.FileInputStream(f);
            try { byte[] b = new byte[16384]; int r; while ((r = in.read(b)) > 0) md.update(b, 0, r); } finally { in.close(); }
            StringBuilder h = new StringBuilder();
            for (int i = 0; i < 4; i++) h.append(String.format("%02x", md.digest()[i] & 0xff));
            return h + " (" + f.length() / 1024 + " KB, modified " + new java.text.SimpleDateFormat("yyyy-MM-dd").format(new java.util.Date(f.lastModified())) + ")";
        } catch (Throwable t) { return "unknown"; }
    }

    static String java() {
        return System.getProperty("java.version") + " (" + System.getProperty("java.vm.name") + ", " + System.getProperty("java.vendor") + ")";
    }
}
