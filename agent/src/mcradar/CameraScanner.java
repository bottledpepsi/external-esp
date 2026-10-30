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
        final float[] fmin; final float[] fmax;
        int n;
        Cand(FieldPath path, List<Probe.PosCand> pos, List<Acc> floats) {
            this.path = path; this.pos = pos; this.floats = floats;
            posErr = new double[pos.size()];
            yawErr = new double[floats.size()]; pitchErr = new double[floats.size()];
            fmin = new float[floats.size()]; fmax = new float[floats.size()];
            reset();
        }
        void reset() {
            n = 0;
            for (int i = 0; i < posErr.length; i++) posErr[i] = 0;
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

    CameraScanner(Probe p) { this.p = p; }

    void init(List<FieldPath> paths) {
        cands.clear();
        decided = false;
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
        if (enough && decide()) return;
        if (System.currentTimeMillis() - windowStart > 25000) {           // stale statistics: start a fresh window
            for (Cand c : cands) c.reset();
            windowStart = System.currentTimeMillis();
        }
    }

    private boolean decide() {
        Cand best = null; int bp = -1, by = -1, bpi = -1; double bestScore = 1e18;
        for (Cand c : cands) {
            if (c.n < 45) continue;
            int ip = -1; double mp = 1e18;
            for (int i = 0; i < c.posErr.length; i++) { double m = c.posErr[i] / c.n; if (m < mp) { mp = m; ip = i; } }
            if (ip < 0 || mp > 5.5) continue;       // first person ~0, third person ~4 (F5 distance)
            int iy = -1; double my = 1e18;
            for (int i = 0; i < c.floats.size(); i++) {
                if (c.fmax[i] - c.fmin[i] < 3f) continue;                  // yaw must have moved to be distinguishable
                double m = c.yawErr[i] / c.n;
                if (m < my) { my = m; iy = i; }
            }
            if (iy < 0 || my > 20) continue;
            int ipi = -1; double mpi = 1e18;
            for (int i = 0; i < c.floats.size(); i++) {
                if (i == iy || c.fmax[i] - c.fmin[i] < 1f) continue;
                double m = c.pitchErr[i] / c.n;
                if (m < mpi) { mpi = m; ipi = i; }
            }
            if (ipi < 0 || mpi > 12) continue;
            double score = mp + my / 10.0 + mpi / 5.0;
            if (score < bestScore) { bestScore = score; best = c; bp = ip; by = iy; bpi = ipi; }
        }
        if (best == null) return false;
        camPath = best.path; posAcc = best.pos.get(bp); yawAcc = best.floats.get(by); pitchAcc = best.floats.get(bpi);
        decided = true;
        findFov();
        describe = "camera: " + camPath.describe() + " | yaw=" + yawAcc.describe() + " | pitch=" + pitchAcc.describe()
                + " | fov=" + (fovMethod == null ? "unavailable (using configured FOV)" : fovMethod.getDeclaringClass().getName() + "." + fovMethod.getName());
        Log.info("Camera found. " + describe);
        return true;
    }

    private void findFov() {
        try {
            if (camPath.steps.length < 2) return;
            Acc[] steps = camPath.steps;
            List<Acc> head = new ArrayList<Acc>();
            for (int i = 0; i < steps.length - 1; i++) head.add(steps[i]);
            holderPath = new FieldPath(head);
            Object holder = holderPath.follow(), cam = camPath.follow();
            if (holder == null || cam == null) return;
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
                        if (f >= 20 && f <= 170) { fovMethod = m; fovTakesCam = withCam; return; }
                    } catch (Throwable ignored) { }
                }
            }
        } catch (Throwable t) { Log.info("FOV method search failed: " + t); }
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
