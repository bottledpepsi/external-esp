package mcradar;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.io.PrintStream;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;

/** The inspector's agent half: request parsing, keep-alive, safe value reading, and the whole round trip on the synthetic world. */
public final class InspectorTest {
    private static int fails = 0, checks = 0;
    private static void check(boolean ok, String what) { checks++; if (!ok) { fails++; System.out.println("FAIL: " + what); } }

    static class Boom { @Override public String toString() { throw new IllegalStateException("toString must never be called on game objects"); } }
    enum Color { RED, GREEN }
    @SuppressWarnings("unused")
    static class Base { int baseInt = 7; protected String secret = "base\u0007\u00e9"; private static int ignoredStatic = 5; }
    @SuppressWarnings("unused")
    static class Thing extends Base {
        int i = 3; long l = 1L << 40; float f = 1.5f; double d = -2.25; boolean b = true; byte by = -3; short sh = 9; char ch = 'A';
        String s = "hello"; String nul = null; Color color = Color.GREEN; int[] arr = new int[4]; String[] strs = new String[2];
        List<Integer> list = new ArrayList<Integer>(java.util.Arrays.asList(1, 2, 3)); Boom boom = new Boom(); Object boxed = Integer.valueOf(42);
        String longText = new String(new char[500]).replace('\0', 'x');
    }

    @SuppressWarnings("unused")
    static class Wide { int a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,a16,a17,a18,a19,a20,a21,a22,a23,a24,a25,a26,a27,a28,a29,a30,a31,a32,a33,a34; }

    static void write(File f, String text) throws Exception {
        FileOutputStream o = new FileOutputStream(f);
        try { o.write(text.getBytes("UTF-8")); } finally { o.close(); }
    }

    public static void main(String[] args) throws Exception {
        PrintStream err = System.err;
        System.setErr(new PrintStream(new OutputStream() { public void write(int b) { } }));
        try {
            // ---- the request text ----
            int[] r = Inspector.parse("inspect 1234567\n");
            check(r != null && r[0] == Inspector.MODE_ENTITY && r[1] == 1234567, "inspect <id>");
            r = Inspector.parse("inspect 4294967295");
            check(r != null && r[0] == Inspector.MODE_ENTITY && r[1] == -1, "the largest id wraps to -1 like the wire's unsigned ids");
            check(Inspector.parse("inspect player")[0] == Inspector.MODE_PLAYER && Inspector.parse("inspect off")[0] == Inspector.MODE_OFF, "player / off");
            check(Inspector.parse("  inspect   77  \nsecond line is ignored")[1] == 77, "whitespace and a second line");
            for (String bad : new String[] {null, "", "inspect", "inspect ", "inspect x", "inspect -1", "inspect 4294967296", "inspect 12345678901", "inspect 1 2", "INSPECT 5", "delete 5", "inspect 0x10", "inspect 1e3"})
                check(Inspector.parse(bad) == null, "not a request: " + bad);

            check(Inspector.validPath(new File(System.getProperty("java.io.tmpdir"), "ctl.txt").getAbsolutePath()), "an absolute path is accepted");
            check(!Inspector.validPath(null) && !Inspector.validPath("") && !Inspector.validPath("relative/path") && !Inspector.validPath("/tmp/a\nb") && !Inspector.validPath("/tmp/" + new String(new char[300]).replace('\0', 'x')), "relative, empty, control characters, too long: refused");

            // ---- the option ----
            AgentConfig c = AgentConfig.parse("user=A;ctl=" + new File(System.getProperty("java.io.tmpdir"), "c.ctl").getAbsolutePath());
            check(c.ctlFile != null && c.ctlFile.endsWith("c.ctl"), "ctl option read");
            check(AgentConfig.parse("user=A;ctl=relative").ctlFile == null && AgentConfig.parse("user=A").ctlFile == null, "bad or missing ctl: none");

            // ---- keep-alive: the file's changes are the heartbeat ----
            File ctl = new File(System.getProperty("java.io.tmpdir"), "esp-inspector-test.ctl");
            ctl.delete();
            Inspector in = new Inspector();
            check(in.poll(0) == Inspector.MODE_OFF, "no file set");
            in.setFile(ctl.getAbsolutePath());
            check(in.poll(0) == Inspector.MODE_OFF, "file missing: off");
            write(ctl, "inspect 55\n");
            check(in.poll(1000) == Inspector.MODE_ENTITY && in.target() == 55, "request read");
            check(in.poll(1000 + Inspector.KEEPALIVE_MS) == Inspector.MODE_ENTITY, "still live inside the keep-alive");
            check(in.poll(1000 + Inspector.KEEPALIVE_MS + 1) == Inspector.MODE_OFF, "lapses when not renewed");
            ctl.setLastModified(ctl.lastModified() + 5000);
            check(in.poll(20000) == Inspector.MODE_ENTITY, "a rewrite (new modification time) renews it");
            write(ctl, "inspect player\n"); ctl.setLastModified(ctl.lastModified() + 9000);
            check(in.poll(20001) == Inspector.MODE_PLAYER, "switched to the player view");
            write(ctl, "inspect off\n"); ctl.setLastModified(ctl.lastModified() + 9000);
            check(in.poll(20002) == Inspector.MODE_OFF, "off");
            write(ctl, "garbage\n"); ctl.setLastModified(ctl.lastModified() + 9000);
            check(in.poll(20003) == Inspector.MODE_OFF, "garbage changes nothing");
            write(ctl, new String(new char[2000]).replace('\0', 'x')); ctl.setLastModified(ctl.lastModified() + 9000);
            check(in.poll(20004) == Inspector.MODE_OFF, "an oversized file is not read");
            in.setFile(null);
            check(in.poll(20005) == Inspector.MODE_OFF, "file removed from the config: off");
            ctl.delete();

            // ---- values: primitives, strings, enums, sizes - and never toString() on a game object ----
            Thing t = new Thing();
            List<String[]> rows = new ArrayList<String[]>();
            Inspector.fieldRows(t, rows);
            check(rows.size() == 19, "all 17 fields of Thing and its base are listed, got " + rows.size());
            List<String[]> wide = new ArrayList<String[]>();
            Inspector.fieldRows(new Wide(), wide);
            check(wide.size() == Inspector.MAX_FIELDS, "capped at " + Inspector.MAX_FIELDS + " rows, got " + wide.size());
            java.util.Map<String, String> m = new java.util.LinkedHashMap<String, String>();
            for (String[] kv : rows) m.put(kv[0], kv[1]);
            check("3".equals(m.get("Thing.i : int")) && "1099511627776".equals(m.get("Thing.l : long")) && "1.5".equals(m.get("Thing.f : float")) && "-2.25".equals(m.get("Thing.d : double")), "numbers");
            check("true".equals(m.get("Thing.b : boolean")) && "-3".equals(m.get("Thing.by : byte")) && "9".equals(m.get("Thing.sh : short")) && "65".equals(m.get("Thing.ch : char")), "boolean, byte, short, char");
            check("\"hello\"".equals(m.get("Thing.s : String")) && "null".equals(m.get("Thing.nul : String")) && "GREEN".equals(m.get("Thing.color : Color")), "string, null, enum");
            check("int[4]".equals(m.get("Thing.arr : int[]")) && "String[2]".equals(m.get("Thing.strs : String[]")), "arrays: the length only");
            check("ArrayList size 3".equals(m.get("Thing.list : List")), "a JDK collection: its size");
            check("Boom".equals(m.get("Thing.boom : Boom")), "a game object: its type name, toString never called");
            check("42".equals(m.get("Thing.boxed : Object")), "a boxed number");
            check(m.get("Thing.longText : String").length() <= Inspector.MAX_VALUE + 2, "long values are cut");
            check(Inspector.valueOf(Thing.class.getDeclaredField("boom"), null).equals("(unreadable)"), "a failed read is reported, not thrown");
            // inherited fields come after the class's own; statics are skipped; control characters are replaced
            List<String[]> all = new ArrayList<String[]>();
            Inspector.fieldRows(new Base(), all);
            check(all.size() == 2 && all.get(0)[0].equals("Base.baseInt : int") && all.get(1)[1].startsWith("\"base?") && all.get(1)[1].indexOf('\u0007') < 0, "base class fields, no statics, control characters made safe: " + all.get(1)[1]);
            check(Inspector.kindName(Snapshot.KIND_HOSTILE).equals("hostile") && Inspector.kindName(99).equals("other"), "kind names");

            // ---- the whole round trip on the synthetic world ----
            File dir = new File(args.length > 0 ? args[0] : "test/fixtures", "synthetic-realistic");
            File ctl2 = new File(System.getProperty("java.io.tmpdir"), "esp-inspector-e2e.ctl");
            ctl2.delete();
            FixtureGame g = FixtureGame.load(dir, new HashSet<String>());
            Probe p = new Probe(g.inst(), AgentConfig.parse("user=Alice;wire=6;export=" + new File(System.getProperty("java.io.tmpdir"), "esp-insp-exp").getPath().replace('\\', '/') + ";ctl=" + ctl2.getAbsolutePath()));
            for (int i = 0; i < 3 && !p.discover(); i++) { }
            double tm = 0;
            Snapshot fresh = null;
            for (int i = 0; i < 150; i++) { tm += 0.006; g.tick(tm); Snapshot s = p.read(); p.pollItemsIfDue(); if (s.itemsFresh) fresh = s; Thread.sleep(2); }
            check(fresh != null && !fresh.hasInspect && fresh.inspect.isEmpty(), "nothing is inspected until the overlay asks");
            List<Snapshot.Ent> ents = p.items().entities();
            check(!ents.isEmpty(), "the world has a mob to inspect");
            int id = ents.get(0).id;
            write(ctl2, "inspect " + (id & 0xFFFFFFFFL) + "\n");
            Snapshot insp = null;
            for (int i = 0; i < 150 && insp == null; i++) { tm += 0.006; g.tick(tm); Snapshot s = p.read(); p.pollItemsIfDue(); if (s.itemsFresh && s.hasInspect) insp = s; Thread.sleep(2); }
            check(insp != null && insp.inspectId == id, "the request is answered with the list");
            if (insp != null) {
                java.util.Map<String, String> rr = new java.util.LinkedHashMap<String, String>();
                for (String[] kv : insp.inspect) rr.put(kv[0], kv[1]);
                check(rr.containsKey("class") && rr.get("class").startsWith("Mob"), "class chain: " + rr.get("class"));
                check("other".equals(rr.get("category")) && rr.get("id").startsWith(Long.toString(id & 0xFFFFFFFFL)), "category and id");
                check(rr.get("position path") != null && !rr.get("position path").equals("-") && rr.get("position") != null && rr.get("position").startsWith("105.0"), "position and where it came from: " + rr.get("position path") + " -> " + rr.get("position"));
                check(rr.get("size") != null && rr.get("size").contains(" x "), "size");
                boolean field = false;
                for (String k : rr.keySet()) field |= k.startsWith("Mob.") || k.startsWith("Entity.");
                check(field, "the entity's own fields are listed");
                check(insp.inspect.size() <= 48 + 6 + Inspector.MAX_FIELDS, "bounded");
                String enc = new String(withWire(insp, 6).encode(), "UTF-8");
                check(enc.contains("\nX " + (id & 0xFFFFFFFFL) + " class "), "the datagram carries X rows");
                check(!new String(withWire(insp, 5).encode(), "UTF-8").contains("\nX "), "never below v6");
                int xl = 0;
                for (String line : enc.split("\n")) if (line.startsWith("X ")) { xl++; check(line.length() < 200 && line.split(" ").length == 4, "an X line has four parts: " + line); }
                check(xl == Math.min(48, insp.inspect.size()), "all rows sent (up to 48): " + xl);
            }
            // the player view
            write(ctl2, "inspect player\n"); ctl2.setLastModified(ctl2.lastModified() + 20000);
            Snapshot pv = null;
            for (int i = 0; i < 150 && pv == null; i++) { tm += 0.006; g.tick(tm); Snapshot s = p.read(); p.pollItemsIfDue(); if (s.itemsFresh && s.hasInspect && s.inspectId == 0) pv = s; Thread.sleep(2); }
            check(pv != null, "the player view is answered");
            if (pv != null) {
                java.util.Map<String, String> rr = new java.util.LinkedHashMap<String, String>();
                for (String[] kv : pv.inspect) rr.put(kv[0], kv[1]);
                check(rr.get("wire") != null && rr.get("wire").startsWith("v6") && rr.containsKey("player list") && rr.containsKey("camera") && rr.containsKey("items"), "discovery paths: " + rr.keySet());
                check(!rr.get("player list").equals("-"), "the player list path was found: " + rr.get("player list"));
            }
            // an id that is not there
            write(ctl2, "inspect 123\n"); ctl2.setLastModified(ctl2.lastModified() + 40000);
            Snapshot nf = null;
            for (int i = 0; i < 150 && nf == null; i++) { tm += 0.006; g.tick(tm); Snapshot s = p.read(); p.pollItemsIfDue(); if (s.itemsFresh && s.hasInspect && s.inspectId == 123) nf = s; Thread.sleep(2); }
            check(nf != null && nf.inspect.size() == 1 && nf.inspect.get(0)[1].contains("not in the agent's entity list"), "an unknown id is said so");
            // off: the rows stop
            write(ctl2, "inspect off\n"); ctl2.setLastModified(ctl2.lastModified() + 60000);
            Snapshot off = null;
            for (int i = 0; i < 150 && off == null; i++) { tm += 0.006; g.tick(tm); Snapshot s = p.read(); p.pollItemsIfDue(); if (s.itemsFresh && !s.hasInspect) off = s; Thread.sleep(2); }
            check(off != null && off.inspect.isEmpty(), "off: no rows");
            // below v6 the control file is not even looked at
            ctl2.delete();
        } finally {
            System.setErr(err);
        }
        System.out.println(fails == 0 ? "ALL OK (" + checks + " checks)" : fails + " FAILURES of " + checks);
        System.exit(fails == 0 ? 0 : 1);
    }

    private static Snapshot withWire(Snapshot s, int wire) { s.wire = wire; s.itemsFresh = true; return s; }
}
