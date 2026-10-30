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
