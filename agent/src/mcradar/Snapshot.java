package mcradar;

import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/** One frame of data, plus its wire encoding (plain text, one UDP datagram). */
final class Snapshot {
    static final class Rec {
        final String name; final double x, y, z; final float yaw, pitch, health, w, h; final String uuid;
        Rec(String name, double x, double y, double z, float yaw, float pitch, float health, float w, float h, String uuid) {
            this.name = name; this.x = x; this.y = y; this.z = z;
            this.yaw = yaw; this.pitch = pitch; this.health = health; this.w = w; this.h = h; this.uuid = uuid;
        }
    }

    /** A dropped item entity. `ench` = "id@level,id@level"; `tags` = the stack's tag/component text (may be long). */
    static final class Item {
        final double x, y, z; final float w, h; final int count; final String key, ench, tags;
        Item(double x, double y, double z, float w, float h, int count, String key, String ench, String tags) {
            this.x = x; this.y = y; this.z = z; this.w = w; this.h = h; this.count = count;
            this.key = key; this.ench = ench; this.tags = tags;
        }
    }

    /** The game's camera. mode: 1 = first person, 2 = third person (behind), 3 = third person (front). fov NaN = unknown. */
    static final class Cam {
        final double x, y, z; final float yaw, pitch, fov; final int mode;
        Cam(double x, double y, double z, float yaw, float pitch, float fov, int mode) {
            this.x = x; this.y = y; this.z = z; this.yaw = yaw; this.pitch = pitch; this.fov = fov; this.mode = mode;
        }
    }

    String status = "ok";
    long sampleNs;                 // System.nanoTime() taken just BEFORE the game state was read (the sample instant)
    boolean hasSampleTime;
    boolean itemsFresh = true;     // false: the item list is left out (the overlay keeps the last one); sent at ~10 Hz
    Rec local;
    Cam camera;
    final List<Rec> players = new ArrayList<Rec>();
    final List<Item> items = new ArrayList<Item>();

    static final int WIRE_VERSION = 5;      // the newest protocol this encoder writes (v5 = v4 + the K session-token line)
    static final int WIRE_MIN = 4;          // the oldest it can still write
    static final int MAX_BYTES = 56000;

    /** Protocol version written in the header (the agent sets the negotiated one; the default is v4 so that tests and older callers are unchanged). */
    int wire = 4;
    /** Session token written as the K line when wire >= 5 (null = none). */
    String token;
    /** Wire v5 Q line: this agent run's random id and the running datagram number (1, 2, ...; 0 = not numbered, no Q line). */
    int session;
    long seq;
     // stay well under the 64 KB UDP datagram limit

    /**
     * Wire format v4 (UTF-8), one UDP datagram per snapshot. Free text is percent-encoded (no spaces).
     * v4 = v3 + the T and F lines; the C++ side still reads v1..v3.
     *   MCRADAR 4
     *   T <nanoTime>                                                        sample instant (agent's System.nanoTime)
     *   S <status text>
     *   C <x> <y> <z> <yaw> <pitch> <fov|-> <mode>                          camera (optional)
     *   L <name> <x> <y> <z> <yaw|-> <pitch|-> <health|-> <w|-> <h|-> <uuid|->   local player (optional)
     *   P <name> ...same columns...                                         one line per OTHER player
     *   F 1                                                                 marks that the item list follows (possibly empty);
     *                                                                       snapshots without it keep the previous items
     *   I <x> <y> <z> <w|-> <h|-> <count> <key> <ench|-> <tags|->           one line per dropped item
     * ("-" = unknown.) Angles in degrees, Minecraft convention: yaw 0 = +Z (south), pitch +90 = straight down.
     */
    byte[] encode() {
        StringBuilder sb = new StringBuilder(512);
        sb.append("MCRADAR ").append(wire).append('\n');
        if (wire >= 5 && token != null && !token.isEmpty()) sb.append("K ").append(token).append('\n');   // session token: the overlay drops datagrams without it
        if (wire >= 5 && seq > 0) sb.append("Q ").append(String.format(Locale.ROOT, "%08x", session)).append(' ').append(seq & 0xFFFFFFFFL).append('\n');   // loss / reorder accounting
        if (hasSampleTime) sb.append("T ").append(sampleNs).append('\n');
        sb.append("S ").append(status.replace('\n', ' ')).append('\n');
        if (camera != null) {
            Cam c = camera;
            sb.append("C ").append(String.format(Locale.ROOT, "%.4f %.4f %.4f ", c.x, c.y, c.z))
              .append(num(c.yaw)).append(' ').append(num(c.pitch)).append(' ').append(num(c.fov)).append(' ').append(c.mode).append('\n');
        }
        if (local != null) line(sb, 'L', local);
        for (Rec r : players) line(sb, 'P', r);
        if (itemsFresh) sb.append("F 1\n");
        for (Item it : items) {
            StringBuilder one = new StringBuilder(160);
            one.append("I ").append(String.format(Locale.ROOT, "%.3f %.3f %.3f ", it.x, it.y, it.z))
               .append(num(it.w)).append(' ').append(num(it.h)).append(' ').append(it.count).append(' ')
               .append(pct(it.key)).append(' ').append(pct(it.ench)).append(' ').append(pct(it.tags)).append('\n');
            if (sb.length() + one.length() > MAX_BYTES) break;     // nearest items were added first
            sb.append(one);
        }
        return sb.toString().getBytes(Charset.forName("UTF-8"));
    }

    private static void line(StringBuilder sb, char tag, Rec r) {
        String n = (r.name == null || r.name.isEmpty()) ? "?" : r.name.replaceAll("\\s", "_");
        sb.append(tag).append(' ').append(n).append(' ')
          .append(String.format(Locale.ROOT, "%.3f %.3f %.3f ", r.x, r.y, r.z))
          .append(num(r.yaw)).append(' ').append(num(r.pitch)).append(' ').append(num(r.health)).append(' ')
          .append(num(r.w)).append(' ').append(num(r.h)).append(' ')
          .append(r.uuid == null || r.uuid.isEmpty() ? "-" : r.uuid).append('\n');
    }

    private static String num(float v) {
        return Float.isNaN(v) || Float.isInfinite(v) ? "-" : String.format(Locale.ROOT, "%.2f", v);
    }

    /** Percent-encode everything outside a small safe set (spaces, newlines, unicode -> %XX of UTF-8 bytes). "-" for empty. */
    static String pct(String s) {
        if (s == null || s.isEmpty()) return "-";
        StringBuilder o = new StringBuilder(s.length() + 8);
        for (byte b : s.getBytes(Charset.forName("UTF-8"))) {
            int c = b & 0xFF;
            boolean safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                    || c == '.' || c == '_' || c == ':' || c == ',' || c == '@' || c == '/';
            if (safe) o.append((char) c); else o.append('%').append(String.format("%02X", c));
        }
        return o.toString();
    }
}
