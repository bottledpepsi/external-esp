package mcradar;

import java.lang.reflect.InvocationTargetException;

/**
 * Command-line loader:  java -jar mcradar-agent.jar <pid> <path-to-this-jar> [agent options]
 *
 * Uses the official JVM Attach API (what jcmd / VisualVM / IDE debuggers use) to ask the running
 * Minecraft JVM to load this jar as a Java agent. It is done reflectively so this class
 * compiles on any JDK. Requires a JDK (module jdk.attach) for THIS process; the game only needs
 * to have attach enabled (default).
 */
public final class Attach {
    public static void main(String[] a) {
        if (a.length < 2) {
            System.err.println("usage: java -jar mcradar-agent.jar <pid> <agent-jar> [options]");
            System.exit(2);
        }
        System.exit(load(a[0], a[1], a.length > 2 ? a[2] : "", System.out, System.err));
    }

    /** Loads the agent into `pid`. Returns 0 on success, 1 if the attach failed, 3 if this Java has no Attach API. */
    static int load(String pid, String jar, String opts, java.io.PrintStream out, java.io.PrintStream err) {
        try {
            Class<?> vmc = Class.forName("com.sun.tools.attach.VirtualMachine");
            Object vm = vmc.getMethod("attach", String.class).invoke(null, pid);
            try {
                vmc.getMethod("loadAgent", String.class, String.class).invoke(vm, jar, opts);
            } finally {
                vmc.getMethod("detach").invoke(vm);
            }
            out.println("agent loaded into PID " + pid);
            return 0;
        } catch (ClassNotFoundException e) {
            err.println("This Java has no Attach API (jdk.attach). Use a full JDK (17+) as java_path.");
            return 3;
        } catch (InvocationTargetException e) {
            Throwable c = e.getCause() == null ? e : e.getCause();
            err.println("attach failed: " + c);
            return 1;
        } catch (Throwable t) {
            err.println("attach failed: " + t);
            return 1;
        }
    }
}
