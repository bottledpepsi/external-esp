package mcradar;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.List;

/**
 * Recreates the game camera exactly - without knowing any Minecraft name.
 *
 * Camera object: among AuxSearch's candidates, the one whose position stays within a block or so of the local
 * player's eye while two of its float fields track the player's yaw and pitch (in first person they are equal;
 * the camera rotation is the partial-tick interpolated value, so a few degrees of slack is allowed). The decision
 * needs the player to actually turn the head a little (a field that never changes cannot be told apart).
 *
 * Look-alikes: the game holds other objects that resemble a camera (the local player's render state has a position, a body yaw, a head-minus-body yaw
 * and a pitch). They are told apart by EXACTNESS, not by closeness to a threshold: candidates are ranked by how closely their two angles follow the
 * player's (the real camera is off by a fraction of a degree, a look-alike by several), the position only breaks ties. In third person the camera
 * must also lie on the look axis through the eye (behind it, or in front of it in "front" view); something at the player's feet does not.
 * And the pitch field is only trusted after it has moved: a field that stays put while the player does not look up or down cannot be told from a
 * constant, so the scanner waits (the overlay reconstructs the camera from the player meanwhile) instead of taking the best of a bad lot.
 *
 * FOV: the object holding the camera (the renderer) has a method taking (Camera, float partialTick, boolean) or
 * (float, boolean) and returning the field of view in degrees - the very method the game calls to build its
 * projection, so it already includes the FOV option, sprint/speed/bow effects, underwater and so on.
 *
 * Third person: the camera position is no longer at the eye (> 0.8 blocks away); "front" view is detected by the
 * camera yaw being ~180 degrees off the player's.
 */
final class CameraScanner {
    private static final class Cand {
        final FieldPath path;
        final List<Probe.PosCand> pos;
        final List<Acc> floats;
        final double[] posErr; final double[] yawErr; final double[] pitchErr;
        final double[] axisErr; final int[] axisN;      // per position candidate: how far off the look axis it lies when it is not at the eye
        final float[] fmin; final float[] fmax;
        int n;
        Cand(FieldPath path, List<Probe.PosCand> pos, List<Acc> floats) {
            this.path = path; this.pos = pos; this.floats = floats;
            posErr = new double[pos.size()];
            axisErr = new double[pos.size()]; axisN = new int[pos.size()];
            yawErr = new double[floats.size()]; pitchErr = new double[floats.size()];
            fmin = new float[floats.size()]; fmax = new float[floats.size()];
            reset();
        }
        void reset() {
            n = 0;
            for (int i = 0; i < posErr.length; i++) { posErr[i] = 0; axisErr[i] = 0; axisN[i] = 0; }
            for (int i = 0; i < yawErr.length; i++) { yawErr[i] = 0; pitchErr[i] = 0; fmin[i] = Float.MAX_VALUE; fmax[i] = -Float.MAX_VALUE; }
        }
    }

    private final Probe p;
    private final List<Cand> cands = new ArrayList<Cand>();
    private long windowStart = System.currentTimeMillis();

    // decided
    boolean decided;
    FieldPath camPath;
    Probe.PosCand posAcc;                 // package-private: StructureDump records what was decided
    Acc yawAcc, pitchAcc;
    FieldPath holderPath;
    Method fovMethod;
    boolean fovTakesCam;
    private float lastFov = Float.NaN;
    private long lastFovAt;
    String describe = "camera: not found yet";
    /** True while the best candidate is known but its pitch field has not moved yet: the player has to look up or down once. */
    volatile boolean waitingForPitch;

    // Strategy chain. Camera: 0 strict value match (needs head movement, mean errors <= 5.5 blocks / 20 / 12 degrees); 1 the same with looser limits, tried
    // when the window has run RELAXED_AFTER_MS without a decision. FOV: 0 a method on the camera's holder, 1..n the same on its ancestors up to the root;
    // none found is retried every FOV_RETRY_MS (a chunk race at the moment of the decision must not mean "configured FOV" for the rest of the session).
    static volatile long RELAXED_AFTER_MS = 15000, FOV_RETRY_MS = 5000;
    String matchStrategy = "value-match", fovStrategy = "none";
    private long lastFovSearch;

    CameraScanner(Probe p) { this.p = p; }

    int cands() { return cands.size(); } // for tests: how many objects were accepted as candidates

    void init(List<FieldPath> paths) {
        cands.clear();
        decided = false;
        windowStart = System.currentTimeMillis();
        for (FieldPath fp : paths) {
            try {
                Object o = fp.follow();
                if (o == null) continue;
                Class<?> c = o.getClass();
                List<Probe.PosCand> pos = p.posCandidates(c);
                List<Acc> floats = new ArrayList<Acc>();
                for (Class<?> k = c; k != null && k != Object.class; k = k.getSuperclass())
                    for (Field f : k.getDeclaredFields())
                        if (!Modifier.isStatic(f.getModifiers()) && f.getType() == float.class) { Acc a = Acc.of(f); if (a != null) floats.add(a); }
                if (!pos.isEmpty() && floats.size() >= 2) cands.add(new Cand(fp, pos, floats));
            } catch (Throwable ignored) { }
        }
        windowStart = System.currentTimeMillis();
        Log.info("CameraScanner: " + cands.size() + " candidate object(s).");
    }

    private static double angDiff(double a, double b) {
        double d = (a - b) % 360.0;
        if (d > 180) d -= 360; if (d < -180) d += 360;
        return Math.abs(d);
    }

    /** Feed one tick of "ground truth" (the local player's eye + angles); decides once enough evidence exists. */
    void observe(double[] eye, float yaw, float pitch) {
        if (decided || cands.isEmpty()) return;
        double[] v = new double[3];
        boolean enough = false;
        for (Cand c : cands) {
            Object o = c.path.follow();
            if (o == null) continue;
            c.n++;
            for (int i = 0; i < c.pos.size(); i++) {
                double e = 1e3;
                if (c.pos.get(i).read(o, v)) e = Math.sqrt((v[0] - eye[0]) * (v[0] - eye[0]) + (v[1] - eye[1]) * (v[1] - eye[1]) + (v[2] - eye[2]) * (v[2] - eye[2]));
                c.posErr[i] += Math.min(e, 1e3);
                if (e > 0.8 && e < 12) {                          // not at the eye: a third-person camera lies on the look axis through the eye
                    double yr = Math.toRadians(yaw), pr = Math.toRadians(pitch);
                    double lx = -Math.sin(yr) * Math.cos(pr), ly = -Math.sin(pr), lz = Math.cos(yr) * Math.cos(pr);   // Minecraft: yaw 0 looks along +z
                    double cos = ((v[0] - eye[0]) * lx + (v[1] - eye[1]) * ly + (v[2] - eye[2]) * lz) / e;
                    c.axisErr[i] += 1.0 - Math.abs(cos);
                    c.axisN[i]++;
                }
            }
            for (int i = 0; i < c.floats.size(); i++) {
                float f = c.floats.get(i).getFloat(o);
                if (Float.isNaN(f) || Float.isInfinite(f)) { c.yawErr[i] += 360; c.pitchErr[i] += 180; continue; }
                c.yawErr[i] += Math.min(angDiff(f, yaw), angDiff(f, yaw + 180.0));
                c.pitchErr[i] += Math.min(Math.abs(f - pitch), Math.abs(f + pitch));
                c.fmin[i] = Math.min(c.fmin[i], f); c.fmax[i] = Math.max(c.fmax[i], f);
            }
            if (c.n >= 45) enough = true;
        }
        if (enough && decide(false)) return;
        if (System.currentTimeMillis() - windowStart > RELAXED_AFTER_MS && decide(true)) return;      // strategy 1: looser limits, same evidence
        if (System.currentTimeMillis() - windowStart > Math.max(25000, RELAXED_AFTER_MS * 2)) {           // stale statistics: start a fresh window
            for (Cand c : cands) c.reset();
            windowStart = System.currentTimeMillis();
        }
    }

    private boolean decide(boolean relaxed) {
        final double maxPos = relaxed ? 8 : 5.5, maxYaw = relaxed ? 35 : 20, maxPitch = relaxed ? 20 : 12;
        final int minN = relaxed ? 100 : 45;
        final double maxAxis = 0.12;                       // mean (1 - |cos|) of the angle to the look axis: ~28 degrees
        Cand best = null; int bp = -1, by = -1, bpi = -1; boolean bestPitchMoved = false; double bestScore = 1e18;
        for (Cand c : cands) {
            if (c.n < minN) continue;
            int ip = -1; double mp = 1e18;
            for (int i = 0; i < c.posErr.length; i++) { double m = c.posErr[i] / c.n; if (m < mp) { mp = m; ip = i; } }
            if (ip < 0 || mp > maxPos) continue;       // first person ~0, third person ~4 (F5 distance)
            if (c.axisN[ip] > c.n / 4 && c.axisErr[ip] / c.axisN[ip] > maxAxis) continue;    // away from the eye but not on the look axis: not a camera
            int iy = -1; double my = 1e18;
            for (int i = 0; i < c.floats.size(); i++) {
                if (c.fmax[i] - c.fmin[i] < 3f) continue;                  // yaw must have moved to be distinguishable
                double m = c.yawErr[i] / c.n;
                if (m < my) { my = m; iy = i; }
            }
            if (iy < 0 || my > maxYaw) continue;
            // the pitch field: the closest match among ALL other floats, moved or not; whether it moved is judged below (a constant that happens
            // to sit near the current pitch must not be mistaken for it, and the real one must not be thrown out for not having moved yet)
            int ipi = -1; double mpi = 1e18;
            for (int i = 0; i < c.floats.size(); i++) {
                if (i == iy) continue;
                double m = c.pitchErr[i] / c.n;
                if (m < mpi) { mpi = m; ipi = i; }
            }
            if (ipi < 0 || mpi > maxPitch) continue;
            double score = my + mpi + mp * 0.5;            // degrees of angle error dominate; the position only breaks ties
            if (score < bestScore) { bestScore = score; best = c; bp = ip; by = iy; bpi = ipi; bestPitchMoved = c.fmax[ipi] - c.fmin[ipi] >= 1f; }
        }
        if (best == null) return false;
        if (!bestPitchMoved) {                             // the leader's pitch is unproven: wait for the player to look up or down
            if (!waitingForPitch) Log.info("Camera candidate found (" + best.path.describe() + "), waiting for the pitch to change: look up or down a little.");
            waitingForPitch = true;
            return false;
        }
        waitingForPitch = false;
        camPath = best.path; posAcc = best.pos.get(bp); yawAcc = best.floats.get(by); pitchAcc = best.floats.get(bpi);
        decided = true;
        matchStrategy = relaxed ? "value-match (relaxed)" : "value-match";
        lastFovSearch = System.currentTimeMillis();
        findFov();
        buildDescribe();
        Log.info("Camera found. " + describe);
        return true;
    }

    private void buildDescribe() {
        describe = "camera: " + camPath.describe() + " | yaw=" + yawAcc.describe() + " | pitch=" + pitchAcc.describe() + " | match=" + matchStrategy
                + " | fov=" + (fovMethod == null ? "unavailable (using configured FOV)" : fovMethod.getDeclaringClass().getName() + "." + fovMethod.getName() + " [" + fovStrategy + "]");
    }

    private void findFov() {
        try {
            Acc[] steps = camPath.steps;
            if (steps.length < 2) return;
            Object cam = camPath.follow();
            if (cam == null) return;
            for (int len = steps.length - 1; len >= 1; len--) {                // the holder first, then each ancestor up to the root
                List<Acc> head = new ArrayList<Acc>();
                for (int i = 0; i < len; i++) head.add(steps[i]);
                FieldPath hp = new FieldPath(head);
                Object holder = hp.follow();
                if (holder == null) continue;
                if (searchFov(holder, cam)) {
                    holderPath = hp;
                    fovStrategy = len == steps.length - 1 ? "holder" : "ancestor " + (steps.length - 1 - len) + " up";
                    return;
                }
            }
            // Not on the holder or any ancestor: in newer versions the method that builds the projection lives on a SIBLING (the game object has both the
            // entity-render dispatcher that holds the camera and the game renderer that computes the FOV). Try the objects one field below each ancestor.
            int tried = 0;
            for (int len = steps.length - 1; len >= 1 && tried < 80; len--) {
                List<Acc> head = new ArrayList<Acc>();
                for (int i = 0; i < len; i++) head.add(steps[i]);
                Object holder = new FieldPath(head).follow();
                if (holder == null) continue;
                for (Acc a : p.refFieldsOf(holder.getClass())) {
                    if (tried++ >= 80) break;
                    Object sib = a.getObj(holder);
                    if (sib == null || sib == cam || Probe.isJdkish(sib.getClass()) || Probe.isContainer(sib)) continue;
                    if (searchFov(sib, cam)) {
                        List<Acc> sp = new ArrayList<Acc>(head);
                        sp.add(a);
                        holderPath = new FieldPath(sp);
                        fovStrategy = "sibling of ancestor " + (steps.length - 1 - len) + " up (" + a.f.getName() + ")";
                        return;
                    }
                }
            }
        } catch (Throwable t) { Log.info("FOV method search failed: " + t); }
    }

    /** A method of `holder` (any visibility) taking (Camera, float, boolean) or (float, boolean) that returns a plausible FOV. */
    private boolean searchFov(Object holder, Object cam) {
        for (Class<?> k = holder.getClass(); k != null && k != Object.class; k = k.getSuperclass()) {
            for (Method m : k.getDeclaredMethods()) {
                Class<?> rt = m.getReturnType();
                if (rt != double.class && rt != float.class) continue;
                Class<?>[] pt = m.getParameterTypes();
                boolean withCam = pt.length == 3 && pt[0].isAssignableFrom(cam.getClass()) && pt[1] == float.class && pt[2] == boolean.class;
                boolean noCam = pt.length == 2 && pt[0] == float.class && pt[1] == boolean.class;
                if (!withCam && !noCam) continue;
                if (Modifier.isStatic(m.getModifiers())) continue;
                try {
                    m.setAccessible(true);
                    Object r = withCam ? m.invoke(holder, cam, 1.0f, Boolean.TRUE) : m.invoke(holder, 1.0f, Boolean.TRUE);
                    double f = r instanceof Number ? ((Number) r).doubleValue() : Double.NaN;
                    if (f >= 20 && f <= 170) { fovMethod = m; fovTakesCam = withCam; return true; }
                } catch (Throwable ignored) { }
            }
        }
        return false;
    }

    /** A new world: statistics gathered before may describe objects that no longer exist; start the window again. A decided camera stays decided. */
    void onWorldChange() {
        if (decided) return;
        for (Cand c : cands) c.reset();
        windowStart = System.currentTimeMillis();
    }

    /** Current camera, or null if not (yet) identified. `eye`/`playerYaw` classify first / third person. */
    Snapshot.Cam read(double[] eye, float playerYaw) {
        if (!decided) return null;
        try {
            Object cam = camPath.follow();
            if (cam == null) return null;
            double[] v = new double[3];
            if (!posAcc.read(cam, v)) return null;
            float yaw = yawAcc.getFloat(cam), pitch = pitchAcc.getFloat(cam);
            if (Float.isNaN(yaw) || Float.isNaN(pitch)) return null;

            if (fovMethod == null && System.currentTimeMillis() - lastFovSearch > FOV_RETRY_MS) {
                lastFovSearch = System.currentTimeMillis();
                findFov();
                if (fovMethod != null) { buildDescribe(); Log.info("FOV method found on a later try. " + describe); }
            }
            float fov = Float.NaN;
            if (fovMethod != null) {
                try {
                    Object holder = holderPath.follow();
                    Object r = holder == null ? null : (fovTakesCam ? fovMethod.invoke(holder, cam, 1.0f, Boolean.TRUE) : fovMethod.invoke(holder, 1.0f, Boolean.TRUE));
                    float f = r instanceof Number ? ((Number) r).floatValue() : Float.NaN;
                    if (f >= 20 && f <= 170) { fov = f; lastFov = f; lastFovAt = System.currentTimeMillis(); }
                } catch (Throwable ignored) { }
                if (Float.isNaN(fov) && System.currentTimeMillis() - lastFovAt < 2000) fov = lastFov;   // brief glitches (chunk race)
            }
            double dist = Math.sqrt((v[0] - eye[0]) * (v[0] - eye[0]) + (v[1] - eye[1]) * (v[1] - eye[1]) + (v[2] - eye[2]) * (v[2] - eye[2]));
            int mode = dist < 0.8 ? 1 : (!Float.isNaN(playerYaw) && angDiff(yaw, playerYaw) > 90 ? 3 : 2);
            return new Snapshot.Cam(v[0], v[1], v[2], yaw, pitch, fov, mode);
        } catch (Throwable t) { return null; }
    }
}
