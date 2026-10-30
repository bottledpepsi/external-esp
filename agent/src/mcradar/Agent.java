package mcradar;

import java.lang.instrument.Instrumentation;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;

/**
 * Java agent entry point. Works both ways:
 *   - loaded into a RUNNING game via the Attach API (agentmain)   <- what mcradar.exe does
 *   - started with the JVM argument -javaagent:mcradar-agent.jar=port=47615  (premain)
 *
 * It starts one low-priority daemon thread that (1) discovers the player objects (Probe) and
 * (2) sends a text snapshot to 127.0.0.1:<port> ~20x per second. It never modifies the game.
 */
public final class Agent {
    private static boolean started;
    private static AgentConfig current;
    private static Thread worker;

    public static void premain(String args, Instrumentation inst) { start(args, inst); }
    public static void agentmain(String args, Instrumentation inst) { start(args, inst); }

    private static synchronized void start(String args, Instrumentation inst) {
        if (started) { reload(args, inst); return; }
        started = true;
        java.util.Map<String, String> unused = new java.util.HashMap<String, String>();
        AgentConfig cfg = AgentConfig.parse(args, unused);
        current = cfg;
        Log.init(cfg.exportDir, cfg.verbose);
        Log.info("Starting (Java " + System.getProperty("java.version") + ", port " + cfg.port + ", export " + cfg.exportDir + ")");
        int wire = cfg.negotiatedWire();
        Log.debug("verbose logging is on");
        Log.info("Wire protocol: overlay reads up to v" + cfg.wire + ", agent writes up to v" + Snapshot.WIRE_VERSION + " -> sending v" + wire
                + (wire >= 5 ? (cfg.token != null ? ", session token set" : ", NO session token (an overlay that checks tokens will drop everything)") : ""));
        if (cfg.wire < Snapshot.WIRE_MIN)
            Log.warn("The overlay asked for wire v" + cfg.wire + " but this agent cannot write anything older than v" + Snapshot.WIRE_MIN
                    + "; sending v" + wire + " anyway. Rebuild mcesp.exe and esp-agent.jar together.");
        else if (cfg.wire > Snapshot.WIRE_VERSION)
            Log.warn("The overlay understands a newer protocol (v" + cfg.wire + ") than this agent writes (v" + Snapshot.WIRE_VERSION + "): rebuild esp-agent.jar to use its features.");
        launchWorker(cfg, inst);
    }

    private static void launchWorker(AgentConfig cfg, Instrumentation inst) {
        Thread t = new Thread(new Worker(cfg, inst), "mcradar-agent");
        t.setDaemon(true);                       // never keeps the game from exiting
        t.setPriority(Thread.MIN_PRIORITY);
        worker = t;
        t.start();
    }

    /**
     * The agent is loaded a second time: the overlay was restarted (new session token, maybe another port or rate) while the game kept running.
     * Ignoring the load - what this used to do - left the agent sending the OLD token to the OLD port, so the new overlay dropped everything until
     * Minecraft was restarted. Now the running agent takes the new options over; a worker thread that died is started again.
     */
    private static void reload(String args, Instrumentation inst) {
        java.util.Map<String, String> given = new java.util.HashMap<String, String>();
        AgentConfig next = AgentConfig.parse(args, given);
        String changed = current.reloadFrom(next, given);
        Log.setVerbose(current.verbose);
        boolean dead = worker == null || !worker.isAlive();
        Log.info("Loaded again: " + (changed.isEmpty() ? "options unchanged" : changed) + (dead ? "; worker thread was not running, restarting it" : ""));
        if (dead) launchWorker(current, inst);
    }

    private static final class Worker implements Runnable {
        private final AgentConfig cfg;
        private final Instrumentation inst;
        Worker(AgentConfig cfg, Instrumentation inst) { this.cfg = cfg; this.inst = inst; }

        public void run() {
            DatagramSocket sock = null;
            try {
                sock = new DatagramSocket();
                InetAddress lo = InetAddress.getByAddress(new byte[] {127, 0, 0, 1});
                Probe probe = new Probe(inst, cfg);
                long nextDiscover = 0, backoff = 1500;
                long next = System.nanoTime();
                // wire v5: a random id for this agent run and a running datagram number (counted whether or not the send works, so a gap = a datagram that never arrived)
                final int session = new java.util.Random(System.nanoTime() ^ (long) System.identityHashCode(probe) * 0x9E3779B97F4A7C15L).nextInt();
                long seqCounter = 0;

                while (true) {
                    long t0 = System.currentTimeMillis();
                    long tSample = System.nanoTime();          // the sample instant, stamped on the wire (T line)
                    Snapshot snap;
                    try {
                        if (!probe.isReady() && t0 >= nextDiscover) {
                            boolean ok = probe.discover();
                            if (ok) backoff = 1500;
                            else { nextDiscover = System.currentTimeMillis() + backoff; backoff = Math.min(backoff * 2, 15000); }
                            tSample = System.nanoTime();
                        }
                        snap = probe.read();
                    } catch (Throwable t) {
                        snap = new Snapshot();
                        snap.status = "agent error: " + t;
                        Log.error("Error in tick: " + t);
                        probe.reset();
                    }
                    snap.sampleNs = tSample;
                    snap.hasSampleTime = true;
                    snap.wire = cfg.negotiatedWire();
                    snap.token = cfg.token;
                    snap.session = session;
                    snap.seq = ++seqCounter;
                    byte[] data = snap.encode();
                    try { sock.send(new DatagramPacket(data, data.length, lo, cfg.port)); } catch (Throwable ignored) { }

                    // Slow work goes AFTER the send, in the idle part of the period (see Probe.pollItemsIfDue).
                    try { probe.pollItemsIfDue(); } catch (Throwable t) { Log.error("Error polling items: " + t); probe.reset(); }

                    // Absolute schedule: sleep jitter does not accumulate, and a late wake-up is made up for by a shorter
                    // next sleep. The sample instants are stamped, so uneven spacing is harmless to the overlay.
                    final long periodNs = 1000000000L / Math.max(1, cfg.hz);     // read every tick: a reload may change hz
                    next += periodNs;
                    long nowNs = System.nanoTime();
                    if (next < nowNs - 4 * periodNs) next = nowNs;      // fell far behind (GC pause...): resynchronise, no burst
                    long ms = (next - nowNs) / 1000000L;
                    if (ms >= 1) Thread.sleep(ms);
                }
            } catch (InterruptedException ie) {
                Log.info("Worker interrupted; stopping.");
            } catch (Throwable t) {
                Log.info("Worker died: " + t);
            } finally {
                if (sock != null) sock.close();
            }
        }
    }
}
