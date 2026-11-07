package mcradar;

import java.io.File;
import java.io.FileInputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.Map;

/**
 * The entity inspector's agent half. The overlay writes a one-line request into the control file (option ctl=<path>):
 *
 *     inspect <id>      the entity whose identity (E line id) is <id>
 *     inspect player    the agent's own discovery paths (container, local player, camera, items)
 *     inspect off
 *
 * and rewrites it every couple of seconds while it is inspecting (the file's modification time is the keep-alive: a request that is not renewed for
 * KEEPALIVE_MS lapses, so a crashed overlay does not leave the agent inspecting forever). While a request is live the agent sends the answer as X rows
 * with the item/entity list. Nothing here writes to the game, and no game object's toString() is ever called (that could run game code): only
 * primitives, Strings, enums and the size of JDK collections are shown.
 */
final class Inspector {
    static final long KEEPALIVE_MS = 6000;
    static final int MAX_FIELDS = 30, MAX_VALUE = 100, MAX_FILE = 512;
    static final int MODE_OFF = 0, MODE_ENTITY = 1, MODE_PLAYER = 2;

    private volatile File file;
    private long lastMod = Long.MIN_VALUE, lastLen = -1;
    private int mode = MODE_OFF, target;
    private long liveUntil;

    /** A control file path the agent will accept: absolute-looking, printable, short. */
    static boolean validPath(String p) {
        if (p == null || p.length() == 0 || p.length() > 260) return false;
        for (int i = 0; i < p.length(); i++) if (p.charAt(i) < 32 || p.charAt(i) == 127) return false;
        return new File(p).isAbsolute();
    }

    void setFile(String path) { file = path == null ? null : new File(path); lastMod = Long.MIN_VALUE; lastLen = -1; if (path == null) mode = MODE_OFF; }

    /** {mode, id} for a request text, or null when it is not one. Pure. */
    static int[] parse(String text) {
        if (text == null) return null;
        String t = text.trim();
        int nl = t.indexOf('\n');
        if (nl >= 0) t = t.substring(0, nl).trim();
        if (t.equals("inspect off")) return new int[] {MODE_OFF, 0};
        if (t.equals("inspect player")) return new int[] {MODE_PLAYER, 0};
        if (!t.startsWith("inspect ")) return null;
        String n = t.substring(8).trim();
        if (n.length() == 0 || n.length() > 10) return null;
        long v = 0;
        for (int i = 0; i < n.length(); i++) {
            char c = n.charAt(i);
            if (c < '0' || c > '9') return null;
            v = v * 10 + (c - '0');
        }
        if (v > 0xFFFFFFFFL) return null;
        return new int[] {MODE_ENTITY, (int) v};
    }

    /** Looks at the control file (cheap: a stat, and a read only when it changed) and returns the live request: MODE_*, with the id in target(). */
    int poll(long nowMs) {
        File f = file;
        if (f != null) {
            try {
                long mod = f.lastModified(), len = f.length();
                if ((mod != lastMod || len != lastLen) && len > 0 && len <= MAX_FILE) {
                    lastMod = mod; lastLen = len;
                    byte[] b = new byte[(int) len];
                    FileInputStream in = new FileInputStream(f);
                    try { int n = 0, r; while (n < b.length && (r = in.read(b, n, b.length - n)) > 0) n += r; } finally { in.close(); }
                    int[] req = parse(new String(b, "UTF-8"));
                    if (req != null) { mode = req[0]; target = req[1]; liveUntil = nowMs + KEEPALIVE_MS; }
                }
            } catch (Exception ignored) { }
        }
        if (mode != MODE_OFF && nowMs > liveUntil) mode = MODE_OFF;
        return mode;
    }
    int target() { return target; }

    // ---- what is shown ----

    private static String cut(String s) { return s.length() <= MAX_VALUE ? s : s.substring(0, MAX_VALUE - 1) + "~"; }
    private static String printable(String s) {
        StringBuilder b = new StringBuilder(s.length());
        for (int i = 0; i < s.length() && b.length() < MAX_VALUE; i++) { char c = s.charAt(i); b.append(c >= 32 && c < 127 ? c : '?'); }
        return b.toString();
    }

    /** One field's value as text, without running any game code. */
    static String valueOf(Field f, Object o) {
        Class<?> t = f.getType();
        try {
            if (t == int.class) return Integer.toString(f.getInt(o));
            if (t == long.class) return Long.toString(f.getLong(o));
            if (t == float.class) return Float.toString(f.getFloat(o));
            if (t == double.class) return Double.toString(f.getDouble(o));
            if (t == boolean.class) return Boolean.toString(f.getBoolean(o));
            if (t == byte.class) return Byte.toString(f.getByte(o));
            if (t == short.class) return Short.toString(f.getShort(o));
            if (t == char.class) return Integer.toString(f.getChar(o));
            Object v = f.get(o);
            if (v == null) return "null";
            if (v instanceof String) return "\"" + printable((String) v) + "\"";
            if (v instanceof Enum) return ((Enum<?>) v).name();
            if (v instanceof Number || v instanceof Boolean) return v.toString();   // boxed JDK values
            if (v.getClass().isArray()) return v.getClass().getComponentType().getSimpleName() + "[" + java.lang.reflect.Array.getLength(v) + "]";
            String cn = v.getClass().getName();
            if (cn.startsWith("java.") ) {
                if (v instanceof Collection) return v.getClass().getSimpleName() + " size " + ((Collection<?>) v).size();
                if (v instanceof Map) return v.getClass().getSimpleName() + " size " + ((Map<?, ?>) v).size();
            }
            return v.getClass().getSimpleName();                // a game object: its type, never its toString()
        } catch (Throwable e) {
            return "(unreadable)";
        }
    }

    /** The fields (instance, then inherited, nearest class first) of an entity as rows, at most MAX_FIELDS, in declaration order. */
    static void fieldRows(Object o, List<String[]> out) {
        int n = 0;
        for (Class<?> c = o.getClass(); c != null && c != Object.class && n < MAX_FIELDS; c = c.getSuperclass()) {
            Field[] fs;
            try { fs = c.getDeclaredFields(); } catch (Throwable e) { continue; }
            for (Field f : fs) {
                if (Modifier.isStatic(f.getModifiers())) continue;
                if (n >= MAX_FIELDS) break;
                try { f.setAccessible(true); } catch (Throwable e) { continue; }
                out.add(new String[] {printable(c.getSimpleName() + "." + f.getName() + " : " + f.getType().getSimpleName()), cut(valueOf(f, o))});
                n++;
            }
        }
    }

    static String kindName(int k) {
        switch (k) {
            case Snapshot.KIND_HOSTILE: return "hostile";
            case Snapshot.KIND_PASSIVE: return "passive";
            case Snapshot.KIND_NEUTRAL: return "neutral";
            case Snapshot.KIND_PROJECTILE: return "projectile";
            case Snapshot.KIND_XP: return "experience orb";
            default: return "other";
        }
    }

    /** The rows describing one entity: its class chain, category, how position / size / health were found and what they read now, then its fields. */
    static List<String[]> describeEntity(Object e, int id, Probe.Model m, int kind) {
        List<String[]> r = new ArrayList<String[]>();
        r.add(new String[] {"class", cut(printable(join(EntityKinds.chain(e.getClass()))))});
        r.add(new String[] {"category", kind < 0 ? "not one of the categories" : kindName(kind)});
        r.add(new String[] {"id", Long.toString(id & 0xFFFFFFFFL) + " (identity hash)"});
        if (m != null) {
            double[] p = new double[3];
            float[] sz = new float[2];
            r.add(new String[] {"position path", cut(printable(m.describePos()))});
            r.add(new String[] {"position", m.position(e, p) ? String.format(java.util.Locale.ROOT, "%.3f %.3f %.3f", p[0], p[1], p[2]) : "(unreadable)"});
            r.add(new String[] {"size path", cut(printable(m.describeSize()))});
            r.add(new String[] {"size", m.size(e, sz) ? String.format(java.util.Locale.ROOT, "%.2f x %.2f", sz[0], sz[1]) : "-"});
            r.add(new String[] {"health path", cut(printable(m.describeHealth()))});
            float hp = m.health(e);
            r.add(new String[] {"health", Float.isNaN(hp) ? "-" : String.format(java.util.Locale.ROOT, "%.1f", hp)});
        }
        fieldRows(e, r);
        return r;
    }

    private static String join(String[] a) {
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < a.length; i++) { if (i > 0) b.append(" > "); b.append(a[i]); }
        return b.toString();
    }

    /** A row with the entity not found (it was removed, or the id is stale). */
    static List<String[]> notFound(int id) {
        List<String[]> r = new ArrayList<String[]>();
        r.add(new String[] {"entity", "id " + (id & 0xFFFFFFFFL) + " is not in the agent's entity list (any more)"});
        return r;
    }
}
