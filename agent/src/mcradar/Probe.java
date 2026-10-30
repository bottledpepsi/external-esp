package mcradar;

import java.io.File;
import java.io.FileOutputStream;
import java.lang.instrument.Instrumentation;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.*;

/**
 * Version-independent discovery of Minecraft's player objects.
 *
 * Minecraft's class and field names change every version and mapping (vanilla obfuscation,
 * Fabric intermediary, Forge SRG, Mojang names). So nothing here relies on a Minecraft name.
 * It relies on STRUCTURE, which is stable:
 *
 *  1. com.mojang.authlib.GameProfile is a library class and is never obfuscated.
 *  2. A "player class" = a class (with superclasses) that has a GameProfile field AND a position:
 *     either a small "Vec3-like" object (a class with exactly three double fields) or 3+ double fields.
 *     (The tab-list entry class has a GameProfile too, but no position -> rejected.)
 *  3. Starting from static fields (preferring self-typed singletons like Minecraft.instance) we walk
 *     the object graph breadth-first looking for a container (List/Set/Map/array) whose elements are
 *     ALL player objects. That is the level's player list. The path of fields leading there is
 *     remembered and replayed every tick, so it survives world changes.
 *  4. Which double triple is the position is decided by value: the position has the largest
 *     magnitude among Vec3-like fields/double triples (velocity is tiny, the position is not).
 *  5. Heading comes from a no-arg method returning a unit-length Vec3-like (the look vector).
 */
final class Probe {
    private static final String[] SKIP = {
        "java.", "javax.", "jdk.", "sun.", "com.sun.", "org.lwjgl.", "io.netty.", "com.google.",
        "org.apache.", "it.unimi.", "org.slf4j.", "org.joml.", "com.mojang.blaze3d.", "com.mojang.authlib.",
        "com.mojang.datafixers.", "com.mojang.serialization.", "com.mojang.brigadier.", "org.objectweb.",
        "org.spongepowered.", "kotlin.", "scala.", "org.jline.", "oshi.", "com.ibm.icu.", "org.w3c.",
        "org.xml.", "org.checkerframework.", "mcradar.", "net.fabricmc.", "net.minecraftforge.fml.",
        "cpw.mods.", "org.apache.logging."
    };

    final Instrumentation inst;
    private final AgentConfig cfg;
    final String username;

    private Class<?> gameProfile;
    private Method gpNameMethod;
    private Acc gpNameField;

    private final Map<Class<?>, Boolean> vecCache = new HashMap<Class<?>, Boolean>();
    private final Map<Class<?>, Acc[]> vecAccCache = new HashMap<Class<?>, Acc[]>();
    private final Map<Class<?>, Boolean> playerCache = new HashMap<Class<?>, Boolean>();
    private final Map<Class<?>, Acc[]> refFieldCache = new HashMap<Class<?>, Acc[]>();
    private final Map<Class<?>, Acc> profileFieldCache = new HashMap<Class<?>, Acc>();
    final Map<Class<?>, Model> models = new HashMap<Class<?>, Model>();
    private final Map<Class<?>, Long> lastModelAttempt = new HashMap<Class<?>, Long>();
    private long lastModelRebuild;
    // At pitch exactly +-90 the look vector has no horizontal part, so yaw cannot be derived from it. Keep the last
    // yaw seen per player object (cleared when it grows) and use it only for that degenerate case.
    private final IdentityHashMap<Object, Float> lastYaw = new IdentityHashMap<Object, Float>();
    private boolean localYawFresh;

    private void holdYaw(Object o, float[] o2) {
        if (!Float.isNaN(o2[0])) {
            if (lastYaw.size() > 512) lastYaw.clear();
            lastYaw.put(o, o2[0]);
        } else if (!Float.isNaN(o2[1])) {          // pitch known, yaw not: straight up/down
            Float y = lastYaw.get(o);
            if (y != null) o2[0] = y;
        }
    }

    FieldPath containerPath;
    private FieldPath localPath;

    // Where the player list came from. The sources are tried in this order (discover()); rank 0 is the normal case, anything higher is a fallback
    // and is re-tried for a better one every UPGRADE_INTERVAL_MS (a world that loads late, a list that appears after the first join).
    static final String[] PLAYER_SOURCES = {"players-list (singleton roots)", "players-list (all static roots)", "entity container (mixed)", "local player only"};
    private int playerRank;
    private boolean localOnly;                         // containerPath IS the local player's path: there is no list to read
    private long upgradeAt;
    static volatile long UPGRADE_INTERVAL_MS = 15000;
    String playerSource() { return PLAYER_SOURCES[playerRank]; }

    // A new level object = a new world (join, leave, dimension change). Negative results learned in the old one are forgotten.
    private Object lastHolder;
    private long lastHolderCheck;
    int epoch;
    private final AuxSearch aux = new AuxSearch(this);
    private final CameraScanner camScan = new CameraScanner(this);
    private final StructureDump dumper = new StructureDump(this);
    private final ItemScanner itemScan = new ItemScanner(this);
    private boolean auxDone;
    private long auxNextTry, lastItemPoll;
    private double[] lastEye;                 // eye of the last snapshot; pollItemsIfDue() searches around it
    private boolean itemsDirty = true;       // lastItems changed since it was last put on the wire
    private List<Snapshot.Item> lastItems = new ArrayList<Snapshot.Item>();
    private String exportedCam = "", exportedItems = "";
    private boolean ready;
    private int failTicks;
    private String status = "starting";

    Probe(Instrumentation inst, AgentConfig cfg) {
        this.inst = inst;
        this.cfg = cfg;
        this.username = cfg.username != null ? cfg.username : detectUsername();
        Log.info("Local username: " + (username == null ? "(unknown - will use shallowest player object)" : username));
    }

    boolean isReady() { return ready; }
    FieldPath localPath() { return localPath; }
    List<FieldPath> entityContainerPaths() { return aux.entityContainers; }
    CameraScanner camera() { return camScan; }
    ItemScanner items() { return itemScan; }
    Class<?> gameProfileClass() { return gameProfile; }
    AgentConfig config() { return cfg; }

    private final Map<Class<?>, Boolean> entityClassCache = new HashMap<Class<?>, Boolean>();

    /** True for entity-like classes (have a position). Cached. Used to avoid walking from a stack back into the world. */
    boolean isEntityClass(Class<?> c) {
        Boolean b = entityClassCache.get(c);
        if (b == null) {
            boolean r = false;
            try { r = !c.isPrimitive() && !c.isArray() && !c.isInterface() && !isJdkish(c) && !posCandidates(c).isEmpty(); } catch (Throwable ignored) { }
            entityClassCache.put(c, r);
            b = r;
        }
        return b;
    }

    File exportDirFile() { return new File(cfg.exportDir); }

    private Set<Class<?>> worldClasses;

    /** True for the world object (the class hierarchy of the object that owns the player list, e.g. ClientLevel/Level). */
    boolean isWorldClass(Class<?> c) {
        if (worldClasses == null) {
            Set<Class<?>> s = new HashSet<Class<?>>();
            Node h = holderNode();
            if (h != null)
                for (Class<?> k = h.obj.getClass(); k != null && k != Object.class && !isJdkish(k); k = k.getSuperclass()) s.add(k);
            worldClasses = s;
        }
        return worldClasses.contains(c);
    }

    /** Node of the object that owns the player container (normally the ClientLevel), or null. */
    Node holderNode() {
        if (containerPath == null) return null;
        Acc[] st = containerPath.steps;
        Node prev = null; Object cur = null;
        for (int i = 0; i < st.length - 1; i++) {
            cur = st[i].getObj(i == 0 ? null : cur);
            if (cur == null) return null;
            prev = new Node(cur, prev, st[i], i + 1);
        }
        return prev;
    }
    String status() { return status; }
    void reset() { ready = false; failTicks = 0; }

    /** Parses --username from the launch command. Other arguments (access tokens!) are never logged or exported. */
    private static String detectUsername() {
        String cmd = System.getProperty("sun.java.command");
        if (cmd == null) return null;
        String[] t = cmd.split("\\s+");
        for (int i = 0; i + 1 < t.length; i++) if (t[i].equals("--username")) return t[i + 1];
        return null;
    }

    // ------------------------------------------------------------------ class classification

    static boolean isJdkish(Class<?> c) {
        String n = c.getName();
        for (String p : SKIP) if (n.startsWith(p)) return true;
        return n.contains("$$Lambda") || n.contains("/0x");
    }

    private static boolean containerish(Class<?> t) {
        return java.util.Collection.class.isAssignableFrom(t) || java.util.Map.class.isAssignableFrom(t)
                || t.isAssignableFrom(ArrayList.class) || t.isAssignableFrom(HashMap.class)
                || t.isAssignableFrom(java.util.concurrent.ConcurrentHashMap.class)
                || t.isAssignableFrom(java.util.concurrent.CopyOnWriteArrayList.class)
                || t.isAssignableFrom(LinkedList.class) || t.isAssignableFrom(ArrayDeque.class);
    }

    /** A class whose instances are "3 doubles and nothing else" (Vec3, Vec3d, Vector3d, ...). */
    boolean isVec3Like(Class<?> t) {
        if (t.isPrimitive() || t.isArray() || t.isInterface() || isJdkish(t)) return false;
        Boolean b = vecCache.get(t);
        if (b != null) return b;
        boolean r = false;
        try {
            int d = 0, other = 0;
            for (Class<?> k = t; k != null && k != Object.class; k = k.getSuperclass())
                for (Field f : k.getDeclaredFields()) {
                    if (Modifier.isStatic(f.getModifiers())) continue;
                    if (f.getType() == double.class) d++; else other++;
                }
            r = d == 3 && other == 0 && vecAccessors(t) != null;
        } catch (Throwable ignored) { }
        vecCache.put(t, r);
        return r;
    }

    /** Accessors for the (x, y, z) doubles of a Vec3-like class, in declaration order. */
    Acc[] vecAccessors(Class<?> t) {
        if (vecAccCache.containsKey(t)) return vecAccCache.get(t);
        Acc[] out = null;
        try {
            List<Acc> l = new ArrayList<Acc>();
            for (Class<?> k = t; k != null && k != Object.class; k = k.getSuperclass())
                for (Field f : k.getDeclaredFields())
                    if (!Modifier.isStatic(f.getModifiers()) && f.getType() == double.class) {
                        Acc a = Acc.of(f);
                        if (a == null) { l = null; break; }
                        l.add(a);
                    }
            if (l != null && l.size() == 3) out = l.toArray(new Acc[0]);
        } catch (Throwable ignored) { }
        vecAccCache.put(t, out);
        return out;
    }

    boolean isPlayerClass(Class<?> c) {
        Boolean b = playerCache.get(c);
        if (b != null) return b;
        boolean r = false;
        if (gameProfile != null && !c.isInterface() && !c.isArray() && !isJdkish(c)) {
            try {
                boolean hasGp = false, hasVec = false;
                int doubles = 0;
                for (Class<?> k = c; k != null && k != Object.class; k = k.getSuperclass())
                    for (Field f : k.getDeclaredFields()) {
                        if (Modifier.isStatic(f.getModifiers())) continue;
                        Class<?> t = f.getType();
                        if (t == gameProfile) hasGp = true;
                        else if (t == double.class) doubles++;
                        else if (!t.isPrimitive() && isVec3Like(t)) hasVec = true;
                    }
                r = hasGp && (hasVec || doubles >= 3);
            } catch (Throwable ignored) { }
        }
        playerCache.put(c, r);
        return r;
    }

    /** Non-static reference fields worth following when walking the object graph. */
    Acc[] refFieldsOf(Class<?> c) {
        Acc[] cached = refFieldCache.get(c);
        if (cached != null) return cached;
        List<Acc> l = new ArrayList<Acc>();
        try {
            for (Class<?> k = c; k != null && k != Object.class; k = k.getSuperclass()) {
                if (isJdkish(k)) break;
                for (Field f : k.getDeclaredFields()) {
                    if (Modifier.isStatic(f.getModifiers())) continue;
                    Class<?> t = f.getType();
                    if (t.isPrimitive() || t == String.class) continue;
                    if (!t.isArray() && t != Object.class && isJdkish(t) && !containerish(t)) continue;
                    Acc a = Acc.of(f);
                    if (a != null) l.add(a);
                }
            }
        } catch (Throwable ignored) { }
        Acc[] out = l.toArray(new Acc[0]);
        refFieldCache.put(c, out);
        return out;
    }

    private List<Acc> staticRoots(Class<?>[] all, boolean selfTypedOnly) {
        List<Acc> roots = new ArrayList<Acc>();
        for (Class<?> c : all) {
            try {
                if (c.isArray() || c.isInterface() || c.isPrimitive() || isJdkish(c)) continue;
                for (Field f : c.getDeclaredFields()) {
                    if (!Modifier.isStatic(f.getModifiers())) continue;
                    Class<?> t = f.getType();
                    if (t.isPrimitive() || t == String.class) continue;
                    if (selfTypedOnly && t != c) continue;
                    if (!t.isArray() && t != Object.class && isJdkish(t) && !containerish(t)) continue;
                    Acc a = Acc.of(f);
                    if (a != null) roots.add(a);
                }
            } catch (Throwable ignored) { /* NoClassDefFoundError etc. from half-loaded classes */ }
        }
        return roots;
    }

    static boolean isContainer(Object o) {
        return o instanceof Collection || o instanceof Map || o instanceof Object[];
    }

    /** Snapshot of a container's elements; null on failure. Never iterates live (avoids CME). */
    static Object[] elements(Object o) {
        try {
            if (o instanceof Object[]) return ((Object[]) o).clone();
            if (o instanceof Collection) return ((Collection<?>) o).toArray();
            if (o instanceof Map) return ((Map<?, ?>) o).values().toArray();
        } catch (Throwable ignored) { }
        return null;
    }

    /** elements() with retries: the game thread mutates these maps while we copy them (CME / torn reads). */
    static Object[] elementsRetry(Object o) {
        for (int i = 0; i < 5; i++) {
            Object[] el = elements(o);
            if (el != null) return el;
            try { Thread.sleep(1); } catch (InterruptedException ignored) { }
        }
        return null;
    }

    // ------------------------------------------------------------------ player name

    String nameOf(Object player) {
        try {
            Class<?> c = player.getClass();
            Acc pf;
            if (profileFieldCache.containsKey(c)) pf = profileFieldCache.get(c);
            else {
                pf = null;
                outer:
                for (Class<?> k = c; k != null && k != Object.class; k = k.getSuperclass())
                    for (Field f : k.getDeclaredFields())
                        if (!Modifier.isStatic(f.getModifiers()) && f.getType() == gameProfile) { pf = Acc.of(f); break outer; }
                profileFieldCache.put(c, pf);
            }
            if (pf == null) return null;
            Object gp = pf.getObj(player);
            if (gp == null) return null;
            if (gpNameMethod != null) {
                Object s = gpNameMethod.invoke(gp);
                return s instanceof String ? (String) s : null;
            }
            if (gpNameField != null) {
                Object s = gpNameField.getObj(gp);
                return s instanceof String ? (String) s : null;
            }
        } catch (Throwable ignored) { }
        return null;
    }

    // ------------------------------------------------------------------ discovery (graph search)

    static final class Node {
        final Object obj; final Node parent; final Acc via; final int depth;
        Node(Object obj, Node parent, Acc via, int depth) { this.obj = obj; this.parent = parent; this.via = via; this.depth = depth; }
    }

    private static final class Cand {
        final Node parent; final Acc via; final int depth, count;
        Cand(Node parent, Acc via, int depth, int count) { this.parent = parent; this.via = via; this.depth = depth; this.count = count; }
    }

    static FieldPath pathOf(Node parent, Acc last) {
        LinkedList<Acc> l = new LinkedList<Acc>();
        if (last != null) l.addFirst(last);
        for (Node n = parent; n != null; n = n.parent) l.addFirst(n.via);
        return new FieldPath(l);
    }

    private final class Search {
        final IdentityHashMap<Object, Boolean> seen = new IdentityHashMap<Object, Boolean>();
        final ArrayDeque<Node> queue = new ArrayDeque<Node>();
        final List<Cand> containers = new ArrayList<Cand>();
        final List<Node> locals = new ArrayList<Node>();
        final int maxNodes, maxDepth;
        final boolean allowMixed;       // accept a container that holds players AND other entities (a level's entity list)

        Search(int maxNodes, int maxDepth) { this(maxNodes, maxDepth, false); }
        Search(int maxNodes, int maxDepth, boolean allowMixed) { this.maxNodes = maxNodes; this.maxDepth = maxDepth; this.allowMixed = allowMixed; }

        void consider(Object v, Node parent, Acc via, int depth) {
            if (v == null) return;
            Class<?> c = v.getClass();
            if (isContainer(v)) {
                if (seen.put(v, Boolean.TRUE) != null) return;
                Object[] el = elements(v);
                if (el == null || el.length == 0 || el.length > 2048) return;
                int n = 0;
                for (Object e : el) {
                    if (e == null) continue;
                    if (!isPlayerClass(e.getClass())) { if (allowMixed) continue; return; }     // pure search: must contain ONLY players
                    n++;
                }
                if (n > 0) containers.add(new Cand(parent, via, depth, n));
                return;
            }
            if (isJdkish(c) || seen.put(v, Boolean.TRUE) != null) return;
            Node node = new Node(v, parent, via, depth);
            if (isPlayerClass(c)) { if (locals.size() < 32) locals.add(node); return; }
            if (depth < maxDepth) queue.add(node);
        }

        void run(List<Acc> roots, long deadline) {
            for (Acc r : roots) consider(r.getObj(null), null, r, 1);
            int foundDepth = Integer.MAX_VALUE;
            while (!queue.isEmpty()) {
                if (seen.size() > maxNodes || System.currentTimeMillis() > deadline) break;
                Node n = queue.poll();
                for (Cand c : containers) foundDepth = Math.min(foundDepth, c.depth);
                if (n.depth > foundDepth) break;                 // BFS: nothing shallower can still appear
                for (Acc a : refFieldsOf(n.obj.getClass())) consider(a.getObj(n.obj), n, a, n.depth + 1);
            }
        }
    }

    /** Returns true once a player container was found. Cheap to call repeatedly (caller backs off on failure). */
    boolean discover() {
        long t0 = System.currentTimeMillis();
        Class<?>[] all = inst.getAllLoadedClasses();

        if (gameProfile == null) {
            for (Class<?> c : all)
                if (c.getName().equals("com.mojang.authlib.GameProfile")) { gameProfile = c; break; }
            if (gameProfile == null) { status = "waiting: GameProfile class not loaded (is this a Minecraft JVM?)"; return false; }
            for (String n : new String[] {"getName", "name"}) {
                try {
                    Method m = gameProfile.getMethod(n);
                    if (m.getReturnType() == String.class) { gpNameMethod = m; break; }
                } catch (Throwable ignored) { }
            }
            if (gpNameMethod == null) {
                try { gpNameField = Acc.of(gameProfile.getDeclaredField("name")); } catch (Throwable ignored) { }
            }
            Log.info("Found " + gameProfile.getName() + " (loader " + gameProfile.getClassLoader() + ")");
        }
        if (!Mem.OK) { status = "Unsafe unavailable; add --sun-misc-unsafe-memory-access=allow to the game's JVM args"; return false; }

        // The chain, in order (each stage has its own time budget; the first that finds something wins):
        //  0 singleton roots, pure players list   1 every static field, pure players list
        //  2 the same search accepting a mixed entity container   3 no list at all: only the local player
        Search s = new Search(150_000, 5);
        s.run(staticRoots(all, true), t0 + 4000);
        int rank = 0;
        if (s.containers.isEmpty()) {
            s = new Search(500_000, 6);
            s.run(staticRoots(all, false), System.currentTimeMillis() + 8000);
            rank = 1;
        }
        if (s.containers.isEmpty() && !s.locals.isEmpty()) {
            // Player objects exist, but no list holds only players: try a container that also holds other entities (read() filters the players).
            Search m = new Search(150_000, 5, true);
            m.run(staticRoots(all, true), System.currentTimeMillis() + 3000);
            if (m.containers.isEmpty()) { m = new Search(500_000, 6, true); m.run(staticRoots(all, false), System.currentTimeMillis() + 5000); }
            if (!m.containers.isEmpty()) { s = m; rank = 2; } else rank = 3;
        }
        if (s.containers.isEmpty() && rank != 3) {
            status = "searching: no player list yet (join a world)";
            return false;
        }
        return adopt(s, rank, t0);
    }

    /** Takes the result of a successful search: the list (or, at rank 3, just the local player) and the local player path. */
    private boolean adopt(Search s, int rank, long t0) {
        Cand best = null;
        for (Cand c : s.containers)
            if (best == null || c.depth < best.depth || (c.depth == best.depth && c.count > best.count)) best = c;

        Node localNode = null;
        for (Node n : s.locals) {
            String nm = nameOf(n.obj);
            if (username != null && username.equals(nm)) { localNode = n; break; }
            if (localNode == null || n.depth < localNode.depth) localNode = n;
        }
        FieldPath newLocal = localNode == null ? null : pathOf(localNode.parent, localNode.via);
        if (rank == 3 && newLocal == null) return false;
        containerPath = rank == 3 ? newLocal : pathOf(best.parent, best.via);
        localPath = newLocal;
        worldClasses = null;
        localOnly = rank == 3;
        playerRank = rank;
        upgradeAt = System.currentTimeMillis() + UPGRADE_INTERVAL_MS;     // lastHolder is deliberately kept: a world change next to an upgrade must still count

        ready = true;
        failTicks = 0;
        auxDone = false;
        status = "ok";
        Log.info("Discovery done in " + (System.currentTimeMillis() - t0) + " ms; "
                + (rank == 3 ? "no player list - local player only" : best.count + " player(s) in container at depth " + best.depth) + "  [player source: " + playerSource() + "]");
        if (rank >= 2) Log.warn("Player list fallback in use (" + playerSource() + "); a better source is looked for every " + UPGRADE_INTERVAL_MS / 1000 + " s.");
        Log.info("  player list path: " + containerPath.describe());
        if (localPath != null) Log.info("  local player path: " + localPath.describe());
        exportLayout();
        return true;
    }

    // ------------------------------------------------------------------ position / yaw model

    static final class PosCand {
        final Acc vecField; final Acc[] xyz; final boolean vec;
        PosCand(Acc vecField, Acc[] xyz, boolean vec) { this.vecField = vecField; this.xyz = xyz; this.vec = vec; }

        /** Reads x,y,z from `o` (a Vec3 field of o, or three double fields of o). false if unreadable/insane. */
        boolean read(Object o, double[] out) {
            if (vec) {
                Object v = vecField.getObj(o);
                if (v == null) return false;
                for (int i = 0; i < 3; i++) out[i] = xyz[i].getDouble(v);
            } else {
                for (int i = 0; i < 3; i++) out[i] = xyz[i].getDouble(o);
            }
            return Model.sane(out);
        }
    }

    static final class Model {
        PosCand pos;
        Method look;
        Acc[] lookXyz;
        Method healthMethod;
        Acc bbField;
        Acc[] bbAcc;

        boolean position(Object p, double[] o) { return pos.read(p, o); }

        /** Hitbox size from the entity's AABB-like field: out[0] = width, out[1] = height. false if unavailable. */
        boolean size(Object p, float[] out) {
            if (bbField == null) return false;
            Object bb = bbField.getObj(p);
            if (bb == null) return false;
            double[] v = new double[6];
            for (int i = 0; i < 6; i++) v[i] = bbAcc[i].getDouble(bb);
            for (double d : v) if (Double.isNaN(d) || Double.isInfinite(d)) return false;
            double w = Math.max(v[3] - v[0], v[5] - v[2]), h = v[4] - v[1];
            if (w < 0.02 || w > 8 || h < 0.02 || h > 8) return false;
            out[0] = (float) w; out[1] = (float) h;
            return true;
        }

        /** out[0] = yaw, out[1] = pitch (degrees); NaN when unknown. */
        void orient(Object p, float[] out) {
            out[0] = Float.NaN; out[1] = Float.NaN;
            if (look == null) return;
            try {
                Object v = look.invoke(p);
                if (v == null) return;
                double x = lookXyz[0].getDouble(v), y = lookXyz[1].getDouble(v), z = lookXyz[2].getDouble(v);
                if (Double.isNaN(x) || Double.isNaN(y) || Double.isNaN(z)) return;
                // Minecraft look vector: x = -sin(yaw)*cos(pitch), y = -sin(pitch), z = cos(yaw)*cos(pitch)
                if (Math.hypot(x, z) > 1e-6) out[0] = (float) Math.toDegrees(Math.atan2(-x, z));
                double n = Math.sqrt(x * x + y * y + z * z);
                if (n > 0.5) out[1] = (float) Math.toDegrees(-Math.asin(Math.max(-1.0, Math.min(1.0, y / n))));
            } catch (Throwable t) { /* leave NaN */ }
        }

        float health(Object p) {
            if (healthMethod == null) return Float.NaN;
            try {
                Object v = healthMethod.invoke(p);
                float f = v instanceof Number ? ((Number) v).floatValue() : Float.NaN;
                return (f >= 0 && f <= 2048) ? f : Float.NaN;
            } catch (Throwable t) { return Float.NaN; }
        }

        static boolean sane(double[] o) {
            for (double d : o) if (Double.isNaN(d) || Double.isInfinite(d) || Math.abs(d) > 3.2e7) return false;
            return true;
        }
    }

    Model buildModel(Class<?> c, List<Object> samples) {
        List<PosCand> cands = posCandidates(c);
        // --- pick by value: biggest magnitude (position >> velocity), summed over all samples
        Model m = new Model();
        double bestScore = 0;
        double[] o = new double[3];
        for (PosCand pc : cands) {
            Model tmp = new Model();
            tmp.pos = pc;
            double score = 0;
            boolean ok = true;
            for (Object s : samples) {
                if (!tmp.position(s, o)) { ok = false; break; }
                score += Math.sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
            }
            if (ok && score > bestScore) { bestScore = score; m.pos = pc; }
        }
        if (m.pos == null) return null;
        attachBoundingBox(c, m);

        // --- heading: public no-arg method returning a unit-length Vec3-like (the look vector)
        List<Method> good = new ArrayList<Method>();
        for (Method me : c.getMethods()) {
            try {
                if (me.getParameterTypes().length != 0 || Modifier.isStatic(me.getModifiers())) continue;
                Class<?> rt = me.getReturnType();
                if (me.getDeclaringClass() == Object.class || !isVec3Like(rt)) continue;
                Acc[] va = vecAccessors(rt);
                try { me.setAccessible(true); } catch (Throwable ignored) { }
                boolean all = true;
                for (Object s : samples) {
                    Object v = me.invoke(s);
                    if (v == null) { all = false; break; }
                    double x = va[0].getDouble(v), y = va[1].getDouble(v), z = va[2].getDouble(v);
                    double n = Math.sqrt(x * x + y * y + z * z);
                    if (!(n > 0.99 && n < 1.01)) { all = false; break; }
                }
                if (all) good.add(me);
            } catch (Throwable ignored) { }
        }
        if (!good.isEmpty()) {
            // Prefer a look vector with a vertical component (the true 3-D view direction) over a
            // horizontal-only "forward" vector, so pitch is available. Then: base class first, then name.
            final Map<Method, Double> ySum = new HashMap<Method, Double>();
            for (Method me : good) {
                double sum = 0;
                Acc[] va = vecAccessors(me.getReturnType());
                for (Object s : samples) {
                    try { sum += Math.abs(va[1].getDouble(me.invoke(s))); } catch (Throwable ignored) { }
                }
                ySum.put(me, sum);
            }
            Collections.sort(good, new Comparator<Method>() {
                public int compare(Method a, Method b) {
                    int va = ySum.get(a) > 1e-3 ? 0 : 1, vb = ySum.get(b) > 1e-3 ? 0 : 1;
                    if (va != vb) return va - vb;
                    int d = depthOf(a.getDeclaringClass()) - depthOf(b.getDeclaringClass());
                    return d != 0 ? d : a.getName().compareTo(b.getName());
                }
            });
            m.look = good.get(0);
            m.lookXyz = vecAccessors(m.look.getReturnType());
        }

        // --- health: only a method literally named getHealth()/health() (works with Mojang, Yarn, MCP
        //     and Forge-mapped names; obfuscated/intermediary builds report health as unknown "-").
        for (Method me : c.getMethods()) {
            try {
                if (me.getParameterTypes().length != 0 || Modifier.isStatic(me.getModifiers())) continue;
                Class<?> rt = me.getReturnType();
                if (rt != float.class && rt != double.class) continue;
                String n = me.getName().toLowerCase(Locale.ROOT);
                if (!n.equals("gethealth") && !n.equals("health")) continue;
                try { me.setAccessible(true); } catch (Throwable ignored) { }
                boolean ok = true;
                for (Object s : samples) {
                    Object v = me.invoke(s);
                    float f = v instanceof Number ? ((Number) v).floatValue() : Float.NaN;
                    if (!(f >= 0 && f <= 2048)) { ok = false; break; }
                }
                if (ok) { try { me.setAccessible(true); } catch (Throwable ignored) { } m.healthMethod = me; break; }
            } catch (Throwable ignored) { }
        }
        return m;
    }

    private static int depthOf(Class<?> c) {
        int d = 0;
        for (Class<?> k = c; k != null; k = k.getSuperclass()) d++;
        return d;
    }

    /** Every plausible "position" accessor of a class: Vec3-like fields and (aligned) triples of double fields. */
    List<PosCand> posCandidates(Class<?> c) {
        List<PosCand> cands = new ArrayList<PosCand>();
        for (Class<?> k = c; k != null && k != Object.class; k = k.getSuperclass()) {
            List<Field> doubles = new ArrayList<Field>();
            for (Field f : k.getDeclaredFields()) {
                if (Modifier.isStatic(f.getModifiers())) continue;
                Class<?> t = f.getType();
                if (t == double.class) doubles.add(f);
                else if (!t.isPrimitive() && isVec3Like(t)) {
                    Acc fa = Acc.of(f);
                    Acc[] va = vecAccessors(t);
                    if (fa != null && va != null) cands.add(new PosCand(fa, va, true));
                }
            }
            // Older versions (<=1.16) store posX/posY/posZ as plain double fields, grouped in triples.
            for (int i = 0; i + 2 < doubles.size(); i += 3) {
                Acc x = Acc.of(doubles.get(i)), y = Acc.of(doubles.get(i + 1)), z = Acc.of(doubles.get(i + 2));
                if (x != null && y != null && z != null) cands.add(new PosCand(null, new Acc[] {x, y, z}, false));
            }
        }
        return cands;
    }

    private final Map<Class<?>, Acc[]> aabbCache = new HashMap<Class<?>, Acc[]>();

    /** Accessors (minX,minY,minZ,maxX,maxY,maxZ) of an AABB-like class (exactly six doubles, nothing else), else null. */
    Acc[] aabbAccessors(Class<?> t) {
        if (t.isPrimitive() || t.isArray() || t.isInterface() || isJdkish(t)) return null;
        if (aabbCache.containsKey(t)) return aabbCache.get(t);
        Acc[] out = null;
        try {
            List<Acc> l = new ArrayList<Acc>();
            int other = 0;
            for (Class<?> k = t; k != null && k != Object.class; k = k.getSuperclass())
                for (Field f : k.getDeclaredFields()) {
                    if (Modifier.isStatic(f.getModifiers())) continue;
                    if (f.getType() == double.class) { Acc a = Acc.of(f); if (a != null) l.add(a); } else other++;
                }
            if (other == 0 && l.size() == 6) out = l.toArray(new Acc[0]);
        } catch (Throwable ignored) { }
        aabbCache.put(t, out);
        return out;
    }

    /** First field of the entity class hierarchy whose type is AABB-like (the bounding box). */
    private void attachBoundingBox(Class<?> c, Model m) {
        for (Class<?> k = c; k != null && k != Object.class; k = k.getSuperclass())
            for (Field f : k.getDeclaredFields()) {
                if (Modifier.isStatic(f.getModifiers()) || f.getType().isPrimitive()) continue;
                Acc[] bb = aabbAccessors(f.getType());
                Acc fa = bb == null ? null : Acc.of(f);
                if (fa != null) { m.bbField = fa; m.bbAcc = bb; return; }
            }
    }

    private Method gpIdMethod;

    /** UUID string of a player's GameProfile ("-" if unknown). */
    String uuidOf(Object player) {
        try {
            Class<?> c = player.getClass();
            Acc pf = profileFieldCache.get(c);
            if (pf == null) { nameOf(player); pf = profileFieldCache.get(c); }
            if (pf == null) return "-";
            Object gp = pf.getObj(player);
            if (gp == null) return "-";
            if (gpIdMethod == null) {
                for (String n : new String[] {"getId", "id"}) {
                    try { Method m = gameProfile.getMethod(n); if (m.getReturnType() == java.util.UUID.class) { gpIdMethod = m; break; } }
                    catch (Throwable ignored) { }
                }
            }
            if (gpIdMethod == null) return "-";
            Object u = gpIdMethod.invoke(gp);
            return u == null ? "-" : u.toString();
        } catch (Throwable t) { return "-"; }
    }

    // ------------------------------------------------------------------ world changes and fallback upgrades

    /** Four times a second: did the level object change? Then what was learned as "not an item class" / "not a stack" may be wrong in the new world. */
    private void checkWorld() {
        long now = System.currentTimeMillis();
        if (now - lastHolderCheck < 250) return;
        lastHolderCheck = now;
        Node h = holderNode();
        Object cur = h == null ? null : h.obj;
        if (cur == null) return;
        if (lastHolder != null && cur != lastHolder) {
            epoch++;
            Log.info("World changed (epoch " + epoch + "): negative caches cleared.");
            itemScan.onWorldChange();
            camScan.onWorldChange();
        }
        lastHolder = cur;
    }

    /** While a fallback player source is in use, look again for a better one (cheap: singleton roots only, 600 ms). */
    private void tryUpgrade() {
        upgradeAt = System.currentTimeMillis() + UPGRADE_INTERVAL_MS;
        try {
            long t0 = System.currentTimeMillis();
            Class<?>[] all = inst.getAllLoadedClasses();
            Search s = new Search(150_000, 5);
            s.run(staticRoots(all, true), t0 + 600);
            int rank = 0;
            if (s.containers.isEmpty() && playerRank == 3) {
                s = new Search(150_000, 5, true);
                s.run(staticRoots(all, true), System.currentTimeMillis() + 600);
                rank = 2;
            }
            if (s.containers.isEmpty() || rank >= playerRank) return;
            String was = playerSource();
            adopt(s, rank, t0);
            Log.info("Player source upgraded: " + was + " -> " + playerSource());
        } catch (Throwable t) { Log.debug("Player source upgrade failed: " + t); }
    }

    // ------------------------------------------------------------------ per-tick read

    Snapshot read() {
        lastEye = null;
        Snapshot s = new Snapshot();
        if (!ready) { s.status = status; return s; }

        checkWorld();
        if (playerRank > 0 && System.currentTimeMillis() >= upgradeAt) tryUpgrade();

        Object container = containerPath.follow();
        Object[] el = container == null ? null : (localOnly ? new Object[] {container} : elements(container));
        if (el == null) {
            if (++failTicks > 60) { Log.info("Player list path went stale; re-discovering."); ready = false; status = "re-discovering"; }
            s.status = "no world loaded (player list unavailable)";
            return s;
        }
        failTicks = 0;

        List<Object> players = new ArrayList<Object>();
        for (Object e : el) if (e != null && isPlayerClass(e.getClass())) players.add(e);

        Object localObj = localPath == null ? null : localPath.follow();
        if (localObj != null && !isPlayerClass(localObj.getClass())) localObj = null;
        String localName = localObj == null ? null : nameOf(localObj);
        if (localObj == null && username != null)
            for (Object p : players) if (username.equals(nameOf(p))) { localObj = p; localName = username; break; }

        // (Re)build models per class from the live samples.
        Map<Class<?>, List<Object>> byClass = new LinkedHashMap<Class<?>, List<Object>>();
        for (Object p : players) addSample(byClass, p);
        if (localObj != null) addSample(byClass, localObj);
        long now = System.currentTimeMillis();
        boolean rebuild = now - lastModelRebuild > 15000;
        boolean built = false;
        for (Map.Entry<Class<?>, List<Object>> e : byClass.entrySet()) {
            Class<?> c = e.getKey();
            Long last = lastModelAttempt.get(c);
            Model have = models.get(c);
            boolean noLook = have != null && have.look == null && (last == null || now - last > 2000);    // built on a bad moment (NaN look vector): do not wait 15 s
            if ((have == null && (last == null || now - last > 1000)) || (rebuild && have != null) || noLook) {
                lastModelAttempt.put(c, now);
                Model m = buildModel(c, e.getValue());
                if (m != null) { models.put(c, m); built = true; }
            }
        }
        if (rebuild) lastModelRebuild = now;
        if (built) exportLayout();

        double[] p3 = new double[3];
        float[] o2 = new float[2];
        float[] sz2 = new float[2];
        if (localObj != null) {
            Model m = models.get(localObj.getClass());
            if (m != null && m.position(localObj, p3)) {
                m.orient(localObj, o2);
                localYawFresh = !Float.isNaN(o2[0]);
                holdYaw(localObj, o2);
                boolean hasSz = m.size(localObj, sz2);
                s.local = new Snapshot.Rec(localName, p3[0], p3[1], p3[2], o2[0], o2[1], m.health(localObj),
                        hasSz ? sz2[0] : Float.NaN, hasSz ? sz2[1] : Float.NaN, uuidOf(localObj));
            }
        }
        for (Object p : players) {
            if (p == localObj) continue;
            String nm = nameOf(p);
            if (localName != null && localName.equals(nm)) continue;
            Model m = models.get(p.getClass());
            if (m == null || !m.position(p, p3)) continue;
            m.orient(p, o2);
            holdYaw(p, o2);
            boolean hasSz = m.size(p, sz2);
            s.players.add(new Snapshot.Rec(nm, p3[0], p3[1], p3[2], o2[0], o2[1], m.health(p),
                    hasSz ? sz2[0] : Float.NaN, hasSz ? sz2[1] : Float.NaN, uuidOf(p)));
        }
        if (s.local != null && !Float.isNaN(s.local.pitch)) {      // yaw may be NaN only when looking straight up/down with no yaw seen yet
            double[] eye = {s.local.x, s.local.y + 1.62, s.local.z};
            ensureAux();
            if (localYawFresh) camScan.observe(eye, s.local.yaw, s.local.pitch);   // never learn from a held (stale) yaw
            s.camera = camScan.read(eye, s.local.yaw);
            // Items change slowly: they are re-polled at 10 Hz by pollItemsIfDue() AFTER the snapshot was sent, so a slow
            // poll can never delay a camera sample, and they go on the wire only when they changed (itemsFresh).
            lastEye = eye;
            s.itemsFresh = itemsDirty;
            if (itemsDirty) { s.items.addAll(lastItems); itemsDirty = false; }
            if (!camScan.describe.equals(exportedCam) || !itemScan.describe.equals(exportedItems)) {
                exportedCam = camScan.describe; exportedItems = itemScan.describe;
                exportLayout();
            }
        }
        if (cfg.dump) dumper.maybeWrite();
        s.status = s.local == null ? "connected, but local player not found (set user=NAME?)"
                                   : "agent ok: " + s.players.size() + " player(s), " + lastItems.size() + " item(s) [" + itemScan.summary() + "]"
                                     + (playerRank > 0 ? " [players via " + playerSource() + "]" : "") + (s.camera == null ? ", camera: reconstructing" : (Float.isNaN(s.camera.fov) ? ", camera: exact, FOV: configured" : ", camera+FOV: exact"));
        return s;
    }

    /**
     * Re-polls the nearby item entities (10 Hz). Called by the sender thread right after a snapshot went out, so the
     * (possibly several ms long) entity walk happens in the idle part of the period instead of between reading the
     * camera and sending it.
     */
    void pollItemsIfDue() {
        double[] eye = lastEye;
        if (eye == null) return;
        long now = System.currentTimeMillis();
        if (now - lastItemPoll < 100) return;
        lastItemPoll = now;
        List<Snapshot.Item> polled = itemScan.poll(eye, 48);
        if (polled != null) { lastItems = polled; itemsDirty = true; }   // null = transient read failure: keep the last frame
    }

    /** Runs the second graph search (camera candidates + entity containers) once per discovery. */
    private void ensureAux() {
        if (auxDone) return;
        long now = System.currentTimeMillis();
        if (now < auxNextTry) return;
        try {
            Acc rootStep = containerPath.steps[0];
            Object root = rootStep.getObj(null);
            if (root == null) { auxNextTry = now + 3000; return; }
            aux.search(root, rootStep, 6, 120000, 3000);
            // The level can be buried deep below a huge root graph; search again from the level itself.
            Node holder = holderNode();
            if (holder != null) aux.searchEntitiesFrom(holder, 5, 60000, 2500);
            camScan.init(aux.cameraCands);
            itemScan.setContainers(aux.entityContainers);
            auxDone = true;
            Log.info("Aux search: " + aux.cameraCands.size() + " camera candidate(s), " + aux.entityContainers.size() + " entity container(s).");
        } catch (Throwable t) {
            Log.info("Aux search failed: " + t);
            auxNextTry = now + 10000;
        }
    }

    private static void addSample(Map<Class<?>, List<Object>> m, Object p) {
        List<Object> l = m.get(p.getClass());
        if (l == null) { l = new ArrayList<Object>(); m.put(p.getClass(), l); }
        l.add(p);
    }

    // ------------------------------------------------------------------ export of discovered offsets

    private static String esc(String s) {
        if (s == null) return "null";
        StringBuilder b = new StringBuilder("\"");
        for (char ch : s.toCharArray()) {
            if (ch == '"' || ch == '\\') b.append('\\').append(ch);
            else if (ch < 0x20) b.append(' ');
            else b.append(ch);
        }
        return b.append('"').toString();
    }

    private static String accJson(Acc a) {
        return "{\"class\":" + esc(a.owner.getName()) + ",\"field\":" + esc(a.f.getName())
                + ",\"type\":" + esc(a.f.getType().getName()) + ",\"static\":" + a.isStatic
                + ",\"offset\":" + (a.off >= 0 ? Long.toString(a.off) : "null") + "}";
    }

    private static String pathJson(FieldPath p) {
        if (p == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < p.steps.length; i++) { if (i > 0) b.append(','); b.append(accJson(p.steps[i])); }
        return b.append(']').toString();
    }

    /**
     * Writes everything that was discovered to esp_layout.json: the field paths and the
     * in-object byte offsets (as reported by Unsafe) - i.e. the "offsets" for THIS exact game
     * version / mapping / JVM. Offsets inside an object can be reused with ReadProcessMemory.
     * (Deliberately does NOT include the launch command line - it contains your access token.)
     */
    private void exportLayout() {
        try {
            StringBuilder b = new StringBuilder();
            b.append("{\n  \"generated\": ").append(esc(new Date().toString()))
             .append(",\n  \"java\": ").append(esc(System.getProperty("java.version") + " / " + System.getProperty("java.vm.name")))
             .append(",\n  \"username\": ").append(esc(username))
             .append(",\n  \"unsafe\": ").append(Mem.OK)
             .append(",\n  \"gameProfileClass\": ").append(esc(gameProfile == null ? null : gameProfile.getName()))
             .append(",\n  \"playerSource\": ").append(esc(playerSource()))
             .append(",\n  \"playerListPath\": ").append(pathJson(containerPath))
             .append(",\n  \"localPlayerPath\": ").append(pathJson(localPath))
             .append(",\n  \"camera\": ").append(esc(camScan.describe))
             .append(",\n  \"items\": ").append(esc(itemScan.describe))
             .append(",\n  \"playerClasses\": [");
            boolean first = true;
            for (Map.Entry<Class<?>, Model> e : models.entrySet()) {
                Model m = e.getValue();
                if (!first) b.append(',');
                first = false;
                b.append("\n    {\"class\": ").append(esc(e.getKey().getName())).append(", \"position\": ");
                if (m.pos.vec) {
                    b.append("{\"kind\":\"vec3\",\"field\":").append(accJson(m.pos.vecField))
                     .append(",\"x\":").append(accJson(m.pos.xyz[0])).append(",\"y\":").append(accJson(m.pos.xyz[1]))
                     .append(",\"z\":").append(accJson(m.pos.xyz[2])).append('}');
                } else {
                    b.append("{\"kind\":\"doubles\",\"x\":").append(accJson(m.pos.xyz[0])).append(",\"y\":")
                     .append(accJson(m.pos.xyz[1])).append(",\"z\":").append(accJson(m.pos.xyz[2])).append('}');
                }
                b.append(", \"lookMethod\": ").append(m.look == null ? "null"
                        : esc(m.look.getDeclaringClass().getName() + "." + m.look.getName() + "() -> " + m.look.getReturnType().getName()))
                 .append(", \"healthMethod\": ").append(m.healthMethod == null ? "null"
                        : esc(m.healthMethod.getDeclaringClass().getName() + "." + m.healthMethod.getName() + "()"))
                 .append('}');
            }
            b.append("\n  ]\n}\n");
            File dir = new File(cfg.exportDir);
            if (!dir.isDirectory()) dir.mkdirs();
            File out = new File(dir, "esp_layout.json");
            FileOutputStream fos = new FileOutputStream(out);
            try { fos.write(b.toString().getBytes("UTF-8")); } finally { fos.close(); }
        } catch (Throwable t) {
            Log.warn("Could not write layout export: " + t);
        }
    }
}
