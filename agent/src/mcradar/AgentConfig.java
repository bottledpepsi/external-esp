package mcradar;

import java.util.HashMap;
import java.util.Map;

/**
 * Agent options, passed as "key=value;key=value" (premain/agentmain argument string).
 *
 * The worker thread reads port / hz / wire / token / verbose on every tick while a second agentmain() call (the overlay was restarted and
 * attaches again) may replace them from another thread, so those fields are volatile and a reload goes through {@link #reloadFrom}.
 * exportDir and username are fixed for the life of the agent (the log file and the player search are built from them).
 */
final class AgentConfig {
    volatile int port = 47615; // UDP port of the C++ radar on 127.0.0.1
    volatile int hz = 20;      // send rate (1..240); the overlay passes hz=120 by default
    String exportDir = System.getProperty("java.io.tmpdir");
    String username;           // override for the local player's name (normally auto-detected)
    /**
     * The newest wire protocol the overlay says it can read (option wire=N). An overlay that does not pass it (an older build, or a
     * hand-written -javaagent line) is treated as speaking v4. The agent sends min(this, Snapshot.WIRE_VERSION).
     */
    volatile int wire = 4;
    /** Option verbose=1 (mcesp --verbose): log DEBUG lines too. */
    volatile boolean verbose;
    /** Option dump=1: write esp_structure.json (StructureDump). */
    volatile boolean dump;
    /** Option pacer=sleep|hybrid (how the worker waits, see Pacer) and pacing=SECONDS (write esp_pacing.txt after that long; 0 = never). */
    volatile boolean pacingReportDone;
    volatile String pacer = "sleep";
    volatile int pacingSeconds;
    /** Session token the overlay generated (option token=...): sent in every v5 datagram. Null = none (then only v4 can be spoken). */
    volatile String token;

    static AgentConfig parse(String args) { return parse(args, new HashMap<String, String>()); }

    /** Like {@link #parse(String)}; `kv` receives the raw key/value pairs, so a reload can tell "not given" from "given as the default". */
    static AgentConfig parse(String args, Map<String, String> kv) {
        AgentConfig c = new AgentConfig();
        if (args == null) return c;
        for (String part : args.split(";")) {
            int i = part.indexOf('=');
            if (i > 0) kv.put(part.substring(0, i).trim(), part.substring(i + 1).trim());
        }
        try { if (kv.containsKey("port")) c.port = Integer.parseInt(kv.get("port")); } catch (NumberFormatException ignored) { }
        try { if (kv.containsKey("hz")) c.hz = Math.max(1, Math.min(240, Integer.parseInt(kv.get("hz")))); } catch (NumberFormatException ignored) { }
        try { if (kv.containsKey("wire")) c.wire = Integer.parseInt(kv.get("wire")); } catch (NumberFormatException ignored) { }
        if (kv.get("pacer") != null) c.pacer = kv.get("pacer").equalsIgnoreCase("hybrid") ? "hybrid" : "sleep";
        try { if (kv.get("pacing") != null) c.pacingSeconds = Math.max(0, Math.min(3600, Integer.parseInt(kv.get("pacing")))); } catch (NumberFormatException ignored) { }
        c.dump = "1".equals(kv.get("dump")) || "true".equalsIgnoreCase(kv.get("dump"));
        c.verbose = "1".equals(kv.get("verbose")) || "true".equalsIgnoreCase(kv.get("verbose"));
        String tok = kv.get("token");
        if (tok != null && tok.matches("[0-9A-Za-z]{16,64}")) c.token = tok;
        if (kv.containsKey("export") && !kv.get("export").isEmpty()) c.exportDir = kv.get("export");
        if (kv.containsKey("user") && !kv.get("user").isEmpty()) c.username = kv.get("user");
        return c;
    }

    /**
     * Takes over the options of a second load (the overlay was restarted and attaches again, with a new session token and perhaps another
     * port or rate). Returns what changed as a short text for the log - never the token itself; "" when nothing changed.
     * A second load that does not mention an option leaves the running value alone (it is not reset to the default), except the token:
     * an overlay that no longer sends one means "no token".
     */
    String reloadFrom(AgentConfig n, java.util.Map<String, String> given) {
        StringBuilder sb = new StringBuilder();
        if (given.containsKey("port") && n.port != port) { sb.append("port ").append(port).append(" -> ").append(n.port).append("; "); port = n.port; }
        if (given.containsKey("hz") && n.hz != hz) { sb.append("hz ").append(hz).append(" -> ").append(n.hz).append("; "); hz = n.hz; }
        if (given.containsKey("wire") && n.wire != wire) { sb.append("wire ").append(wire).append(" -> ").append(n.wire).append("; "); wire = n.wire; }
        if (given.containsKey("verbose") && n.verbose != verbose) { sb.append("verbose ").append(verbose).append(" -> ").append(n.verbose).append("; "); verbose = n.verbose; }
        if (given.containsKey("dump") && n.dump != dump) { sb.append("dump ").append(dump).append(" -> ").append(n.dump).append("; "); dump = n.dump; }
        if (given.containsKey("pacer") && !n.pacer.equals(pacer)) { sb.append("pacer ").append(pacer).append(" -> ").append(n.pacer).append("; "); pacer = n.pacer; }
        if (given.containsKey("pacing") && n.pacingSeconds != pacingSeconds) { sb.append("pacing report after ").append(n.pacingSeconds).append(" s; "); pacingSeconds = n.pacingSeconds; pacingReportDone = false; }
        final String old = token;
        if (old == null ? n.token != null : !old.equals(n.token)) { sb.append(n.token == null ? "session token removed; " : "session token replaced; "); token = n.token; }
        if (given.containsKey("export") && !n.exportDir.equals(exportDir)) sb.append("export folder change ignored (fixed while the agent runs); ");
        if (given.containsKey("user") && n.username != null && !n.username.equals(username)) sb.append("user change ignored (fixed while the agent runs); ");
        return sb.length() == 0 ? "" : sb.substring(0, sb.length() - 2);
    }

    /** The protocol version to put on the wire: the newest one both sides know. Never below the oldest this encoder can write. */
    int negotiatedWire() {
        return Math.max(Snapshot.WIRE_MIN, Math.min(wire, Snapshot.WIRE_VERSION));
    }
}
