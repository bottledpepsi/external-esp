package mcradar;

import java.io.File;
import java.io.OutputStream;
import java.io.PrintStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * For every agent/test/fixtures/NAME/structure.json: rebuild a fake game from the dump (FixtureGame), run the agent's real discovery against it and assert
 * that it finds what the dump says it found in the real game: the player list, the local player, the camera (yaw, pitch, position accessor) and the
 * FOV method, and a stack path for every item entity class - plus that the snapshot it produces has the players, the camera and the items.
 *
 * A fixture whose fixture.txt says "real: true" is a dump from a real Minecraft; the others are dumps of the repo's own fake games and only prove
 * the round trip (dump -> generated game -> discovery) works. The summary line says how many of each passed - never "version X works" for a synthetic one.
 *   java mcradar.FixtureTest [fixtures-dir] [fixture-name ...]
 */
public final class FixtureTest {
    private static int fails = 0, checks = 0;
    private static void check(boolean ok, String what) { checks++; if (!ok) { fails++; System.out.println("  FAIL: " + what); } }

    static String names(FieldPath p) {
        StringBuilder b = new StringBuilder();
        if (p != null) for (Acc a : p.steps) b.append(b.length() > 0 ? " > " : "").append(a.owner.getName()).append('.').append(a.f.getName());
        return b.toString();
    }

    static String names(List<Object> steps) {
        StringBuilder b = new StringBuilder();
        for (Object o : steps) {
            Map<String, Object> s = MiniJson.obj(o);
            if (b.length() > 0) b.append(" > ");
            if (s.containsKey("idx")) b.append('[').append(s.get("idx")).append(']'); else b.append(s.get("c")).append('.').append(s.get("f"));
        }
        return b.toString();
    }

    /** One fixture. Returns a one-line verdict; counts into the static check counters. */
    static String run(File dir, Set<String> variants) throws Exception {
        PrintStream err = System.err;
        System.setErr(new PrintStream(new OutputStream() { public void write(int b) { } }));     // the agent's log goes to stderr
        try {
            FixtureGame g = FixtureGame.load(dir, variants);
            AgentConfig cfg = AgentConfig.parse("user=Alice;export=" + new File(System.getProperty("java.io.tmpdir"), "esp-fixture-" + dir.getName()).getPath().replace('\\', '/'));
            Probe probe = new Probe(g.inst(), cfg);
            Map<String, Object> paths = MiniJson.obj(g.doc.get("paths")), roles = MiniJson.obj(g.doc.get("roles")), cam = MiniJson.obj(roles.get("camera"));
            List<Object> items = MiniJson.arr(roles.get("items"));
            boolean wantCam = Boolean.TRUE.equals(cam.get("decided"));

            boolean found = false;
            for (int attempt = 0; attempt < 3 && !found; attempt++) found = probe.discover();
            check(found, dir.getName() + ": discover() finds a player container");
            if (!found) return "FAIL (no player container)";
            check(names(probe.containerPath).equals(names(MiniJson.arr(MiniJson.obj(paths.get("playerList")).get("steps")))),
                    dir.getName() + ": player list path " + names(probe.containerPath) + " == dump " + names(MiniJson.arr(MiniJson.obj(paths.get("playerList")).get("steps"))));
            List<Object> lps = MiniJson.arr(MiniJson.obj(paths.get("localPlayer")).get("steps"));
            if (!lps.isEmpty()) check(names(probe.localPath()).equals(names(lps)), dir.getName() + ": local player path " + names(probe.localPath()) + " == dump " + names(lps));

            Snapshot snap = null;
            long t0 = System.nanoTime();
            boolean camOk = !wantCam, itemsOk = items.isEmpty();
            List<Snapshot.Item> lastItems = new ArrayList<Snapshot.Item>();      // items ride on the snapshots of poll ticks only (F 1)
            int wantItems = 3 * items.size();
            for (int tick = 0; tick < 6000 && (System.nanoTime() - t0) < 25_000_000_000L; tick++) {
                g.tick((System.nanoTime() - t0) / 1e9 * 2);                        // 2x speed: the head sweeps through the camera scanner's movement thresholds sooner
                snap = probe.read();
                probe.pollItemsIfDue();
                if (snap.itemsFresh) lastItems = snap.items;
                camOk = !wantCam || probe.camera().decided;
                itemsOk = items.isEmpty() || lastItems.size() >= wantItems;
                if (camOk && itemsOk && (!wantCam || snap.camera != null) && tick > 200) break;
                Thread.sleep(2);
            }
            check(snap != null && snap.local != null && "Alice".equals(snap.local.name), dir.getName() + ": local player is Alice");
            check(snap != null && snap.players.size() == 2, dir.getName() + ": two other players, got " + (snap == null ? -1 : snap.players.size()));

            if (wantCam) {
                CameraScanner cs = probe.camera();
                check(cs.decided, dir.getName() + ": camera decided");
                if (cs.decided) {
                    check(names(cs.camPath).equals(names(MiniJson.arr(MiniJson.obj(cam.get("path")).get("steps")))), dir.getName() + ": camera path " + names(cs.camPath));
                    check(cs.yawAcc.f.getName().equals(MiniJson.str(MiniJson.obj(cam.get("yaw")).get("field"))), dir.getName() + ": camera yaw field " + cs.yawAcc.f.getName());
                    check(cs.pitchAcc.f.getName().equals(MiniJson.str(MiniJson.obj(cam.get("pitch")).get("field"))), dir.getName() + ": camera pitch field " + cs.pitchAcc.f.getName());
                    Map<String, Object> fov = MiniJson.obj(cam.get("fov"));
                    check((cs.fovMethod == null) == fov.isEmpty() && (cs.fovMethod == null || cs.fovMethod.getName().equals(fov.get("name"))), dir.getName() + ": FOV method " + (cs.fovMethod == null ? "none" : cs.fovMethod.getName()));
                }
                check(snap != null && snap.camera != null && snap.camera.mode == 1, dir.getName() + ": a first-person camera is in the snapshot");
                if (snap != null && snap.camera != null && !MiniJson.obj(cam.get("fov")).isEmpty()) check(snap.camera.fov >= 60 && snap.camera.fov <= 80, dir.getName() + ": FOV " + snap.camera.fov + " in the fake's 63-77 sweep");
            }
            // items: a stack path for every item entity class in the dump, and every item emitted
            Set<String> want = new HashSet<String>(), got = new HashSet<String>();
            for (Object io : items) { Map<String, Object> it = MiniJson.obj(io); want.add(it.get("entity") + " via " + names(MiniJson.arr(it.get("steps")))); }
            for (ItemScanner.DumpItem d : probe.items().dumpItems()) {
                StringBuilder b = new StringBuilder();
                for (Object[] st : d.steps) b.append(b.length() > 0 ? " > " : "").append(st[0] instanceof Acc ? ((Acc) st[0]).owner.getName() + "." + ((Acc) st[0]).f.getName() : "[" + st[0] + "]");
                got.add(d.entity.getName() + " via " + b);
            }
            check(got.equals(want), dir.getName() + ": item stack paths " + got + " == dump " + want);
            check(lastItems.size() >= wantItems, dir.getName() + ": " + wantItems + " items emitted, got " + lastItems.size());
            if (!lastItems.isEmpty()) {
                Set<String> keys = new HashSet<String>();
                for (Snapshot.Item it : lastItems) keys.add(it.key);
                check(keys.containsAll(Arrays.asList("minecraft:diamond_sword", "minecraft:cobblestone", "minecraft:golden_apple")), dir.getName() + ": item ids resolved, got " + keys);
            }
            return fails == 0 ? "ok" : "see FAIL lines";
        } finally { System.setErr(err); }
    }

    public static void main(String[] args) throws Exception {
        File root = new File(args.length > 0 ? args[0] : "test/fixtures");
        Set<String> only = new HashSet<String>(args.length > 1 ? Arrays.asList(args).subList(1, args.length) : new ArrayList<String>());
        File[] dirs = root.listFiles();
        if (dirs == null) { System.out.println("no fixtures directory: " + root); System.exit(1); }
        Arrays.sort(dirs);
        int real = 0, synth = 0, n = 0;
        for (File d : dirs) {
            if (!new File(d, "structure.json").exists() || (!only.isEmpty() && !only.contains(d.getName()))) continue;
            int before = fails;
            String v;
            try { v = run(d, new HashSet<String>()); }
            catch (Throwable t) { fails++; checks++; v = "FAIL: " + t; }
            boolean isReal = new File(d, "fixture.txt").exists() && new String(java.nio.file.Files.readAllBytes(new File(d, "fixture.txt").toPath()), "UTF-8").matches("(?s).*(?m)^real:\\s*true\\s*$.*");
            System.out.println((fails == before ? "PASS " : "FAIL ") + d.getName() + (isReal ? "  [REAL dump]" : "  [synthetic round trip]") + "  " + v);
            n++;
            if (fails == before) { if (isReal) real++; else synth++; }
        }
        System.out.println((fails == 0 ? "ALL OK" : fails + " FAILURES") + " (" + checks + " checks; " + n + " fixtures: " + real + " real, " + synth + " synthetic passed)");
        System.exit(fails == 0 && n > 0 ? 0 : 1);
    }
}
