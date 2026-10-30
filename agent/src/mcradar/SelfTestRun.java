package mcradar;

import java.io.File;
import java.io.FileOutputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * The self-test: for `selftest=SECONDS` the agent watches what discovery finds and what reaches the wire, then writes esp_selftest.txt (or selftestout=FILE).
 * Line based, stable, safe to share: no player names, no paths of the machine, no tokens.
 *
 *   CHECK <name> <PASS|WARN|FAIL|SKIP> <text>      one per feature
 *   RESULT <word>                                  PASS | PASS-WITH-SKIPS | WARN | FAIL | NOT-MINECRAFT | NOT-IN-WORLD
 *   EXIT <n>                                       0 all passed, 1 passed but something could not be exercised (move the mouse, drop an item),
 *                                                  2 warnings, 3 failures, 10 not a Minecraft JVM, 11 no world joined yet
 *                                                  (12 no data, 13 several clients, 14 attach failed are decided by the overlay / CLI, not here)
 *
 * SKIP means "the situation did not occur" (nobody else on the server, no item dropped, the head never moved) - not "it works".
 */
final class SelfTestRun {
    static final int EXIT_OK = 0, EXIT_SKIPS = 1, EXIT_WARN = 2, EXIT_FAIL = 3, EXIT_NOT_MC = 10, EXIT_NO_WORLD = 11;

    final long startMs = System.currentTimeMillis();
    final long durationMs;
    final String outPath;
    boolean done;

    // ---- facts (filled by observe()/finish(); a test fills them directly)
    boolean gameProfileLoaded, ready, localFound, cameraDecided, fovFound, itemClassFound;
    String status = "", playerSource = "", cameraStrategy = "", fovStrategy = "", itemStrategy = "", minecraft = "unknown", container = "";
    int maxPlayers, maxItems, maxItemEntities, maxEntities;
    float yawMin = Float.NaN, yawMax = Float.NaN, pitchMin = Float.NaN, pitchMax = Float.NaN;
    long ticks, sent, sendErrors;
    PacingStats.Summary pacing;
    long periodNs = 8_333_333L;

    SelfTestRun(long seconds, String outPath) { this.durationMs = seconds * 1000L; this.outPath = outPath; }

    boolean headMoved() { return yawMax - yawMin >= 3f && pitchMax - pitchMin >= 1f; }

    private static float min(float a, float b) { return Float.isNaN(a) ? b : Math.min(a, b); }
    private static float max(float a, float b) { return Float.isNaN(a) ? b : Math.max(a, b); }

    /** Every tick, after the snapshot was built. Cheap: a few comparisons. */
    void observe(Probe p, Snapshot s) {
        if (done) return;
        ticks++;
        ready = p.isReady();
        if (s.local != null) {
            localFound = true;
            if (!Float.isNaN(s.local.yaw)) { yawMin = min(yawMin, s.local.yaw); yawMax = max(yawMax, s.local.yaw); }
            if (!Float.isNaN(s.local.pitch)) { pitchMin = min(pitchMin, s.local.pitch); pitchMax = max(pitchMax, s.local.pitch); }
        }
        maxPlayers = Math.max(maxPlayers, s.players.size());
        if (s.itemsFresh) maxItems = Math.max(maxItems, s.items.size());
        maxItemEntities = Math.max(maxItemEntities, p.items().itemEntities);
        maxEntities = Math.max(maxEntities, p.items().entitiesSeen);
        status = s.status;
        if (System.currentTimeMillis() - startMs >= durationMs) finish(p);
    }

    private void finish(Probe p) {
        done = true;
        gameProfileLoaded = p.gameProfileClass() != null;
        ready = p.isReady();
        playerSource = ready ? p.playerSource() : "";
        CameraScanner cs = p.camera();
        cameraDecided = cs.decided; cameraStrategy = cs.matchStrategy; fovFound = cs.fovMethod != null; fovStrategy = cs.fovStrategy;
        for (ItemScanner.DumpItem d : p.items().dumpItems()) { itemClassFound = true; itemStrategy = d.getter != null ? "getter" : "field-path"; }
        FieldPath bc = p.items().bestContainer();
        container = bc == null ? "" : bc.describe();
        periodNs = 1_000_000_000L / Math.max(1, p.config().hz);
        pacing = Agent.pacing.summary(periodNs);
        sent = Agent.datagramsSent; sendErrors = Agent.sendErrors;
        ClassLoader cl = ready && p.containerPath != null ? p.containerPath.steps[0].owner.getClassLoader() : null;
        minecraft = GameInfo.minecraftVersion(cl);
        List<String[]> checks = new ArrayList<String[]>();
        int code = evaluate(checks);
        String report = render(checks, code);
        Log.info("Self-test finished: exit " + code + "\n" + report);
        write(report);
        // the structure dump is the other half of a bug report: write it now whatever the configuration
        try { p.config().dump = true; } catch (Throwable ignored) { }
    }

    /** The verdict. checks gets {name, level, text}; returns the exit code. */
    int evaluate(List<String[]> checks) {
        if (!gameProfileLoaded) {
            checks.add(new String[] {"game", "FAIL", "the GameProfile class is not loaded: this JVM is not a Minecraft client, or the game has not got that far yet"});
            return EXIT_NOT_MC;
        }
        checks.add(new String[] {"game", "PASS", "Minecraft classes found"});
        if (!ready) {
            checks.add(new String[] {"world", "FAIL", "no player list found yet (" + status + "): join a world (single player or a server) and run the self-test again"});
            return EXIT_NO_WORLD;
        }
        checks.add(new String[] {"world", "PASS", "discovered; player source: " + playerSource + (playerSource.startsWith("players-list") ? "" : " (a fallback)")});
        boolean fail = false, warn = false, skip = false;
        if (!playerSource.startsWith("players-list")) warn = true;

        checks.add(localFound ? new String[] {"local-player", "PASS", "the local player is found"} : new String[] {"local-player", "FAIL", "the local player was never found (set --user or check the player list path in esp_layout.json)"});
        fail |= !localFound;

        if (playerSource.equals("local player only")) { checks.add(new String[] {"players", "WARN", "no list of other players was found (local player only)"}); warn = true; }
        else if (maxPlayers > 0) checks.add(new String[] {"players", "PASS", "other players seen: up to " + maxPlayers});
        else { checks.add(new String[] {"players", "SKIP", "no other player was in range: join a server or a LAN world with a second player to check"}); skip = true; }

        if (cameraDecided) checks.add(new String[] {"camera", "PASS", "exact camera found (" + cameraStrategy + ")"});
        else if (!headMoved()) { checks.add(new String[] {"camera", "SKIP", "the camera is found by comparing it with your head movement and you did not turn the view: move the mouse around for a few seconds"}); skip = true; }
        else { checks.add(new String[] {"camera", "FAIL", "the view moved (yaw " + (int) (yawMax - yawMin) + " deg, pitch " + (int) (pitchMax - pitchMin) + " deg) but no camera object matched: the overlay falls back to a reconstructed camera"}); fail = true; }

        if (cameraDecided) {
            if (fovFound) checks.add(new String[] {"fov", "PASS", "FOV method found (" + fovStrategy + ")"});
            else { checks.add(new String[] {"fov", "WARN", "no FOV method found: the configured FOV is used, boxes drift when the FOV changes (sprinting, potions)"}); warn = true; }
        } else checks.add(new String[] {"fov", "SKIP", "needs the camera"});

        if (itemClassFound && maxItems > 0) checks.add(new String[] {"items", "PASS", "dropped items found via " + itemStrategy + ": up to " + maxItems + " at once"});
        else if (itemClassFound) { checks.add(new String[] {"items", "WARN", "an item class was found but no item was reported"}); warn = true; }
        else if (maxItemEntities > 0) { checks.add(new String[] {"items", "FAIL", maxItemEntities + " item entities were seen but their stack could not be read"}); fail = true; }
        else if (container.isEmpty()) { checks.add(new String[] {"items", "FAIL", "no entity container was found: item ESP cannot work"}); fail = true; }
        else { checks.add(new String[] {"items", "SKIP", "no item was lying around: drop one (Q) near you and run the self-test again"}); skip = true; }

        if (pacing != null && pacing.samples > 20) {
            boolean late = pacing.lateShare > 0.10 || pacing.p99Ms > 4.0 * periodNs / 1e6;
            checks.add(new String[] {"pacing", late ? "WARN" : "PASS", String.format(java.util.Locale.ROOT, "tick interval mean %.2f p99 %.2f max %.2f ms at %.0f Hz configured, late %.1f%%", pacing.meanMs, pacing.p99Ms, pacing.maxMs, 1e9 / periodNs, pacing.lateShare * 100)});
            warn |= late;
        }
        if (sendErrors > 0) { checks.add(new String[] {"send", "WARN", sendErrors + " of " + sent + " datagrams could not be sent"}); warn = true; }
        else checks.add(new String[] {"send", "PASS", sent + " datagrams sent"});
        return fail ? EXIT_FAIL : warn ? EXIT_WARN : skip ? EXIT_SKIPS : EXIT_OK;
    }

    static String resultWord(int code) {
        switch (code) {
            case EXIT_OK: return "PASS"; case EXIT_SKIPS: return "PASS-WITH-SKIPS"; case EXIT_WARN: return "WARN"; case EXIT_FAIL: return "FAIL";
            case EXIT_NOT_MC: return "NOT-MINECRAFT"; default: return "NOT-IN-WORLD";
        }
    }

    String render(List<String[]> checks, int code) {
        StringBuilder b = new StringBuilder("esp-selftest 1\n");
        b.append("agent: build ").append(GameInfo.agentBuild()).append(", wire ").append(Snapshot.WIRE_VERSION).append("\n");
        b.append("java: ").append(GameInfo.java()).append("\n");
        b.append("os: ").append(System.getProperty("os.name")).append(' ').append(System.getProperty("os.version")).append(' ').append(System.getProperty("os.arch")).append("\n");
        b.append("minecraft: ").append(minecraft).append("\n");
        b.append("window: ").append(durationMs / 1000).append(" s, ").append(ticks).append(" ticks\n");
        if (ready) {
            b.append("player-source: ").append(playerSource).append("\n");
            b.append("camera: ").append(cameraDecided ? cameraStrategy : "not found").append(", fov: ").append(fovFound ? fovStrategy : "none").append("\n");
            b.append("entities: up to ").append(maxEntities).append(" seen, ").append(maxItemEntities).append(" item entities, ").append(maxItems).append(" items reported\n");
        }
        if (pacing != null) b.append("pacing: ").append(pacing).append("\n");
        for (String[] c : checks) b.append("CHECK ").append(c[0]).append(' ').append(c[1]).append(' ').append(c[2]).append("\n");
        b.append("RESULT ").append(resultWord(code)).append("\n");
        b.append("EXIT ").append(code).append("\n");
        b.append("FILES esp_selftest.txt esp_structure.json esp_agent.log (send these three with a bug report; they contain no names or tokens)\n");
        return b.toString();
    }

    private void write(String report) {
        try {
            FileOutputStream f = new FileOutputStream(new File(outPath));
            try { f.write(report.getBytes("UTF-8")); } finally { f.close(); }
        } catch (Throwable t) { Log.warn("Could not write " + outPath + ": " + t); }
    }
}
