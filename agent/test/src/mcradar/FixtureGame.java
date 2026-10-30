package mcradar;

import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.io.StringWriter;
import java.lang.instrument.Instrumentation;
import java.lang.reflect.Array;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.lang.reflect.Proxy;
import java.net.URI;
import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.IdentityHashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import javax.tools.Diagnostic;
import javax.tools.DiagnosticCollector;
import javax.tools.FileObject;
import javax.tools.ForwardingJavaFileManager;
import javax.tools.JavaCompiler;
import javax.tools.JavaFileObject;
import javax.tools.SimpleJavaFileObject;
import javax.tools.StandardJavaFileManager;
import javax.tools.ToolProvider;

/**
 * A fake Minecraft rebuilt from an esp_structure.json (docs/fixtures.md): the classes, fields, generic types and super classes are generated as
 * Java source with exactly the dumped names, compiled in memory, and wired into an object graph along the dumped paths. The few methods the agent
 * calls (look vector, health, FOV, item id) are generated too and answer from a state this class keeps (hook()). The agent's own Probe then runs
 * against it in this JVM through a fake Instrumentation - no Minecraft, no -javaagent.
 *
 * Limits (also in docs/fixtures.md): only the structure on the dumped paths exists, so decoys and the real game's size are missing; field values
 * are invented (three players, three items per item class); "final" is dropped so the scene can be set by reflection.
 */
public final class FixtureGame {
    public final String name;
    public final boolean real;                          // fixture.txt says "real: true" - a dump from a real Minecraft, not from a fake game
    public final Map<String, Object> doc;
    final Set<String> variants;
    private final Map<String, Map<String, Object>> defs = new LinkedHashMap<String, Map<String, Object>>();
    private final Map<String, String> libKinds = new HashMap<String, String>();
    private final Map<String, Class<?>> cls = new LinkedHashMap<String, Class<?>>();
    private ClassLoader loader;

    // scene state
    private final Map<String, Object> built = new HashMap<String, Object>();          // path prefix -> object
    private final List<Object> players = new ArrayList<Object>();
    private final IdentityHashMap<Object, double[]> state = new IdentityHashMap<Object, double[]>();   // x, y, z, yaw, pitch
    private final List<Object> itemEntities = new ArrayList<Object>();
    private Object camera, fovHolder;
    private final List<String> names = Arrays.asList("Alice", "Bob", "Carol");
    static final IdentityHashMap<Object, String> ID_OF = new IdentityHashMap<Object, String>();
    static final IdentityHashMap<Object, float[]> LOOK = new IdentityHashMap<Object, float[]>();
    static volatile double fovNow = 70.0;
    static FixtureGame current;

    // ---- controls for tests that need the game to misbehave
    private volatile Float forceYaw, forcePitch;          // the local player looks exactly there (pitch -90: no horizontal look vector)
    private volatile boolean lookNaN, cameraNaN;          // the look method returns NaN / the camera object's yaw is NaN
    private final List<Object> stacks = new ArrayList<Object>();
    private final List<String> stackIds = new ArrayList<String>();
    private final List<Integer> stackCounts = new ArrayList<Integer>();
    private final List<Object[]> stackRefs = new ArrayList<Object[]>();     // {stack, field, holder}: the first id reference of each stack, nulled on an empty stack

    public void forceLook(Float yaw, Float pitch) { forceYaw = yaw; forcePitch = pitch; }
    public void lookNaN(boolean on) { lookNaN = on; }
    public void cameraNaN(boolean on) { cameraNaN = on; }
    public int stackCount() { return stacks.size(); }
    /** ItemStack.EMPTY: count 0, no item - the id method throws, like the real one on the empty stack. */
    public void emptyStack(int i) throws Exception {
        ID_OF.remove(stacks.get(i)); setCount(stacks.get(i), 0);
        Object[] r = stackRefs.get(i);
        if (r != null) ((Field) r[1]).set(r[0], null);                  // item == null, like ItemStack.EMPTY
    }
    public void fillStack(int i, String id, int count) throws Exception {
        ID_OF.put(stacks.get(i), id); setCount(stacks.get(i), count);
        Object[] r = stackRefs.get(i);
        if (r != null) { ((Field) r[1]).set(r[0], r[2]); ID_OF.put(r[2], id); }
    }

    private FixtureGame(String name, boolean real, Map<String, Object> doc, Set<String> variants) {
        this.name = name; this.real = real; this.doc = doc; this.variants = variants;
    }

    public static FixtureGame load(File dir, Set<String> variants) throws Exception {
        String json = new String(java.nio.file.Files.readAllBytes(new File(dir, "structure.json").toPath()), Charset.forName("UTF-8"));
        boolean real = false;
        File meta = new File(dir, "fixture.txt");
        if (meta.exists()) real = Pattern.compile("^real:\\s*true\\s*$", Pattern.MULTILINE).matcher(new String(java.nio.file.Files.readAllBytes(meta.toPath()), Charset.forName("UTF-8"))).find();
        FixtureGame g = new FixtureGame(dir.getName(), real, MiniJson.obj(MiniJson.parse(json)), variants);
        g.generate();
        g.buildScene();
        current = g;
        return g;
    }

    // ================================================================== class generation

    private static final Pattern IDENT = Pattern.compile("[A-Za-z_$][\\w$]*(?:\\.[A-Za-z_$][\\w$]*)*");

    private static String src(String binary) {                 // binary class name -> source type
        if (binary.startsWith("[")) {
            int d = 0;
            while (binary.charAt(d) == '[') d++;
            String base;
            char k = binary.charAt(d);
            switch (k) {
                case 'L': base = src(binary.substring(d + 1, binary.length() - 1)); break;
                case 'D': base = "double"; break; case 'F': base = "float"; break; case 'I': base = "int"; break; case 'J': base = "long"; break;
                case 'Z': base = "boolean"; break; case 'B': base = "byte"; break; case 'S': base = "short"; break; default: base = "char";
            }
            StringBuilder b = new StringBuilder(base);
            for (int i = 0; i < d; i++) b.append("[]");
            return b.toString();
        }
        return binary.startsWith("java.") || binary.startsWith("javax.") ? binary.replace('$', '.') : binary;
    }

    private static String genericSrc(String g) {
        Matcher m = IDENT.matcher(g);
        StringBuffer b = new StringBuffer();
        while (m.find()) m.appendReplacement(b, Matcher.quoteReplacement(m.group().startsWith("java.") ? m.group().replace('$', '.') : m.group()));
        m.appendTail(b);
        return b.toString();
    }

    private final Map<String, List<String[]>> roleMethods = new HashMap<String, List<String[]>>();    // class -> {role, name, ret, params|...}
    private final Set<String> textClasses = new HashSet<String>();

    private void addRole(String clazz, String role, Map<String, Object> m) {
        if (m == null || m.isEmpty() || clazz == null) return;
        StringBuilder ps = new StringBuilder();
        for (Object p : MiniJson.arr(m.get("params"))) { if (ps.length() > 0) ps.append('|'); ps.append(p); }
        List<String[]> l = roleMethods.get(clazz);
        if (l == null) { l = new ArrayList<String[]>(); roleMethods.put(clazz, l); }
        for (String[] have : l) if (have[1].equals(MiniJson.str(m.get("name"))) && have[3].equals(ps.toString())) return;     // several dumped classes share one declaring class
        l.add(new String[] {role, MiniJson.str(m.get("name")), MiniJson.str(m.get("ret")), ps.toString()});
    }

    private void collectRoles() {
        Map<String, Object> roles = MiniJson.obj(doc.get("roles"));
        for (Object o : MiniJson.arr(roles.get("playerClasses"))) {
            Map<String, Object> pc = MiniJson.obj(o);
            Map<String, Object> look = MiniJson.obj(pc.get("look")), health = MiniJson.obj(pc.get("health"));
            addRole(MiniJson.str(look.get("class")), "look", look);
            addRole(MiniJson.str(health.get("class")), "health", health);
        }
        Map<String, Object> cam = MiniJson.obj(roles.get("camera"));
        Map<String, Object> fov = MiniJson.obj(cam.get("fov"));
        addRole(MiniJson.str(fov.get("class")), "fov", fov);
        for (Object o : MiniJson.arr(roles.get("items"))) {
            Map<String, Object> it = MiniJson.obj(o);
            Map<String, Object> key = MiniJson.obj(it.get("keyMethod"));
            if (!variants.contains("nokey")) addRole(MiniJson.str(key.get("class")), "key", key);
            for (Object r : MiniJson.arr(it.get("idRefs"))) {
                Map<String, Object> ref = MiniJson.obj(r);
                Map<String, Object> dm = MiniJson.obj(ref.get("descMethod"));
                if (!variants.contains("nokey")) addRole(MiniJson.str(dm.get("class")), "desc", dm);
                if (Boolean.TRUE.equals(ref.get("idText"))) textClasses.add(MiniJson.str(ref.get("class")));
            }
        }
    }

    private boolean loadable(String n) {
        if (n.startsWith("java.") || n.startsWith("javax.")) return true;
        try { Class.forName(n, false, FixtureGame.class.getClassLoader()); return true; } catch (Throwable t) { return false; }
    }

    private String classSource(Map<String, Object> d, boolean erased) {
        String n = MiniJson.str(d.get("name"));
        int dot = n.lastIndexOf('.');
        StringBuilder b = new StringBuilder();
        if (dot > 0) b.append("package ").append(n, 0, dot).append(";\n");
        String simple = n.substring(dot + 1);
        String kind = MiniJson.str(d.get("kind"));
        b.append("@SuppressWarnings({\"unchecked\", \"rawtypes\"})\npublic ").append("interface".equals(kind) ? "interface " : "abstract".equals(kind) ? "abstract class " : "class ").append(simple);
        String sup = MiniJson.str(d.get("super"));
        List<Object> ifs = MiniJson.arr(d.get("interfaces"));
        if (sup != null && !"interface".equals(kind)) b.append(" extends ").append(src(sup));
        if (!ifs.isEmpty()) {
            b.append("interface".equals(kind) ? " extends " : " implements ");
            for (int i = 0; i < ifs.size(); i++) b.append(i > 0 ? ", " : "").append(src((String) ifs.get(i)));
        }
        b.append(" {\n");
        if (!"interface".equals(kind)) {
            for (Object fo : MiniJson.arr(d.get("fields"))) {
                Map<String, Object> f = MiniJson.obj(fo);
                String t = MiniJson.str(f.get("t")), g = MiniJson.str(f.get("g"));
                String type = !erased && g != null && g.indexOf('<') >= 0 ? genericSrc(g) : src(t);
                b.append("    public ").append(Boolean.TRUE.equals(f.get("s")) ? "static " : "").append(type).append(' ').append(f.get("n")).append(";\n");
            }
            List<String[]> rm = roleMethods.get(n);
            if (rm != null) for (String[] r : rm) {
                String[] ps = r[3].isEmpty() ? new String[0] : r[3].split("\\|");
                StringBuilder sig = new StringBuilder(), args = new StringBuilder();
                for (int i = 0; i < ps.length; i++) {
                    sig.append(i > 0 ? ", " : "").append(src(ps[i])).append(" p").append(i);
                    args.append(i > 0 ? ", " : "").append('p').append(i);
                }
                String ret = r[2], call = "mcradar.FixtureGame.hook(\"" + r[0] + "\", this, new Object[] {" + args + "})";
                String body;
                if (ret.equals("double")) body = "((Number) " + call + ").doubleValue()";
                else if (ret.equals("float")) body = "((Number) " + call + ").floatValue()";
                else if (ret.equals("int")) body = "((Number) " + call + ").intValue()";
                else body = "(" + src(ret) + ") " + call;
                b.append("    public ").append(src(ret)).append(' ').append(r[1]).append('(').append(sig).append(") { return ").append(body).append("; }\n");
            }
            if (textClasses.contains(n)) b.append("    public String toString() { return (String) mcradar.FixtureGame.hook(\"text\", this, new Object[0]); }\n");
        }
        return b.append("}\n").toString();
    }

    /** Names a class mentions that are neither defined nor loadable: they get empty stand-ins (collections where the dump said so). */
    private void stubMissing(Map<String, String> sources) {
        Set<String> need = new HashSet<String>();
        for (Map<String, Object> d : defs.values()) {
            String sup = MiniJson.str(d.get("super"));
            if (sup != null) need.add(sup);
            for (Object i : MiniJson.arr(d.get("interfaces"))) need.add((String) i);
            for (Object fo : MiniJson.arr(d.get("fields"))) {
                Map<String, Object> f = MiniJson.obj(fo);
                String t = MiniJson.str(f.get("t"));
                while (t.startsWith("[")) t = t.substring(1);
                if (t.startsWith("L") && t.endsWith(";")) t = t.substring(1, t.length() - 1);
                need.add(t);
                String g = MiniJson.str(f.get("g"));
                if (g != null && g.indexOf('<') >= 0) { Matcher m = IDENT.matcher(g); while (m.find()) need.add(m.group()); }
            }
        }
        for (String n : need) {
            if (n.isEmpty() || defs.containsKey(n) || sources.containsKey(n) || !n.matches("[A-Za-z_$][\\w$.]*") || n.matches("int|long|double|float|boolean|byte|short|char|void|\\?|extends|super")) continue;
            if (!n.contains(".") && n.length() <= 2 && Character.isUpperCase(n.charAt(0)) && !loadable(n) && !defs.containsKey(n) && n.equals(n.toUpperCase()) && n.length() == 1) continue;   // type variable T, K, V
            if (loadable(n)) continue;
            int dot = n.lastIndexOf('.');
            String simple = n.substring(dot + 1), kind = libKinds.get(n);
            String ext = "map".equals(kind) ? " extends java.util.LinkedHashMap" : "list".equals(kind) ? " extends java.util.ArrayList" : "set".equals(kind) ? " extends java.util.LinkedHashSet" : "";
            sources.put(n, (dot > 0 ? "package " + n.substring(0, dot) + ";\n" : "") + "@SuppressWarnings({\"unchecked\", \"rawtypes\"})\npublic class " + simple + ext + " { }\n");
        }
    }

    private void generate() throws Exception {
        for (Object o : MiniJson.arr(doc.get("classes"))) { Map<String, Object> d = MiniJson.obj(o); defs.put(MiniJson.str(d.get("name")), d); }
        for (Map.Entry<String, Object> e : MiniJson.obj(doc.get("libs")).entrySet()) libKinds.put(e.getKey(), (String) e.getValue());
        collectRoles();
        for (boolean erased : new boolean[] {false, true}) {
            Map<String, String> sources = new LinkedHashMap<String, String>();
            for (Map<String, Object> d : defs.values()) sources.put(MiniJson.str(d.get("name")), classSource(d, erased));
            stubMissing(sources);
            DiagnosticCollector<JavaFileObject> diags = new DiagnosticCollector<JavaFileObject>();
            Map<String, byte[]> out = compile(sources, diags);
            if (out != null) {
                final Map<String, byte[]> bytes = out;
                loader = new ClassLoader(FixtureGame.class.getClassLoader()) {
                    protected Class<?> findClass(String n) throws ClassNotFoundException {
                        byte[] b = bytes.get(n);
                        if (b == null) throw new ClassNotFoundException(n);
                        return defineClass(n, b, 0, b.length);
                    }
                };
                for (String n : sources.keySet()) cls.put(n, Class.forName(n, true, loader));
                return;
            }
            if (erased) {
                StringBuilder m = new StringBuilder("fixture " + name + ": generated classes do not compile:\n");
                for (Diagnostic<? extends JavaFileObject> d : diags.getDiagnostics()) if (d.getKind() == Diagnostic.Kind.ERROR) m.append("  ").append(d.getMessage(null)).append('\n');
                throw new IllegalStateException(m.toString());
            }
        }
    }

    private static final class Src extends SimpleJavaFileObject {
        final String code;
        Src(String n, String code) { super(URI.create("string:///" + n.replace('.', '/') + ".java"), Kind.SOURCE); this.code = code; }
        public CharSequence getCharContent(boolean b) { return code; }
    }

    private static final class Bin extends SimpleJavaFileObject {
        final java.io.ByteArrayOutputStream bos = new java.io.ByteArrayOutputStream();
        Bin(String n) { super(URI.create("mem:///" + n.replace('.', '/') + ".class"), Kind.CLASS); }
        public OutputStream openOutputStream() { return bos; }
    }

    private Map<String, byte[]> compile(Map<String, String> sources, DiagnosticCollector<JavaFileObject> diags) throws IOException {
        JavaCompiler jc = ToolProvider.getSystemJavaCompiler();
        if (jc == null) throw new IllegalStateException("no system Java compiler: run the fixture tests with a JDK, not a JRE");
        final Map<String, Bin> outs = new HashMap<String, Bin>();
        StandardJavaFileManager std = jc.getStandardFileManager(diags, null, null);
        JavaFileManager2 fm = new JavaFileManager2(std, outs);
        List<JavaFileObject> units = new ArrayList<JavaFileObject>();
        for (Map.Entry<String, String> e : sources.entrySet()) units.add(new Src(e.getKey(), e.getValue()));
        boolean ok = jc.getTask(new StringWriter(), fm, diags, Arrays.asList("-nowarn", "-proc:none", "-Xlint:none", "-cp", System.getProperty("java.class.path")), null, units).call();
        if (!ok) return null;
        Map<String, byte[]> res = new HashMap<String, byte[]>();
        for (Map.Entry<String, Bin> e : outs.entrySet()) res.put(e.getKey(), e.getValue().bos.toByteArray());
        return res;
    }

    private static final class JavaFileManager2 extends ForwardingJavaFileManager<StandardJavaFileManager> {
        final Map<String, Bin> outs;
        JavaFileManager2(StandardJavaFileManager m, Map<String, Bin> outs) { super(m); this.outs = outs; }
        public JavaFileObject getJavaFileForOutput(Location l, String n, JavaFileObject.Kind k, FileObject sibling) {
            Bin b = new Bin(n); outs.put(n, b); return b;
        }
    }

    // ================================================================== scene

    private Class<?> clsOf(String n) throws Exception {
        if (n.startsWith("[")) {
            if (n.startsWith("[L")) return Array.newInstance(clsOf(n.substring(2, n.length() - 1)), 0).getClass();
            return Class.forName(n);
        }
        Class<?> c = cls.get(n);
        return c != null ? c : Class.forName(n, true, loader != null ? loader : FixtureGame.class.getClassLoader());
    }

    private Object inst(String n) throws Exception {
        Class<?> c = clsOf(n);
        if (c.isInterface() || Modifier.isAbstract(c.getModifiers())) {                // an abstract/interface runtime class cannot exist: pick a concrete subtype from the dump
            for (Map.Entry<String, Class<?>> e : cls.entrySet())
                if (c.isAssignableFrom(e.getValue()) && !e.getValue().isInterface() && !Modifier.isAbstract(e.getValue().getModifiers())) { c = e.getValue(); break; }
        }
        java.lang.reflect.Constructor<?> k = c.getDeclaredConstructor();
        k.setAccessible(true);
        return k.newInstance();
    }

    private static Field field(Class<?> c, String n) throws NoSuchFieldException {
        for (Class<?> k = c; k != null; k = k.getSuperclass()) try { Field f = k.getDeclaredField(n); f.setAccessible(true); return f; } catch (NoSuchFieldException ignored) { }
        throw new NoSuchFieldException(c.getName() + "." + n);
    }

    private static void set(Object o, String n, Object v) throws Exception { field(o.getClass(), n).set(o, v); }

    /** Builds the objects along `steps` from `start` (null = static root); returns the last one. Objects are shared between paths with the same prefix. */
    private Object chain(String keyPrefix, Object start, List<Object> steps) throws Exception {
        Object cur = start;
        String key = keyPrefix;
        for (int i = 0; i < steps.size(); i++) {
            Map<String, Object> s = MiniJson.obj(steps.get(i));
            String vc = MiniJson.str(s.get("vc"));
            if (vc == null) return cur;
            boolean idx = s.containsKey("idx");
            key += "/" + (idx ? "#" + s.get("idx") : MiniJson.str(s.get("c")) + "." + MiniJson.str(s.get("f")));
            Object next = built.get(key);
            if (next == null) {
                if (vc.startsWith("[")) {
                    int len = 1;
                    if (i + 1 < steps.size() && MiniJson.obj(steps.get(i + 1)).containsKey("idx")) len = ((Number) MiniJson.obj(steps.get(i + 1)).get("idx")).intValue() + 1;
                    next = Array.newInstance(clsOf(vc.substring(2, vc.length() - 1)), len);
                } else next = inst(vc);
                built.put(key, next);
                if (idx) {
                    int at = ((Number) s.get("idx")).intValue();
                    if (cur instanceof Object[]) {
                        Object[] a = (Object[]) cur;
                        for (int k = 0; k < a.length; k++) if (k != at && a[k] == null) a[k] = inst(a.getClass().getComponentType().getName());
                        a[at] = next;
                    }
                } else if (Boolean.TRUE.equals(s.get("s"))) {
                    field(clsOf(MiniJson.str(s.get("c"))), MiniJson.str(s.get("f"))).set(null, next);
                } else field(clsOf(MiniJson.str(s.get("c"))), MiniJson.str(s.get("f"))).set(cur, next);
            }
            cur = next;
        }
        return cur;
    }

    private Map<String, Object> playerRole(Class<?> c) {
        for (Object o : MiniJson.arr(MiniJson.obj(doc.get("roles")).get("playerClasses"))) {
            Map<String, Object> pc = MiniJson.obj(o);
            if (c.getName().equals(pc.get("class"))) return pc;
        }
        return null;
    }

    private static boolean isVec(Class<?> t, FixtureGame g) {
        int d = 0, o = 0;
        for (Class<?> k = t; k != null && k != Object.class; k = k.getSuperclass()) for (Field f : k.getDeclaredFields()) { if (Modifier.isStatic(f.getModifiers())) continue; if (f.getType() == double.class) d++; else o++; }
        return d == 3 && o == 0;
    }

    /** Creates a Vec3-like object (three doubles in declaration order). */
    private Object vec(Class<?> t, double x, double y, double z) throws Exception {
        Object v = inst(t.getName());
        List<Field> fs = new ArrayList<Field>();
        for (Class<?> k = t; k != null && k != Object.class; k = k.getSuperclass()) for (Field f : k.getDeclaredFields()) if (!Modifier.isStatic(f.getModifiers()) && f.getType() == double.class) fs.add(f);
        double[] d = {x, y, z};
        for (int i = 0; i < 3; i++) { fs.get(i).setAccessible(true); fs.get(i).setDouble(v, d[i]); }
        return v;
    }

    /** Writes a position through the dumped accessor: a Vec3 field (a fresh object, like the game does) or three doubles. Other Vec3 fields get a tiny "velocity". */
    private void writePos(Object o, Map<String, Object> pos, double x, double y, double z) throws Exception {
        if (pos == null) return;
        boolean isVec = "vec3".equals(pos.get("kind"));
        String chosen = isVec ? MiniJson.str(MiniJson.obj(pos.get("field")).get("field")) : null;
        if (isVec) {
            Field f = field(o.getClass(), chosen);
            f.set(o, vec(f.getType(), x, y, z));
            for (Class<?> k = o.getClass(); k != null && k != Object.class; k = k.getSuperclass())
                for (Field g : k.getDeclaredFields()) {
                    if (Modifier.isStatic(g.getModifiers()) || g.getType().isPrimitive() || g.getName().equals(chosen) || !isVec(g.getType(), this)) continue;
                    g.setAccessible(true);
                    g.set(o, vec(g.getType(), 0.01, 0, 0.02));
                }
        } else {
            String[] axes = {"x", "y", "z"};
            double[] v = {x, y, z};
            for (int i = 0; i < 3; i++) field(o.getClass(), MiniJson.str(MiniJson.obj(pos.get(axes[i])).get("field"))).setDouble(o, v[i]);
        }
    }

    private void writeBox(Object o, Map<String, Object> bbAcc, double x, double y, double z, double w, double h) throws Exception {
        if (bbAcc == null || bbAcc.isEmpty()) return;
        Field f = field(o.getClass(), MiniJson.str(bbAcc.get("field")));
        Object bb = inst(f.getType().getName());
        List<Field> fs = new ArrayList<Field>();
        for (Class<?> k = bb.getClass(); k != null && k != Object.class; k = k.getSuperclass()) for (Field g : k.getDeclaredFields()) if (!Modifier.isStatic(g.getModifiers()) && g.getType() == double.class) fs.add(g);
        double[] v = {x - w / 2, y, z - w / 2, x + w / 2, y + h, z + w / 2};
        for (int i = 0; i < 6; i++) { fs.get(i).setAccessible(true); fs.get(i).setDouble(bb, v[i]); }
        f.set(o, bb);
    }

    private void buildScene() throws Exception {
        Map<String, Object> paths = MiniJson.obj(doc.get("paths"));
        Map<String, Object> pl = MiniJson.obj(paths.get("playerList")), lp = MiniJson.obj(paths.get("localPlayer"));
        Object localObj = lp.isEmpty() || MiniJson.arr(lp.get("steps")).isEmpty() ? null : chain("", null, MiniJson.arr(lp.get("steps")));
        Object container = chain("", null, MiniJson.arr(pl.get("steps")));
        List<Object> elems = MiniJson.arr(pl.get("elems"));
        if (elems.isEmpty() && localObj != null) elems = new ArrayList<Object>(Arrays.asList(localObj.getClass().getName()));
        for (int i = 0; i < 3; i++) {
            String ec = (String) elems.get(Math.min(i == 0 ? 0 : 1, elems.size() - 1));
            Object p = i == 0 && localObj != null ? localObj : inst(ec);
            players.add(p);
            for (Class<?> k = p.getClass(); k != null; k = k.getSuperclass())
                for (Field f : k.getDeclaredFields())
                    if (f.getType().getName().equals("com.mojang.authlib.GameProfile")) {
                        f.setAccessible(true);
                        f.set(p, Class.forName("com.mojang.authlib.GameProfile").getConstructor(UUID.class, String.class).newInstance(UUID.nameUUIDFromBytes(("OfflinePlayer:" + names.get(i)).getBytes()), names.get(i)));
                    }
            state.put(p, new double[] {100 + 10 * i, 64, -50 + 7 * i, 0, 0});
            add(container, p, i);
        }
        // camera
        Map<String, Object> cam = MiniJson.obj(MiniJson.obj(doc.get("roles")).get("camera"));
        if (Boolean.TRUE.equals(cam.get("decided"))) {
            camera = chain("", null, MiniJson.arr(MiniJson.obj(cam.get("path")).get("steps")));
            fovHolder = chain("", null, MiniJson.arr(MiniJson.obj(cam.get("holder")).get("steps")));
        }
        // item entities
        List<Object> items = MiniJson.arr(MiniJson.obj(doc.get("roles")).get("items"));
        String[] ids = {"diamond_sword", "cobblestone", "golden_apple"};
        int n = 0;
        for (Object io : items) {
            Map<String, Object> it = MiniJson.obj(io);
            for (int k = 0; k < 3; k++) {
                Object e = inst(MiniJson.str(it.get("entity")));
                Object stack = chainFrom(e, MiniJson.arr(it.get("steps")));
                String id = ids[k];
                ID_OF.put(stack, id);
                setCount(stack, k == 1 ? 37 : 1);
                stacks.add(stack); stackIds.add(id); stackCounts.add(k == 1 ? 37 : 1);
                for (Object ro : MiniJson.arr(it.get("idRefs"))) {
                    Map<String, Object> ref = MiniJson.obj(ro);
                    List<Object> via = MiniJson.arr(ref.get("via"));
                    Object holder = stack;
                    for (int v = 0; v < via.size(); v++) {
                        Field f = field(holder.getClass(), (String) via.get(v));
                        Object nx = v == via.size() - 1 ? inst(MiniJson.str(ref.get("class"))) : (f.get(holder) != null ? f.get(holder) : inst(f.getType().getName()));
                        f.set(holder, nx);
                        holder = nx;
                    }
                    ID_OF.put(holder, id);
                    if (via.size() >= 1 && stackRefs.size() == stacks.size() - 1) stackRefs.add(new Object[] {stack, field(stack.getClass(), (String) via.get(0)), field(stack.getClass(), (String) via.get(0)).get(stack)});
                }
                if (stackRefs.size() < stacks.size()) stackRefs.add(null);
                double[] st = {98 + 2 * n, 64, -48 - n, 0, 0};
                state.put(e, st);
                itemEntities.add(e);
                writeBox(e, MiniJson.obj(it.get("bbField")), st[0], st[1], st[2], 0.25, 0.25);
                writePos(e, MiniJson.obj(it.get("position")), st[0], st[1], st[2]);
                n++;
            }
        }
        // entity containers: players + item entities + one of every other element class
        for (Object co : MiniJson.arr(paths.get("entityContainers"))) {
            Map<String, Object> c = MiniJson.obj(co);
            Object cont = chain("", null, MiniJson.arr(c.get("steps")));
            Set<String> el = new HashSet<String>();
            for (Object x : MiniJson.arr(c.get("elems"))) el.add((String) x);
            if (el.isEmpty()) continue;
            int id = 0;
            for (Object p : players) if (el.contains(p.getClass().getName())) add(cont, p, id++);
            for (Object e : itemEntities) if (el.contains(e.getClass().getName())) add(cont, e, id++);
            for (String ec : el) {
                boolean known = false;
                for (Object p : players) known |= p.getClass().getName().equals(ec);
                for (Object e : itemEntities) known |= e.getClass().getName().equals(ec);
                if (!known) { Object d = inst(ec); state.put(d, new double[] {105, 64, -55, 0, 0}); add(cont, d, id++); }
            }
        }
        tick(0);
    }

    private Object chainFrom(Object start, List<Object> steps) throws Exception { return chain("item@" + System.identityHashCode(start), start, steps); }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private static void add(Object cont, Object e, int id) {
        if (cont instanceof Map) { if (!((Map) cont).containsValue(e)) ((Map) cont).put(Integer.valueOf(1000 + id), e); }     // the player list is also an entity container: add once
        else if (cont instanceof Collection) { if (!((Collection) cont).contains(e)) ((Collection) cont).add(e); }
    }

    private void setCount(Object stack, int n) throws Exception {
        for (Class<?> k = stack.getClass(); k != null && k != Object.class; k = k.getSuperclass())
            for (Field f : k.getDeclaredFields())
                if (!Modifier.isStatic(f.getModifiers()) && f.getType() == int.class) { f.setAccessible(true); f.setInt(stack, n); return; }
    }

    /** Advances the scene to time t (seconds): the local head sweeps like FakeGame's, the camera follows the local eye, the FOV breathes. */
    public void tick(double t) {
        try {
            double yaw = -30 + 20 * Math.sin(t * 1.5), pitch = 10 + 8 * Math.sin(t * 2.1);
            fovNow = 70.0 * (1.0 + 0.1 * Math.sin(t * 0.8));
            for (int i = 0; i < players.size(); i++) {
                Object p = players.get(i);
                double[] s = state.get(p);
                s[3] = i == 0 ? (forceYaw != null ? forceYaw : yaw) : 45 * i; s[4] = i == 0 ? (forcePitch != null ? forcePitch : pitch) : 10 * i;
                s[0] = 100 + 10 * i + (i == 0 ? 3 * Math.sin(t) : 0);
                LOOK.put(p, new float[] {(float) s[3], (float) s[4]});
                Map<String, Object> pr = playerRole(p.getClass());
                if (pr == null) continue;
                writePos(p, MiniJson.obj(pr.get("position")), s[0], s[1], s[2]);
                writeBox(p, MiniJson.obj(pr.get("bbField")), s[0], s[1], s[2], 0.6, 1.8);
            }
            if (camera != null) {
                Map<String, Object> cam = MiniJson.obj(MiniJson.obj(doc.get("roles")).get("camera"));
                double[] s = state.get(players.get(0));
                writePos(camera, MiniJson.obj(cam.get("position")), s[0], s[1] + 1.62, s[2]);
                set(camera, MiniJson.str(MiniJson.obj(cam.get("yaw")).get("field")), cameraNaN ? Float.NaN : (float) s[3]);
                set(camera, MiniJson.str(MiniJson.obj(cam.get("pitch")).get("field")), (float) s[4]);
            }
        } catch (Exception e) { throw new IllegalStateException(e); }
    }

    /** The local player object, for tests that want to look at it. */
    public Object local() { return players.get(0); }

    // ================================================================== what the generated methods answer

    public static Object hook(String role, Object self, Object[] args) {
        try {
            if (role.equals("look")) {
                float[] a = LOOK.get(self);
                double y = Math.toRadians(a[0]), p = Math.toRadians(a[1]);
                Class<?> rt = current.lookReturn(self);
                if (current.lookNaN && self == current.players.get(0)) return current.vec(rt, Double.NaN, Double.NaN, Double.NaN);
                return current.vec(rt, -Math.sin(y) * Math.cos(p), -Math.sin(p), Math.cos(y) * Math.cos(p));
            }
            if (role.equals("health")) return Float.valueOf(20f);
            if (role.equals("fov")) return Double.valueOf(fovNow);
            if (role.equals("key") || role.equals("desc")) {
                String id = ID_OF.get(self);
                if (id == null) {                                    // a key method on a stack asks its item reference; the empty stack has none
                    throw new NullPointerException("no item");
                }
                return "item.minecraft." + id;
            }
            if (role.equals("text")) return "Reference{ResourceKey[minecraft:item / minecraft:" + ID_OF.get(self) + "]=x}";
        } catch (RuntimeException e) { throw e; } catch (Exception e) { throw new IllegalStateException(e); }
        throw new IllegalArgumentException(role);
    }

    private Class<?> lookReturn(Object self) throws Exception {
        for (Map.Entry<String, List<String[]>> e : roleMethods.entrySet())
            for (String[] r : e.getValue()) if (r[0].equals("look") && clsOf(e.getKey()).isInstance(self)) return clsOf(r[2]);
        throw new IllegalStateException("no look method");
    }

    // ================================================================== the agent's view

    /** A fake Instrumentation whose only job is getAllLoadedClasses(): the generated classes plus the library stand-ins on the class path. */
    public Instrumentation inst() {
        final List<Class<?>> all = new ArrayList<Class<?>>(cls.values());
        try { all.add(Class.forName("com.mojang.authlib.GameProfile")); } catch (Throwable ignored) { }
        return (Instrumentation) Proxy.newProxyInstance(FixtureGame.class.getClassLoader(), new Class<?>[] {Instrumentation.class}, new InvocationHandler() {
            public Object invoke(Object p, Method m, Object[] a) {
                if (m.getName().equals("getAllLoadedClasses")) return all.toArray(new Class<?>[0]);
                Class<?> r = m.getReturnType();
                return r == boolean.class ? Boolean.FALSE : r == long.class ? Long.valueOf(0) : null;
            }
        });
    }
}
