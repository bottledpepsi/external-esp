package mcradar;

/** Tests of the agent option string and of the wire version negotiation (package mcradar: AgentConfig is package-private). */
public final class AgentConfigTest {
    private static int fails = 0, checks = 0;

    private static void check(boolean ok, String what) {
        checks++;
        if (!ok) { fails++; System.out.println("FAIL: " + what); }
    }

    public static void main(String[] args) {
        AgentConfig d = AgentConfig.parse(null);
        check(d.port == 47615 && d.hz == 20 && d.wire == 4, "defaults: port 47615, hz 20, wire 4 (an overlay that passes no wire= option is treated as v4)");
        check(d.negotiatedWire() == 4, "no wire option -> v4 (an overlay that does not say anything is treated as v4)");

        AgentConfig c = AgentConfig.parse("port=50000;hz=120;wire=4;export=C:/x/y;user=Alice");
        check(c.port == 50000 && c.hz == 120 && c.wire == 4 && c.exportDir.equals("C:/x/y") && "Alice".equals(c.username), "full option string");
        check(c.negotiatedWire() == 4, "overlay v4 / agent v4 -> v4");

        // a newer overlay never makes the agent write a version it does not know
        AgentConfig n = AgentConfig.parse("wire=9");
        check(n.wire == 9 && n.negotiatedWire() == Snapshot.WIRE_VERSION && Snapshot.WIRE_VERSION == 5, "overlay wants v9: the agent writes its own newest (" + Snapshot.WIRE_VERSION + ")");

        // an older overlay than this encoder can write: still the oldest it can write (the agent logs a warning)
        AgentConfig o = AgentConfig.parse("wire=2");
        check(o.wire == 2 && o.negotiatedWire() == Snapshot.WIRE_MIN, "overlay v2: the agent writes " + Snapshot.WIRE_MIN);
        check(AgentConfig.parse("wire=0").negotiatedWire() == Snapshot.WIRE_MIN && AgentConfig.parse("wire=-5").negotiatedWire() == Snapshot.WIRE_MIN, "wire 0 / negative");

        // junk does not break parsing of the other options
        AgentConfig j = AgentConfig.parse("wire=abc;port=1234;;=;hz=x;wire");
        check(j.wire == 4 && j.port == 1234 && j.hz == 20, "junk values are ignored, the rest is kept");
        check(AgentConfig.parse("wire= 5 ").wire == 5, "whitespace around the value");
        check(AgentConfig.parse("wire=99999999999999999999").wire == 4, "overflowing value is ignored");

        // session token option
        check("0123456789abcdef0123456789abcdef".equals(AgentConfig.parse("token=0123456789abcdef0123456789abcdef").token), "token option");
        check(AgentConfig.parse("token=short").token == null && AgentConfig.parse("token=has space in it 123456").token == null && AgentConfig.parse("token=0123456789abcdef!").token == null
                && AgentConfig.parse("token=" + new String(new char[65]).replace('\0', 'a')).token == null && AgentConfig.parse("").token == null, "invalid tokens are ignored");
        check(AgentConfig.parse("wire=5;token=0123456789abcdef0123456789abcdef").negotiatedWire() == 5, "overlay v5 / agent v5 -> v5");
        check(AgentConfig.parse("wire=5").negotiatedWire() == 5 && AgentConfig.parse("port=1").negotiatedWire() == 4, "v5 only when the overlay asks for it");

        // the header carries the negotiated version
        Snapshot s = new Snapshot();
        check(new String(s.encode(), java.nio.charset.Charset.forName("UTF-8")).startsWith("MCRADAR 4\n"), "default header is v4");

        System.out.println(fails == 0 ? "ALL OK (" + checks + " checks)" : fails + " FAILURES of " + checks);
        System.exit(fails == 0 ? 0 : 1);
    }
}
