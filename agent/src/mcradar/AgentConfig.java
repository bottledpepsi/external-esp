package mcradar;

import java.util.HashMap;
import java.util.Map;

/** Agent options, passed as "key=value;key=value" (premain/agentmain argument string). */
final class AgentConfig {
    int port = 47615;          // UDP port of the C++ radar on 127.0.0.1
    int hz = 20;               // send rate (1..240); the overlay passes hz=120 by default
    String exportDir = System.getProperty("java.io.tmpdir");
    String username;           // override for the local player's name (normally auto-detected)
    /**
     * The newest wire protocol the overlay says it can read (option wire=N). An overlay that does not pass it (an older build, or a
     * hand-written -javaagent line) is treated as speaking v4. The agent sends min(this, Snapshot.WIRE_VERSION).
     */
    int wire = 4;
    /** Session token the overlay generated (option token=...): sent in every v5 datagram. Null = none (then only v4 can be spoken). */
    String token;

    static AgentConfig parse(String args) {
        AgentConfig c = new AgentConfig();
        if (args == null) return c;
        Map<String, String> kv = new HashMap<String, String>();
        for (String part : args.split(";")) {
            int i = part.indexOf('=');
            if (i > 0) kv.put(part.substring(0, i).trim(), part.substring(i + 1).trim());
        }
        try { if (kv.containsKey("port")) c.port = Integer.parseInt(kv.get("port")); } catch (NumberFormatException ignored) { }
        try { if (kv.containsKey("hz")) c.hz = Math.max(1, Math.min(240, Integer.parseInt(kv.get("hz")))); } catch (NumberFormatException ignored) { }
        try { if (kv.containsKey("wire")) c.wire = Integer.parseInt(kv.get("wire")); } catch (NumberFormatException ignored) { }
        String tok = kv.get("token");
        if (tok != null && tok.matches("[0-9A-Za-z]{16,64}")) c.token = tok;
        if (kv.containsKey("export") && !kv.get("export").isEmpty()) c.exportDir = kv.get("export");
        if (kv.containsKey("user") && !kv.get("user").isEmpty()) c.username = kv.get("user");
        return c;
    }

    /** The protocol version to put on the wire: the newest one both sides know. Never below the oldest this encoder can write. */
    int negotiatedWire() {
        return Math.max(Snapshot.WIRE_MIN, Math.min(wire, Snapshot.WIRE_VERSION));
    }
}
