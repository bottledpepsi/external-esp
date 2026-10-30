package mcradar;

import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Writes esp_structure.json (agent option dump=1): the SHAPE of the game that discovery relied on, and nothing else - no values, no names of
 * players, no UUIDs, no tokens, no paths. Fields and methods of the classes along the discovered paths (player list, local player, camera,
 * entity container, item stacks) with their declared types, and which of them were chosen.
 *
 * Why: "it does not work on version X" becomes a file that agent/test/src/FixtureGame.java can turn back into a fake game, so discovery is
 * tested without Minecraft on exactly that structure (docs/fixtures.md). Only structure that a path touches is listed; everything else the
 * real game has (decoy graphs, thousands of other classes) is NOT in it - so a fixture is easier than the real game, never harder.
 */
final class StructureDump {
    static final int SCHEMA = 1;
    private final Probe p;
    private final LinkedHashMap<String, Class<?>> classes = new LinkedHashMap<String, Class<?>>();
    private final LinkedHashMap<String, String> libs = new LinkedHashMap<String, String>();
    private long lastWrite;
    private int lastItemClasses = -1;
    private String lastCam = "";

    StructureDump(Probe p) { this.p = p; }

    /** Called every tick by the Probe; rewrites the file when something new was learned (at most every 2 s). */
    void maybeWrite() {
        long now = System.currentTimeMillis();
        if (now - lastWrite < 2000 || !p.isReady()) return;
        int ic = p.items().learnedClasses();
        String cam = p.camera().describe;
        if (ic == lastItemClasses && cam.equals(lastCam) && lastWrite != 0) return;
        lastWrite = now; lastItemClasses = ic; lastCam = cam;
        try {
            File dir = p.exportDirFile();
            if (!dir.isDirectory()) dir.mkdirs();
            FileOutputStream fos = new FileOutputStream(new File(dir, "esp_structure.json"));
            try { fos.write(build().getBytes("UTF-8")); } finally { fos.close(); }
            Log.info("Wrote esp_structure.json (" + classes.size() + " classes) - the structure only, safe to share.");
        } catch (Throwable t) { Log.warn("Could not write esp_structure.json: " + t); }
    }

    // ------------------------------------------------------------------ collection

    private void addClass(Class<?> c) {
        if (c == null || c.isPrimitive()) return;
        if (c.isArray()) { addClass(c.getComponentType()); return; }
        String n = c.getName();
        if (classes.containsKey(n) || libs.containsKey(n)) return;
        if (Probe.isJdkish(c)) {
            if (n.startsWith("java.") || n.startsWith("javax.") || n.startsWith("jdk.") || n.startsWith("sun.")) return;
            libs.put(n, java.util.Map.class.isAssignableFrom(c) ? "map" : java.util.List.class.isAssignableFrom(c) ? "list"
                    : java.util.Set.class.isAssignableFrom(c) ? "set" : java.util.Collection.class.isAssignableFrom(c) ? "list" : "plain");
            return;
        }
        classes.put(n, c);
        addClass(c.getSuperclass());
        for (Class<?> i : c.getInterfaces()) addClass(i);
        for (Field f : c.getDeclaredFields()) {
            Class<?> t = f.getType();
            if (!t.isPrimitive() && !Modifier.isStatic(f.getModifiers()) && (p.isVec3Like(t) || p.aabbAccessors(t) != null)) addClass(t);
        }
    }

    private void addPath(FieldPath fp) {
        if (fp == null) return;
        for (Acc a : fp.steps) { addClass(a.owner); addClass(a.f.getType()); }
        Object cur = null;
        for (int i = 0; i < fp.steps.length; i++) {
            cur = fp.steps[i].getObj(i == 0 ? null : cur);
            if (cur == null) return;
            addClass(cur.getClass());
        }
        Object[] el = Probe.isContainer(cur) ? Probe.elements(cur) : null;
        if (el != null) for (Class<?> c : distinct(el)) addClass(c);
    }

    private static List<Class<?>> distinct(Object[] el) {
        List<Class<?>> out = new ArrayList<Class<?>>();
        for (Object o : el) {
            if (o == null || out.contains(o.getClass())) continue;
            out.add(o.getClass());
            if (out.size() >= 12) break;
        }
        return out;
    }

    // ------------------------------------------------------------------ JSON

    private static final class J {
        final StringBuilder b = new StringBuilder();
        private final java.util.ArrayDeque<Boolean> first = new java.util.ArrayDeque<Boolean>();
        J() { b.append('{'); first.push(Boolean.TRUE); }
        private void sep() { if (first.peek()) { first.pop(); first.push(Boolean.FALSE); } else b.append(','); }
        J open(String key, char c) { sep(); if (key != null) b.append(str(key)).append(':'); b.append(c); first.push(Boolean.TRUE); return this; }
        J close(char c) { first.pop(); b.append(c); return this; }
        J obj(String k) { return open(k, '{'); }
        J arr(String k) { return open(k, '['); }
        J s(String k, String v) { sep(); b.append(str(k)).append(':').append(v == null ? "null" : str(v)); return this; }
        J v(String v) { sep(); b.append(v == null ? "null" : str(v)); return this; }
        J i(String k, long v) { sep(); b.append(str(k)).append(':').append(v); return this; }
        J z(String k, boolean v) { sep(); b.append(str(k)).append(':').append(v); return this; }
        static String str(String s) {
            StringBuilder o = new StringBuilder("\"");
            for (int i = 0; i < s.length(); i++) {
                char ch = s.charAt(i);
                if (ch == '"' || ch == '\\') o.append('\\').append(ch);
                else if (ch < 0x20) o.append(' ');
                else o.append(ch);
            }
            return o.append('"').toString();
        }
    }

    private static void step(J j, Acc a) {
        j.obj(null).s("c", a.owner.getName()).s("f", a.f.getName()).s("t", a.f.getType().getName()).z("s", a.isStatic).close('}');
    }

    /** {"steps": [{c, f, t, s, vc}...], "elems": [classes of the container's elements]}; vc = runtime class of the object the step leads to. */
    private static void path(J j, String key, FieldPath fp) {
        j.obj(key).arr("steps");
        Object cur = null;
        boolean alive = fp != null;
        if (fp != null) for (int i = 0; i < fp.steps.length; i++) {
            Acc a = fp.steps[i];
            if (alive) { cur = a.getObj(i == 0 ? null : cur); alive = cur != null; }
            j.obj(null).s("c", a.owner.getName()).s("f", a.f.getName()).s("t", a.f.getType().getName()).z("s", a.isStatic)
                    .s("vc", alive ? cur.getClass().getName() : null).close('}');
        }
        j.close(']').arr("elems");
        Object[] el = alive && Probe.isContainer(cur) ? Probe.elements(cur) : null;
        if (el != null) for (Class<?> c : distinct(el)) j.v(c.getName());
        j.close(']').close('}');
    }

    private static void method(J j, String key, Method m) {
        if (m == null) { j.s(key, null); return; }
        j.obj(key).s("class", m.getDeclaringClass().getName()).s("name", m.getName()).s("ret", m.getReturnType().getName());
        j.arr("params");
        for (Class<?> t : m.getParameterTypes()) j.v(t.getName());
        j.close(']').close('}');
    }

    private static void acc(J j, String key, Acc a) {
        if (a == null) { j.s(key, null); return; }
        j.obj(key).s("class", a.owner.getName()).s("field", a.f.getName()).s("type", a.f.getType().getName()).close('}');
    }

    private static void pos(J j, String key, Probe.PosCand pc) {
        if (pc == null) { j.s(key, null); return; }
        j.obj(key).s("kind", pc.vec ? "vec3" : "doubles");
        if (pc.vec) acc(j, "field", pc.vecField);
        acc(j, "x", pc.xyz[0]); acc(j, "y", pc.xyz[1]); acc(j, "z", pc.xyz[2]);
        j.close('}');
    }

    String build() {
        classes.clear(); libs.clear();
        if (p.gameProfileClass() != null) { /* external library class: never generated */ }
        FieldPath players = p.containerPath, local = p.localPath();
        addPath(players); addPath(local);
        for (FieldPath fp : p.entityContainerPaths()) addPath(fp);
        CameraScanner cs = p.camera();
        if (cs.decided) { addPath(cs.camPath); addPath(cs.holderPath); if (cs.fovMethod != null) addClass(cs.fovMethod.getDeclaringClass()); }
        for (Map.Entry<Class<?>, Probe.Model> e : p.models.entrySet()) addClass(e.getKey());
        List<ItemScanner.DumpItem> items = p.items().dumpItems();
        for (ItemScanner.DumpItem d : items) {
            addClass(d.entity); addClass(d.stack);
            for (Object[] st : d.steps) if (st[0] instanceof Acc) { addClass(((Acc) st[0]).owner); addClass(((Acc) st[0]).f.getType()); }
            for (ItemScanner.IdRef r : d.refs) addClass(r.cls);
            for (String vcn : d.vcs) if (vcn != null) try { addClass(Class.forName(vcn, false, d.entity.getClassLoader())); } catch (Throwable ignored) { }
        }
        // elements of the player list: their runtime classes (a field may be declared with a base class)
        Object cont = players == null ? null : players.follow();
        Object[] el = cont == null ? null : Probe.elements(cont);
        if (el != null) for (int i = 0; i < el.length && i < 8; i++) if (el[i] != null) addClass(el[i].getClass());

        J j = new J();
        j.i("schema", SCHEMA);
        j.s("kind", "structure-dump");
        j.obj("game").s("minecraft", GameInfo.minecraftVersion(players == null ? null : players.steps[0].owner.getClassLoader()))
                .s("java", GameInfo.java()).s("agentWire", String.valueOf(Snapshot.WIRE_VERSION)).close('}');
        j.arr("classes");
        for (Class<?> c : classes.values()) {
            j.obj(null).s("name", c.getName()).s("super", c.getSuperclass() == null || c.getSuperclass() == Object.class ? null : c.getSuperclass().getName())
                    .s("kind", c.isInterface() ? "interface" : Modifier.isAbstract(c.getModifiers()) ? "abstract" : "class");
            j.arr("interfaces");
            for (Class<?> i : c.getInterfaces()) if (classes.containsKey(i.getName())) j.v(i.getName());
            j.close(']').arr("fields");
            for (Field f : c.getDeclaredFields()) {
                if (f.isSynthetic()) continue;
                j.obj(null).s("n", f.getName()).s("t", f.getType().getName()).s("g", f.getGenericType().getTypeName())
                        .z("s", Modifier.isStatic(f.getModifiers())).close('}');
            }
            j.close(']').close('}');
        }
        j.close(']');
        j.obj("libs");
        for (Map.Entry<String, String> e : libs.entrySet()) j.s(e.getKey(), e.getValue());
        j.close('}');
        j.obj("paths");
        path(j, "playerList", players); path(j, "localPlayer", local);
        j.arr("entityContainers");
        for (FieldPath fp : p.entityContainerPaths()) path(j, null, fp);
        j.close(']').close('}');
        j.obj("roles");
        j.arr("playerClasses");
        for (Map.Entry<Class<?>, Probe.Model> e : p.models.entrySet()) {
            Probe.Model m = e.getValue();
            j.obj(null).s("class", e.getKey().getName());
            pos(j, "position", m.pos);
            method(j, "look", m.look); method(j, "health", m.healthMethod);
            acc(j, "bbField", m.bbField);
            j.close('}');
        }
        j.close(']');
        j.obj("camera").z("decided", cs.decided);
        if (cs.decided) {
            path(j, "path", cs.camPath); path(j, "holder", cs.holderPath);
            pos(j, "position", cs.posAcc); acc(j, "yaw", cs.yawAcc); acc(j, "pitch", cs.pitchAcc);
            method(j, "fov", cs.fovMethod); j.z("fovTakesCamera", cs.fovTakesCam);
        }
        j.close('}');
        j.arr("items");
        for (ItemScanner.DumpItem d : items) {
            j.obj(null).s("entity", d.entity.getName()).s("stack", d.stack == null ? null : d.stack.getName());
            j.arr("steps");
            for (int si = 0; si < d.steps.size(); si++) {
                Object[] st = d.steps.get(si);
                String vc = si < d.vcs.size() ? d.vcs.get(si) : null;
                if (st[0] instanceof Acc) {
                    Acc a = (Acc) st[0];
                    j.obj(null).s("c", a.owner.getName()).s("f", a.f.getName()).s("t", a.f.getType().getName()).z("s", a.isStatic).s("vc", vc).close('}');
                } else j.obj(null).i("idx", (Integer) st[0]).s("vc", vc).close('}');
            }
            j.close(']');
            method(j, "getter", d.getter);
            method(j, "keyMethod", d.key);
            pos(j, "position", d.model == null ? null : d.model.pos);
            acc(j, "bbField", d.model == null ? null : d.model.bbField);
            j.arr("idRefs");
            for (ItemScanner.IdRef r : d.refs) {
                j.obj(null).s("class", r.cls.getName()).z("idText", r.idText);
                j.arr("via");
                for (String f : r.via) j.v(f);
                j.close(']');
                method(j, "descMethod", r.desc);
                j.close('}');
            }
            j.close(']').close('}');
        }
        j.close(']').close('}');          // items, roles
        j.b.append('}');
        return j.b.toString() + "\n";
    }
}
