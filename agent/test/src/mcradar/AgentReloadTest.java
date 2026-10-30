package mcradar;

import java.io.File;
import java.lang.instrument.Instrumentation;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.nio.charset.Charset;
import java.util.HashMap;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * A second agent load (the overlay was restarted: new session token, maybe a new port and rate) must reconfigure the running agent.
 * Before the fix the second load was ignored and the old token went to the old port forever (reproduced with a real Attach in agent_smoke.py "reload").
 * Runs the real Agent in this JVM with a fake Instrumentation (no classes loaded: the worker just reports "waiting").
 */
public final class AgentReloadTest {
    private static int fails = 0, checks = 0;
    private static void check(boolean ok, String what) { checks++; if (!ok) { fails++; System.out.println("FAIL: " + what); } }

    static Instrumentation fakeInst() {
        return (Instrumentation) Proxy.newProxyInstance(AgentReloadTest.class.getClassLoader(), new Class<?>[] {Instrumentation.class}, new InvocationHandler() {
            public Object invoke(Object p, Method m, Object[] a) {
                if (m.getName().equals("getAllLoadedClasses")) return new Class<?>[0];
                Class<?> r = m.getReturnType();
                return r == boolean.class ? Boolean.FALSE : r == long.class ? Long.valueOf(0) : null;
            }
        });
    }

    /** Receives for `ms`; returns {datagram count, set of tokens as "a,b"}. */
    static Object[] listen(DatagramSocket s, int ms) throws Exception {
        long end = System.currentTimeMillis() + ms; int n = 0; java.util.Set<String> toks = new java.util.TreeSet<String>();
        byte[] buf = new byte[65536]; Pattern k = Pattern.compile("^K (\\S+)$", Pattern.MULTILINE);
        s.setSoTimeout(100);
        while (System.currentTimeMillis() < end) {
            try {
                DatagramPacket p = new DatagramPacket(buf, buf.length); s.receive(p); n++;
                Matcher m = k.matcher(new String(p.getData(), 0, p.getLength(), Charset.forName("UTF-8")));
                toks.add(m.find() ? m.group(1) : "-");
            } catch (java.net.SocketTimeoutException ignored) { }
        }
        return new Object[] {n, toks.toString()};
    }

    public static void main(String[] args) throws Exception {
        File dir = new File(System.getProperty("java.io.tmpdir"), "esp-reload-test-" + System.nanoTime()); dir.mkdirs();
        String ta = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", tb = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
        DatagramSocket a = new DatagramSocket(0, InetAddress.getByName("127.0.0.1")), b = new DatagramSocket(0, InetAddress.getByName("127.0.0.1"));
        int pa = a.getLocalPort(), pb = b.getLocalPort();
        Instrumentation inst = fakeInst();
        String ex = dir.getPath().replace('\\', '/');

        Agent.agentmain("port=" + pa + ";hz=50;wire=5;token=" + ta + ";export=" + ex, inst);
        Object[] r1 = listen(a, 600);
        check((Integer) r1[0] >= 10 && r1[1].equals("[" + ta + "]"), "first load: datagrams with token A on port A, got " + r1[0] + " " + r1[1]);

        Agent.agentmain("port=" + pb + ";hz=100;wire=5;token=" + tb + ";export=" + ex, inst);
        Object[] r2 = listen(b, 600);
        check((Integer) r2[0] >= 20 && r2[1].equals("[" + tb + "]"), "second load: token B on port B (was: ignored), got " + r2[0] + " " + r2[1]);
        listen(a, 150);                                                     // flush what was in flight
        Object[] r3 = listen(a, 400);
        check((Integer) r3[0] == 0, "the old port gets nothing any more, got " + r3[0]);
        check((Integer) r2[0] > (Integer) r1[0] * 1.2, "hz 50 -> 100 took effect (" + r1[0] + " -> " + r2[0] + " datagrams per 600 ms)");

        // identical options: nothing changes, the stream keeps flowing
        Agent.agentmain("port=" + pb + ";hz=100;wire=5;token=" + tb + ";export=" + ex, inst);
        Object[] r4 = listen(b, 400);
        check((Integer) r4[0] >= 10 && r4[1].equals("[" + tb + "]"), "same options again: still streaming with token B");

        // an option that is not mentioned keeps its running value (hz stays 100); no token option removes the token
        Agent.agentmain("port=" + pb + ";wire=4;export=" + ex, inst);
        listen(b, 150);                                                     // datagrams queued before the reload still carry token B
        Object[] r5 = listen(b, 600);
        check((Integer) r5[0] >= 40 && r5[1].equals("[-]"), "no token / wire=4: tokenless datagrams, hz kept (" + r5[0] + ")");

        // the log says what happened and never prints a token
        String log = new String(java.nio.file.Files.readAllBytes(new File(dir, "esp_agent.log").toPath()), Charset.forName("UTF-8"));
        check(log.contains("Loaded again: port " + pa + " -> " + pb) && log.contains("session token replaced") && log.contains("options unchanged") && log.contains("session token removed"), "log describes each reload:\n" + log);
        check(!log.contains(ta) && !log.contains(tb), "the log never contains a token");

        // AgentConfig.reloadFrom on its own
        Map<String, String> given = new HashMap<String, String>();
        AgentConfig c = AgentConfig.parse("port=1000;hz=20;wire=5;token=" + ta);
        AgentConfig n = AgentConfig.parse("port=2000;export=/elsewhere;user=Bob", given);
        String ch = c.reloadFrom(n, given);
        check(c.port == 2000 && c.hz == 20 && ch.contains("export folder change ignored") && ch.contains("user change ignored"), "reloadFrom: " + ch);
        System.out.println(fails == 0 ? "ALL OK (" + checks + " checks)" : fails + " FAILURES of " + checks);
        System.exit(fails == 0 ? 0 : 1);
    }
}
