package mcradar;

import java.io.File;
import java.io.OutputStream;
import java.io.PrintStream;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Hazards the scanners must survive, each driven through a fake game rebuilt from a synthetic fixture:
 *   NaN      the look vector, or the camera's yaw, is NaN (a half-initialised player, a chunk race): nothing throws, nothing NaN reaches the wire,
 *            and the camera scanner does not learn from it
 *   EMPTY    an item entity whose stack is still empty (not synced: ItemStack.EMPTY has count 0 and no item) must not condemn its entity class; once the
 *            stack is filled the item appears within a few polls, not after the 10 s negative cache expires
 *   held yaw looking straight up/down (pitch +-90) the look vector has no horizontal part: the last yaw is held for the player record, and the camera
 *            scanner must NOT learn from that held (stale) yaw - the camera's real yaw keeps moving
 */
public final class DiscoveryRegressionTest {
    private static int fails = 0, checks = 0;
    private static void check(boolean ok, String what) { checks++; if (!ok) { fails++; System.out.println("FAIL: " + what); } }

    private static Probe start(FixtureGame g, String tag) throws Exception {
        Probe p = new Probe(g.inst(), AgentConfig.parse("user=Alice;export=" + new File(System.getProperty("java.io.tmpdir"), "esp-regress-" + tag).getPath().replace('\\', '/')));
        for (int i = 0; i < 3 && !p.discover(); i++) { }
        return p;
    }

    /** Items ride on the snapshot of a poll tick only (F 1): the most recent such list, whichever tick of a batch it came in. */
    private static List<Snapshot.Item> lastFresh;

    /** Runs `ticks` ticks at ~330 Hz of simulated time `t0`..; returns the last snapshot. */
    private static Snapshot run(FixtureGame g, Probe p, int ticks, double[] t) throws Exception {
        Snapshot s = null;
        for (int i = 0; i < ticks; i++) { t[0] += 0.006; g.tick(t[0]); s = p.read(); p.pollItemsIfDue(); if (s.itemsFresh) lastFresh = s.items; Thread.sleep(2); }
        return s;
    }

    public static void main(String[] args) throws Exception {
        PrintStream err = System.err;
        System.setErr(new PrintStream(new OutputStream() { public void write(int b) { } }));
        try {
            File fx = new File(args.length > 0 ? args[0] : "test/fixtures");

            // ---------------- NaN look vector: no exception, no NaN on the wire, camera not trained
            FixtureGame g = FixtureGame.load(new File(fx, "synthetic-fakegame"), new HashSet<String>());
            Probe p = start(g, "nan");
            double[] t = {0};
            g.lookNaN(true);
            Snapshot s = run(g, p, 200, t);
            check(s != null && s.local != null && !Double.isNaN(s.local.x), "NaN look: the local position is still reported");
            check(s.local != null && Float.isNaN(s.local.yaw) && Float.isNaN(s.local.pitch), "NaN look: yaw and pitch are unknown (NaN), not invented");
            s.token = null;
            String wire = new String(s.encode(), "UTF-8");
            check(!wire.contains("NaN") && !wire.contains("Infinity"), "NaN look: no NaN/Infinity text on the wire");
            check(!p.camera().decided, "NaN look: 200 ticks of head movement with an unknown yaw do not decide the camera");
            // the player model was built while the look vector was NaN, so it has no look method: it must be rebuilt within seconds, not at the 15 s refresh
            g.lookNaN(false);
            s = run(g, p, 1500, t);
            check(s.local != null && !Float.isNaN(s.local.yaw) && !Float.isNaN(s.local.pitch), "after the look vector is valid again the yaw is known within ~4 s (model rebuilt), got " + (s.local == null ? "no local" : s.local.yaw + ""));
            run(g, p, 800, t);
            check(p.camera().decided, "and the camera is then decided from real data");

            // ---------------- NaN camera yaw after the camera was decided (a fresh game: the look vector is valid throughout)
            g = FixtureGame.load(new File(fx, "synthetic-fakegame"), new HashSet<String>());
            p = start(g, "camnan");
            t = new double[] {0};
            run(g, p, 600, t);
            check(p.camera().decided, "setup: camera decided on a normal game");
            g.cameraNaN(true);
            s = run(g, p, 20, t);
            check(s.camera == null, "NaN camera yaw: no camera in the snapshot (the overlay reconstructs one)");
            g.cameraNaN(false);
            s = run(g, p, 20, t);
            check(s.camera != null, "camera comes back when the yaw is valid again");

            // ---------------- held yaw: looking straight up
            g = FixtureGame.load(new File(fx, "synthetic-fakegame"), new HashSet<String>());
            p = start(g, "held");
            t = new double[] {0};
            g.forceLook(30f, 10f);
            s = run(g, p, 10, t);
            check(s.local != null && Math.abs(s.local.yaw - 30f) < 0.5f, "yaw 30 is reported while the look vector is normal, got " + (s.local == null ? "none" : s.local.yaw));
            g.forceLook(30f, -90f);
            s = run(g, p, 10, t);
            check(s.local != null && Math.abs(s.local.yaw - 30f) < 0.5f && Math.abs(s.local.pitch + 90f) < 0.5f, "pitch -90 (straight up): the last yaw (30) is held, pitch " + (s.local == null ? "none" : s.local.pitch + "") + " reported");
            // The hazard: the player spins while looking straight up. The look vector cannot show the yaw (it is held at 30), the camera's real yaw sweeps.
            // A scanner that trains on the held value collects 900 wrong observations (mean yaw error ~60 degrees, over the 20 degree limit); one that skips them decides as soon as the head is normal again.
            Probe fresh = start(g, "held2");
            double[] t2 = {0};
            g.forceLook(30f, 10f);
            run(g, fresh, 5, t2);
            g.forceLook(null, -90f);
            run(g, fresh, 900, t2);
            check(!fresh.camera().decided, "held yaw: 900 ticks at pitch -90 do not decide the camera");
            g.forceLook(null, null);
            run(g, fresh, 700, t2);
            check(fresh.camera().decided, "held yaw: the camera is decided within 700 normal ticks - the held values were not learned from");

            // ---------------- empty stack first: with an id method on the stack (it throws on the empty one) and without (the id comes from the item reference, null when empty)
            for (String variant : new String[] {"key", "nokey"}) {
                Set<String> vs = new HashSet<String>();
                if (variant.equals("nokey")) vs.add("nokey");
                g = FixtureGame.load(new File(fx, "synthetic-realistic"), vs);
                p = start(g, "empty-" + variant);
                t = new double[] {0};
                for (int i = 0; i < g.stackCount(); i++) g.emptyStack(i);
                run(g, p, 300, t);                                       // several polls on nothing but empty stacks
                for (int i = 0; i < g.stackCount(); i++) g.fillStack(i, i % 3 == 0 ? "diamond_sword" : i % 3 == 1 ? "cobblestone" : "golden_apple", 1 + i);
                List<Snapshot.Item> got = null;
                lastFresh = null;
                long end = System.currentTimeMillis() + 3000;
                while (System.currentTimeMillis() < end && (got == null || got.size() < g.stackCount())) {
                    run(g, p, 20, t);
                    got = lastFresh;
                }
                check(got != null && got.size() >= g.stackCount(), "empty stack first (" + variant + "): once synced all " + g.stackCount() + " items appear within 3 s (a condemned class waits for the 10 s negative cache), got " + (got == null ? 0 : got.size()));
                Set<String> ids = new HashSet<String>();
                if (got != null) for (Snapshot.Item it : got) ids.add(it.key);
                check(ids.contains("minecraft:diamond_sword") && ids.contains("minecraft:cobblestone"), "ids resolved after the fill (" + variant + "): " + ids);
            }
        } finally { System.setErr(err); }
        System.out.println(fails == 0 ? "ALL OK (" + checks + " checks)" : fails + " FAILURES of " + checks);
        System.exit(fails == 0 ? 0 : 1);
    }
}
