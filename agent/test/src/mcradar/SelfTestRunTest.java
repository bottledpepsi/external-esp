package mcradar;

import java.util.ArrayList;
import java.util.List;

/** The self-test verdicts: every branch of SelfTestRun.evaluate with hand-made facts, and the report format the overlay parses. */
public final class SelfTestRunTest {
    private static int fails = 0, checks = 0;
    private static void check(boolean ok, String what) { checks++; if (!ok) { fails++; System.out.println("FAIL: " + what); } }

    /** A run in which everything worked. */
    static SelfTestRun good() {
        SelfTestRun r = new SelfTestRun(10, "unused");
        r.gameProfileLoaded = true; r.ready = true; r.localFound = true; r.playerSource = "players-list (singleton roots)"; r.maxPlayers = 2;
        r.cameraDecided = true; r.cameraStrategy = "value-match"; r.fovFound = true; r.fovStrategy = "holder";
        r.itemClassFound = true; r.itemStrategy = "field-path"; r.maxItems = 3; r.container = "a.b";
        r.yawMin = -20; r.yawMax = 20; r.pitchMin = 0; r.pitchMax = 10; r.sent = 600;
        return r;
    }

    static int code(SelfTestRun r, List<String[]> out) { return r.evaluate(out); }

    static String level(List<String[]> c, String name) { for (String[] x : c) if (x[0].equals(name)) return x[1]; return "-"; }

    public static void main(String[] a) {
        List<String[]> c = new ArrayList<String[]>();
        SelfTestRun r = good();
        check(code(r, c) == 0 && level(c, "camera").equals("PASS") && level(c, "items").equals("PASS"), "everything works -> exit 0");

        r = good(); r.gameProfileLoaded = false; c.clear();
        check(code(r, c) == 10 && c.size() == 1, "no GameProfile -> exit 10 (not Minecraft), nothing else judged");
        r = good(); r.ready = false; r.status = "searching: no player list yet (join a world)"; c.clear();
        check(code(r, c) == 11 && c.get(1)[2].contains("join a world"), "no player list -> exit 11 (not in a world) with the instruction");

        r = good(); r.maxPlayers = 0; c.clear();
        check(code(r, c) == 1 && level(c, "players").equals("SKIP"), "nobody else around -> SKIP, exit 1");
        r = good(); r.cameraDecided = false; r.fovFound = false; r.yawMin = r.yawMax = 5; c.clear();
        check(code(r, c) == 1 && level(c, "camera").equals("SKIP") && level(c, "fov").equals("SKIP"), "head never moved -> camera and FOV SKIP, not FAIL");
        r = good(); r.cameraDecided = false; r.fovFound = false; c.clear();
        check(code(r, c) == 3 && level(c, "camera").equals("FAIL"), "head moved but no camera -> FAIL");
        r = good(); r.fovFound = false; c.clear();
        check(code(r, c) == 2 && level(c, "fov").equals("WARN"), "camera without FOV -> WARN, exit 2");
        r = good(); r.itemClassFound = false; r.maxItems = 0; r.maxItemEntities = 0; c.clear();
        check(code(r, c) == 1 && level(c, "items").equals("SKIP"), "no item entities seen -> SKIP");
        r = good(); r.itemClassFound = false; r.maxItems = 0; r.maxItemEntities = 4; c.clear();
        check(code(r, c) == 3 && level(c, "items").equals("FAIL"), "item entities seen but no stack path -> FAIL");
        r = good(); r.itemClassFound = false; r.maxItems = 0; r.container = ""; c.clear();
        check(code(r, c) == 3 && level(c, "items").equals("FAIL"), "no entity container -> FAIL");
        r = good(); r.localFound = false; c.clear();
        check(code(r, c) == 3 && level(c, "local-player").equals("FAIL"), "no local player -> FAIL");
        r = good(); r.playerSource = "entity container (mixed)"; c.clear();
        check(code(r, c) == 2, "a fallback player source -> warning");
        r = good(); r.playerSource = "local player only"; r.maxPlayers = 0; c.clear();
        check(code(r, c) == 2 && level(c, "players").equals("WARN"), "local player only -> WARN");
        r = good(); r.sendErrors = 5; c.clear();
        check(code(r, c) == 2 && level(c, "send").equals("WARN"), "send errors -> WARN");
        r = good(); r.pacing = new PacingStats.Summary(); r.pacing.samples = 500; r.pacing.lateShare = 0.3; r.pacing.meanMs = 9; r.pacing.p99Ms = 30; r.pacing.maxMs = 90; c.clear();
        check(code(r, c) == 2 && level(c, "pacing").equals("WARN"), "30% late ticks -> WARN");
        r = good(); r.localFound = false; r.cameraDecided = false; r.maxPlayers = 0; c.clear();
        check(code(r, c) == 3, "a FAIL outranks SKIPs");

        // the report the overlay parses
        r = good(); c.clear();
        String rep = r.render(c, code(r, c));
        check(rep.startsWith("esp-selftest 1\n") && rep.contains("\nRESULT PASS\n") && rep.contains("\nEXIT 0\n") && rep.contains("CHECK camera PASS "), "report format");
        check(!rep.contains("Alice") && !rep.contains("token="), "no names or tokens in the report");
        System.out.println(fails == 0 ? "ALL OK (" + checks + " checks)" : fails + " FAILURES of " + checks);
        System.exit(fails == 0 ? 0 : 1);
    }
}
