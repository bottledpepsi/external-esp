package mcradar;

import fake.game.Fakes;
import fake.game.Fakes.Cam;
import fake.game.Fakes.Renderer;
import fake.game.Fakes.Root;
import fake.game.Fakes.State;
import java.util.ArrayList;
import java.util.List;

/**
 * Camera identification when the game also holds a look-alike: the local player's RENDER STATE (position at the feet, body yaw, head-minus-body yaw,
 * pitch). This is the structure of the Minecraft 26.x session where the overlay's boxes sat ~7 degrees off: the scanner chose
 * yaw = bodyRot, pitch = yRot (the head-body difference, which is small and wanders, so it "matches" a pitch near the horizon) from the render state,
 * because the real Camera was thrown out for not having had a pitch that moved by a degree yet.
 *
 * The scanner is fed the same ground truth the agent feeds it (the local player's eye, yaw, pitch) and must pick the real camera, in first and third
 * person, and must wait rather than guess when the real camera's pitch has not moved yet.
 */
public final class CameraDecoyTest {
    private static int fails = 0, checks = 0;

    private static void check(boolean ok, String what) {
        checks++;
        if (!ok) { fails++; System.out.println("FAIL: " + what); }
    }

    private static FieldPath path(String field) throws Exception {
        List<Acc> steps = new ArrayList<Acc>();
        steps.add(Acc.of(Fakes.Root.class.getField("INSTANCE")));
        steps.add(Acc.of(Fakes.Root.class.getField(field)));
        return new FieldPath(steps);
    }

    private static CameraScanner scanner() throws Exception {
        Root.INSTANCE = new Root();   // initialises the class (the agent reads statics without running initialisers) and gives every scenario fresh objects
        Probe p = new Probe(null, AgentConfig.parse("port=47615"));
        CameraScanner s = new CameraScanner(p);
        List<FieldPath> paths = new ArrayList<FieldPath>();
        paths.add(path("state"));     // the decoy is listed first on purpose: an order-dependent tie must not matter
        paths.add(path("camera"));
        s.init(paths);
        return s;
    }

    /** One game tick: the player is at (10, 64, -3) with `yaw`/`pitch`; the camera is `back` blocks behind the eye (0 = first person). */
    private static double[] tick(double yaw, double pitch, double back) {
        final double ex = 10, ey = 65.62, ez = -3;
        Cam c = Root.INSTANCE.camera;
        double r = Math.toRadians(yaw), pr = Math.toRadians(pitch);
        c.x = ex + Math.sin(r) * Math.cos(pr) * back; c.y = ey + Math.sin(pr) * back; c.z = ez - Math.cos(r) * Math.cos(pr) * back;   // behind the eye on the look axis (Minecraft: yaw 0 looks along +z)
        c.yRot = (float) (yaw + 0.15 * Math.sin(yaw));      // partial-tick interpolation: a fraction of a degree off the tick value
        c.xRot = (float) (pitch + 0.1 * Math.cos(pitch));
        State s = Root.INSTANCE.state;
        s.x = ex; s.y = ey - 1.62; s.z = ez;
        s.bodyRot = (float) (yaw * 0.75);                    // the body follows the head with a lag
        s.yRot = (float) (yaw - s.bodyRot);                  // head relative to body: small, wanders with the yaw
        s.xRot = (float) pitch;
        return new double[] {ex, ey, ez};
    }

    private static String what(CameraScanner s) {
        return s.decided ? s.camPath.describe() + " yaw=" + s.yawAcc.f.getName() + " pitch=" + s.pitchAcc.f.getName() : "(not decided)";
    }

    public static void main(String[] args) throws Exception {
        // ---- first person: yaw sweeps +-40 degrees, pitch is held at ~4 degrees (moves by under a degree) ----
        {
            CameraScanner s = scanner();
            check(s.cands() == 2, "both objects are candidates, got " + s.cands());
            for (int t = 0; t < 200; t++) {
                double yaw = 40 * Math.sin(t * 0.05), pitch = 4 + 0.2 * Math.sin(t * 0.3);
                double[] eye = tick(yaw, pitch, 0);
                s.observe(eye, (float) yaw, (float) pitch);
            }
            check(!s.decided || s.camPath.steps[1].f.getName().equals("camera"),
                  "pitch has not moved: the render state must not be taken for the camera, got " + what(s));
            // now the player looks up and down: the real camera is identified
            for (int t = 200; t < 520; t++) {
                double yaw = 40 * Math.sin(t * 0.05), pitch = 4 + 25 * Math.sin((t - 200) * 0.06);
                double[] eye = tick(yaw, pitch, 0);
                s.observe(eye, (float) yaw, (float) pitch);
            }
            check(s.decided && s.camPath.steps[1].f.getName().equals("camera") && s.yawAcc.f.getName().equals("yRot") && s.pitchAcc.f.getName().equals("xRot"),
                  "first person: the camera with its own yRot/xRot, got " + what(s));
        }

        // ---- first person, the player moves pitch from the start: decided, and decided right ----
        {
            CameraScanner s = scanner();
            for (int t = 0; t < 200 && !s.decided; t++) {
                double yaw = 40 * Math.sin(t * 0.05), pitch = 20 * Math.sin(t * 0.07);
                double[] eye = tick(yaw, pitch, 0);
                s.observe(eye, (float) yaw, (float) pitch);
            }
            check(s.decided && s.camPath.steps[1].f.getName().equals("camera") && s.yawAcc.f.getName().equals("yRot") && s.pitchAcc.f.getName().equals("xRot"),
                  "first person, both angles moving: the real camera, got " + what(s));
        }

        // ---- third person (the camera 4 blocks behind the eye): the position no longer points at the camera, the angles do ----
        {
            CameraScanner s = scanner();
            for (int t = 0; t < 300 && !s.decided; t++) {
                double yaw = 40 * Math.sin(t * 0.05), pitch = 20 * Math.sin(t * 0.07);
                double[] eye = tick(yaw, pitch, 4.0);
                s.observe(eye, (float) yaw, (float) pitch);
            }
            check(s.decided && s.camPath.steps[1].f.getName().equals("camera") && s.yawAcc.f.getName().equals("yRot") && s.pitchAcc.f.getName().equals("xRot"),
                  "third person: still the real camera, got " + what(s));
        }

        // ---- no real camera at all: nothing is invented from the decoy's constants; the result is whatever the loose match gives, but never a crash ----
        {
            Root.INSTANCE = new Root();
            CameraScanner s = new CameraScanner(new Probe(null, AgentConfig.parse("port=47615")));
            List<FieldPath> paths = new ArrayList<FieldPath>();
            paths.add(path("state"));
            s.init(paths);
            for (int t = 0; t < 100; t++) { double[] eye = tick(10, 0, 0); s.observe(eye, 10f, 0f); }
            check(!s.decided, "a candidate whose angles never move cannot be identified, got " + what(s));
        }

        // ---- the FOV method on a sibling of the camera's holder (here: the root's other field) ----
        {
            CameraScanner s = scanner();
            for (int t = 0; t < 300 && !s.decided; t++) {
                double yaw = 40 * Math.sin(t * 0.05), pitch = 20 * Math.sin(t * 0.07);
                double[] eye = tick(yaw, pitch, 0);
                s.observe(eye, (float) yaw, (float) pitch);
            }
            check(s.decided && s.fovMethod != null && s.fovStrategy.startsWith("sibling"), "FOV found on a sibling object, strategy: " + s.fovStrategy);
            double[] eye = tick(10, 5, 0);
            Snapshot.Cam c = s.read(eye, 10f);
            check(c != null && Math.abs(c.fov - 77f) < 0.01, "the snapshot carries the game's FOV, got " + (c == null ? "no camera" : c.fov + ""));
            Renderer.fov = 95f;                                 // sprinting / a speed effect changes it: the next read follows
            c = s.read(eye, 10f);
            check(c != null && Math.abs(c.fov - 95f) < 0.01, "FOV follows the game, got " + (c == null ? "no camera" : c.fov + ""));
            Renderer.fov = 77f;
        }

        System.out.println(fails == 0 ? "ALL OK (" + checks + " checks)" : fails + " FAILURES of " + checks);
        System.exit(fails == 0 ? 0 : 1);
    }
}
