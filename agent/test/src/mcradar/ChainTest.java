package mcradar;

import java.io.File;
import java.io.OutputStream;
import java.io.PrintStream;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Discovery fallback chains, driven through fake games rebuilt from the synthetic fixtures with the structure degraded on purpose:
 *   mixed-only  no list that holds only players: the entity container (players + items + mobs) must be used           -> "entity container (mixed)"
 *   local-only  no container at all: the local player alone is still reported, and the status says so                -> "local player only"
 *   upgrade     a fallback is replaced by the better source when it appears (a world that loads late)
 *   epoch       what was learned as a negative in one world (no item stacks) is forgotten in the next
 *   budget      classification that runs out of time is deferred, never cached as "not an item"
 */
public final class ChainTest {
    /** package access to StructureDump.build() */
    static final class StructureDumpAccess { final Probe p; StructureDumpAccess(Probe p) { this.p = p; } String json() { return new StructureDump(p).build(); } }
    private static int fails = 0, checks = 0;
    private static void check(boolean ok, String what) { checks++; if (!ok) { fails++; System.out.println("FAIL: " + what); } }
    private static List<Snapshot.Item> lastFresh;

    private static Probe start(FixtureGame g, String tag) throws Exception {
        Probe p = new Probe(g.inst(), AgentConfig.parse("user=Alice;export=" + new File(System.getProperty("java.io.tmpdir"), "esp-chain-" + tag).getPath().replace('\\', '/')));
        for (int i = 0; i < 3 && !p.discover(); i++) { }
        return p;
    }

    private static Snapshot run(FixtureGame g, Probe p, int ticks, double[] t) throws Exception {
        Snapshot s = null;
        for (int i = 0; i < ticks; i++) { t[0] += 0.006; g.tick(t[0]); s = p.read(); p.pollItemsIfDue(); if (s.itemsFresh) lastFresh = s.items; Thread.sleep(2); }
        return s;
    }

    private static Set<String> v(String... a) { Set<String> s = new HashSet<String>(); for (String x : a) s.add(x); return s; }

    public static void main(String[] args) throws Exception {
        PrintStream err = System.err;
        System.setErr(new PrintStream(new OutputStream() { public void write(int b) { } }));
        try {
            File fx = new File(args.length > 0 ? args[0] : "test/fixtures");
            for (String fixture : new String[] {"synthetic-fakegame", "synthetic-realistic"}) {
                File dir = new File(fx, fixture);
                // ---- normal: rank 0, nothing about fallbacks in the status
                FixtureGame g = FixtureGame.load(dir, v());
                Probe p = start(g, "n");
                double[] t = {0};
                Snapshot s = run(g, p, 50, t);
                check(p.playerSource().startsWith("players-list"), fixture + ": normal game uses the dedicated player list, got " + p.playerSource());
                check(!s.status.contains("players via"), fixture + ": no fallback text in the status of a normal game: " + s.status);

                // ---- mixed-only
                g = FixtureGame.load(dir, v("mixed-only"));
                p = start(g, "m");
                t = new double[] {0};
                lastFresh = null;
                s = run(g, p, 400, t);
                check(p.playerSource().equals("entity container (mixed)"), fixture + " mixed-only: source is the mixed entity container, got " + p.playerSource());
                check(s.local != null && "Alice".equals(s.local.name) && s.players.size() == 2, fixture + " mixed-only: Alice + 2 other players, got " + s.players.size());
                check(s.status.contains("players via entity container (mixed)"), fixture + " mixed-only: the status names the source: " + s.status);
                check(lastFresh != null && lastFresh.size() >= 3, fixture + " mixed-only: items still found, got " + (lastFresh == null ? 0 : lastFresh.size()));

                // ---- local-only
                g = FixtureGame.load(dir, v("local-only"));
                p = start(g, "l");
                t = new double[] {0};
                s = run(g, p, 200, t);
                check(p.playerSource().equals("local player only"), fixture + " local-only: source, got " + p.playerSource());
                check(s.local != null && "Alice".equals(s.local.name) && s.players.isEmpty(), fixture + " local-only: only Alice is reported");
                check(s.status.contains("local player only"), fixture + " local-only: the status says it: " + s.status);

                // ---- upgrade: a local-only game gets its player list (a different world with the full structure)
                Probe.UPGRADE_INTERVAL_MS = 300;
                try {
                    g = FixtureGame.load(dir, v("local-only"));
                    p = start(g, "u");
                    t = new double[] {0};
                    run(g, p, 50, t);
                    check(p.playerSource().equals("local player only"), fixture + " upgrade: starts local-only");
                    g.swapWorld(v());
                    s = run(g, p, 600, t);
                    check(p.playerSource().startsWith("players-list"), fixture + " upgrade: replaced by the real list, got " + p.playerSource());
                    check(s.players.size() == 2, fixture + " upgrade: other players appear, got " + s.players.size());
                    check(p.epoch >= 1, fixture + " upgrade: the world change was noticed (epoch " + p.epoch + ")");
                } finally { Probe.UPGRADE_INTERVAL_MS = 15000; }
            }

            // ---- item stack strategies: the field path first, the getter when there is no field path
            {
                FixtureGame g0 = FixtureGame.load(new File(fx, "synthetic-realistic"), v());
                Probe p0 = start(g0, "s1");
                double[] t0 = {0};
                lastFresh = null;
                Snapshot s0 = run(g0, p0, 300, t0);
                boolean anyGetter = false;
                for (ItemScanner.DumpItem d : p0.items().dumpItems()) anyGetter |= d.getter != null;
                check(!anyGetter && lastFresh != null && lastFresh.size() >= 6, "items: the field path wins when there is one, got " + (lastFresh == null ? 0 : lastFresh.size()) + " items");
                check(!s0.status.contains("via getter"), "items: status does not mention the getter on a normal game: " + s0.status);

                g0 = FixtureGame.load(new File(fx, "synthetic-realistic"), v("getter-only"));
                p0 = start(g0, "s2");
                t0 = new double[] {0};
                lastFresh = null;
                s0 = run(g0, p0, 300, t0);
                anyGetter = false;
                for (ItemScanner.DumpItem d : p0.items().dumpItems()) anyGetter |= d.getter != null && d.steps.isEmpty();
                check(anyGetter, "items getter-only: the stack is found through a getter");
                check(lastFresh != null && lastFresh.size() >= 6, "items getter-only: all 6 items emitted, got " + (lastFresh == null ? 0 : lastFresh.size()));
                Set<String> ids = new HashSet<String>();
                if (lastFresh != null) for (Snapshot.Item it : lastFresh) ids.add(it.key);
                check(ids.contains("minecraft:diamond_sword") && ids.contains("minecraft:cobblestone"), "items getter-only: ids resolved " + ids);
                check(s0.status.contains("via getter"), "items getter-only: the status names the strategy: " + s0.status);
                // the structure dump of such a game records the getter, and a game rebuilt from that dump finds it again
                String dumpJson = new StructureDumpAccess(p0).json();
                check(dumpJson.contains("\"getter\":{\"class\""), "items getter-only: esp_structure.json records the getter");
            }

            // ---- camera and FOV strategies (the fakegame fixture has a camera and a FOV method)
            File fk = new File(fx, "synthetic-fakegame");
            {
                FixtureGame g0 = FixtureGame.load(fk, v());
                Probe p0 = start(g0, "c1");
                double[] t0 = {0};
                run(g0, p0, 700, t0);
                check(p0.camera().decided && p0.camera().matchStrategy.equals("value-match") && p0.camera().fovStrategy.equals("holder"), "camera: strict match and the holder's FOV method on a normal game, got " + p0.camera().matchStrategy + " / " + p0.camera().fovStrategy);

                // camera yaw is 25 degrees off the player's: over the strict 20 degree limit, inside the relaxed 35
                CameraScanner.RELAXED_AFTER_MS = 500;
                try {
                    g0 = FixtureGame.load(fk, v("camera-offset"));
                    p0 = start(g0, "c2");
                    t0 = new double[] {0};
                    run(g0, p0, 120, t0);                                  // well inside the first 500 ms: strict fails, relaxed not yet allowed
                    check(!p0.camera().decided, "camera-offset: not decided by the strict match");
                    Snapshot s0 = run(g0, p0, 1500, t0);
                    check(p0.camera().decided && p0.camera().matchStrategy.contains("relaxed"), "camera-offset: decided by the relaxed match, got " + p0.camera().matchStrategy);
                    check(s0.status.contains("relaxed match"), "camera-offset: the status says the match was relaxed: " + s0.status);
                } finally { CameraScanner.RELAXED_AFTER_MS = 15000; }

                // the FOV method lives on the root object, two levels above the camera's holder
                g0 = FixtureGame.load(fk, v("fov-on-root"));
                p0 = start(g0, "c3");
                t0 = new double[] {0};
                Snapshot s0 = run(g0, p0, 800, t0);
                check(p0.camera().fovStrategy.startsWith("ancestor"), "fov-on-root: found on an ancestor, got " + p0.camera().fovStrategy);
                check(s0.camera != null && s0.camera.fov >= 60 && s0.camera.fov <= 80, "fov-on-root: FOV read, got " + (s0.camera == null ? "no camera" : s0.camera.fov + ""));

                // the FOV method throws when the camera is decided (chunk race), and works later: it must be found on a retry
                CameraScanner.FOV_RETRY_MS = 300;
                try {
                    g0 = FixtureGame.load(fk, v());
                    g0.fovBroken(true);
                    p0 = start(g0, "c4");
                    t0 = new double[] {0};
                    s0 = run(g0, p0, 700, t0);
                    check(p0.camera().decided && p0.camera().fovMethod == null, "fov retry: camera decided while the FOV method is broken, FOV unknown");
                    check(s0.camera != null && Float.isNaN(s0.camera.fov), "fov retry: the snapshot says FOV unknown, not a made-up value");
                    g0.fovBroken(false);
                    s0 = run(g0, p0, 400, t0);
                    check(p0.camera().fovMethod != null && s0.camera != null && s0.camera.fov >= 60, "fov retry: found on a later try, got " + (s0.camera == null ? "no camera" : s0.camera.fov + ""));
                } finally { CameraScanner.FOV_RETRY_MS = 5000; }
            }

            // ---- epoch: item entities without stacks are condemned (10 s negative); in the next world they must be found at once
            FixtureGame g = FixtureGame.load(new File(fx, "synthetic-realistic"), v("no-stacks"));
            Probe p = start(g, "e");
            double[] t = {0};
            run(g, p, 300, t);
            check(p.items().learnedClasses() == 0, "epoch: no item class learned while the entities have no stacks");
            g.swapWorld(v());
            lastFresh = null;
            long end = System.currentTimeMillis() + 3000;
            while (System.currentTimeMillis() < end && (lastFresh == null || lastFresh.size() < 6)) run(g, p, 20, t);
            check(lastFresh != null && lastFresh.size() >= 6, "epoch: items of the new world appear within 3 s (not after the 10 s negative cache), got " + (lastFresh == null ? 0 : lastFresh.size()));

            // ---- budget: classification that runs out of time is deferred, not condemned
            g = FixtureGame.load(new File(fx, "synthetic-realistic"), v());
            p = start(g, "b");
            t = new double[] {0};
            ItemScanner.classifyBudgetMs = 0;
            try {
                run(g, p, 300, t);
                check(p.items().learnedClasses() == 0 && p.items().budgetStops > 0, "budget 0: nothing classified, the stops are counted (" + p.items().budgetStops + ")");
            } finally { ItemScanner.classifyBudgetMs = 30; }
            lastFresh = null;
            end = System.currentTimeMillis() + 3000;
            while (System.currentTimeMillis() < end && (lastFresh == null || lastFresh.size() < 6)) run(g, p, 20, t);
            check(lastFresh != null && lastFresh.size() >= 6, "budget restored: the deferred classes are classified on the next polls, got " + (lastFresh == null ? 0 : lastFresh.size()));
        } finally { System.setErr(err); }
        System.out.println(fails == 0 ? "ALL OK (" + checks + " checks)" : fails + " FAILURES of " + checks);
        System.exit(fails == 0 ? 0 : 1);
    }
}
