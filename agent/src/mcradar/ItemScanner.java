package mcradar;

import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.HashMap;
import java.util.IdentityHashMap;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Finds dropped-item entities and reads their stack (type, count, enchantments, tags) - version independent.
 *
 *  ItemStack     = a small non-JDK class with an int field (the count), no doubles, and an item id that can be resolved:
 *                  either a public no-arg String method returning a translation key ("item.minecraft.diamond_sword"),
 *                  or - when the stack has no such method - through its item reference: an Item object with such a method,
 *                  or a registry Holder that prints "ResourceKey[minecraft:item / minecraft:diamond_sword]".
 *  Item entity   = an entity-like class (it has a position) from which a stack is REACHABLE through a short path of small
 *                  objects. In real Minecraft the ItemEntity has no stack field: the stack lives in
 *                  entityData -> DataItem[] -> value, so the path is found by a bounded graph search and replayed per entity.
 *                  Item frames / arrows / ... are filtered by hitbox size (a dropped item is ~0.25 blocks).
 *  Containers    = AuxSearch's entity containers (the level's entity storage); the largest one is polled.
 */
final class ItemScanner {
    private final Probe p;
    private final List<FieldPath> containers = new ArrayList<FieldPath>();
    private FieldPath best;
    private long lastPick;

    /** Count accessor + optional direct key method. `empty` negative results expire so a half-initialised object is retried. */
    private static final class StackModel { Method key; Acc count; }
    /** One hop of a stack path: a field (acc != null) or an element of a container/array snapshot (idx). */
    private static final class Step { final Acc acc; final int idx; Step(Acc a, int i) { acc = a; idx = i; } }
    private static final class ItemClass { Step[] path; Probe.Model model; String pathText; }
    private static final class Meta { int count; String key, ench, tags; int gen; }
    private static final class Neg { final long at; Neg(long t) { at = t; } }

    private final Map<Class<?>, Object> stackModels = new HashMap<Class<?>, Object>();      // StackModel | Neg
    private final Map<Class<?>, Object> itemClasses = new HashMap<Class<?>, Object>();      // ItemClass | Neg
    private final Map<Class<?>, Object> itemKeyMethods = new HashMap<Class<?>, Object>();   // Method | Boolean.FALSE
    private final IdentityHashMap<Object, Meta> meta = new IdentityHashMap<Object, Meta>();
    private int gen;
    private long lastDebug, lastNonEmpty = System.currentTimeMillis();
    private int entitiesSeen, itemEntities;
    String describe = "items: no entity container found yet";

    private static final long NEG_TTL = 10000;
    private static final int PATH_MAX_HOPS = 6, PATH_MAX_NODES = 500, SMALL_CLASS_FIELDS = 16;
    private static final Pattern ITEM_KEY_TEXT = Pattern.compile("ResourceKey\\[[^/\\]]*(?:item|block)[^/\\]]*/\\s*([a-z0-9_.-]+:[a-z0-9_/.-]+)\\s*\\]");

    ItemScanner(Probe p) { this.p = p; }

    void setContainers(List<FieldPath> paths) {
        containers.clear();
        containers.addAll(paths);
        best = null;
        lastPick = 0;
        Log.info("ItemScanner: " + containers.size() + " entity container candidate(s).");
    }

    // ------------------------------------------------------------------ item id resolution

    /**
     * Public no-arg String method of `c` that returns a translation key for `sample`, or null. A positive is cached forever.
     * A negative is cached only when no candidate method THREW: the empty stack has a null item, so getDescriptionId()
     * throws on it - that says nothing about the class.
     */
    private Method keyMethodOf(Class<?> c, Object sample) {
        Object cached = itemKeyMethods.get(c);
        if (cached != null) return cached instanceof Method ? (Method) cached : null;
        Method found = null;
        boolean threw = false;
        try {
            if (!Probe.isJdkish(c)) {
                for (Method m : c.getMethods()) {
                    if (m.getParameterTypes().length != 0 || m.getReturnType() != String.class
                            || Modifier.isStatic(m.getModifiers()) || m.getDeclaringClass() == Object.class) continue;
                    try {
                        m.setAccessible(true);
                        Object r = m.invoke(sample);
                        if (r instanceof String && Tags.looksLikeItemKey((String) r)) { found = m; break; }
                    } catch (Throwable ignored) { threw = true; }
                }
            }
        } catch (Throwable ignored) { threw = true; }
        if (found != null) itemKeyMethods.put(c, found);
        else if (!threw) itemKeyMethods.put(c, Boolean.FALSE);
        return found;
    }

    /** Item id ("minecraft:diamond_sword") if `v` is an Item object or a registry Holder of one, else null. */
    private String idFromItemLike(Object v, int depth) {
        if (v == null) return null;
        Class<?> c = v.getClass();
        if (c.isPrimitive() || c.isArray() || v instanceof CharSequence || v instanceof Number) return null;
        if (p.isEntityClass(c)) return null;
        try {
            Method km = keyMethodOf(c, v);
            if (km != null) {
                Object r = km.invoke(v);
                if (r instanceof String && Tags.looksLikeItemKey((String) r)) return Tags.idFromKey((String) r);
            }
            String txt = Tags.safeText(v, 400);
            Matcher m = ITEM_KEY_TEXT.matcher(txt);
            if (m.find()) return m.group(1);
            if (depth < 1 && !Probe.isJdkish(c)) {                                   // Holder.Reference -> value (the Item)
                for (Acc a : p.refFieldsOf(c)) {
                    if (a.f.getType() != Object.class) continue;                      // Holder.value is an erased generic `T`; never walk into registries
                    String id = idFromItemLike(a.getObj(v), depth + 1);
                    if (id != null) return id;
                }
            }
        } catch (Throwable ignored) { }
        return null;
    }

    /** Id of the item in `stack`: via the stack's own key method, else through its item reference. null if unknown. */
    private String resolveId(Object stack, StackModel sm) {
        if (sm != null && sm.key != null) {
            try {
                Object k = sm.key.invoke(stack);
                if (k instanceof String && Tags.looksLikeItemKey((String) k)) return Tags.idFromKey((String) k);
            } catch (Throwable ignored) { }
        }
        try {
            for (Acc a : p.refFieldsOf(stack.getClass())) {
                String id = idFromItemLike(a.getObj(stack), 0);
                if (id != null) return id;
            }
        } catch (Throwable ignored) { }
        return null;
    }

    private static boolean isAir(String id) { return id == null || id.endsWith(":air") || id.equals("air") || id.endsWith("_air"); }

    // ------------------------------------------------------------------ stack classification

    /**
     * Structural test, independent of the instance's value (ItemStack.EMPTY has count 0 and must not condemn the class).
     * Returns null when the object is not a stack; negatives expire after NEG_TTL so half-initialised objects are retried.
     */
    private StackModel stackModel(Object v) {
        Class<?> c = v.getClass();
        Object cached = stackModels.get(c);
        if (cached instanceof StackModel) return (StackModel) cached;
        if (cached instanceof Neg && System.currentTimeMillis() - ((Neg) cached).at < NEG_TTL) return null;
        StackModel sm = null;
        try {
            if (!Probe.isJdkish(c) && !c.isArray() && !c.isInterface() && !p.isEntityClass(c)) {
                int total = 0, doubles = 0;
                Acc count = null;
                for (Class<?> k = c; k != null && k != Object.class; k = k.getSuperclass())
                    for (Field f : k.getDeclaredFields()) {
                        if (Modifier.isStatic(f.getModifiers())) continue;
                        total++;
                        if (f.getType() == double.class) doubles++;
                        else if (f.getType() == int.class && count == null) {
                            Acc a = Acc.of(f);
                            int n = a == null ? Integer.MIN_VALUE : a.getInt(v);
                            if (n >= 0 && n <= 999) count = a;           // first declared int that is a plausible count (0 = empty stack)
                        }
                    }
                if (count != null && doubles == 0 && total <= 24) {
                    StackModel cand = new StackModel();
                    cand.count = count;
                    cand.key = keyMethodOf(c, v);
                    if (cand.key != null || resolveId(v, cand) != null) sm = cand;
                    else if (hasNullRef(v)) return null;          // maybe ItemStack.EMPTY (item == null): inconclusive, decide on another instance
                }
            }
        } catch (Throwable ignored) { }
        stackModels.put(c, sm == null ? (Object) new Neg(System.currentTimeMillis()) : sm);
        return sm;
    }

    /** True if some reference field of `v` (other than Strings) is null. */
    private boolean hasNullRef(Object v) {
        try { for (Acc a : p.refFieldsOf(v.getClass())) if (a.getObj(v) == null) return true; } catch (Throwable ignored) { }
        return false;
    }

    /** True if `v` is a stack that currently holds something (count >= 1, not air). */
    private boolean nonEmptyStack(Object v) {
        StackModel sm = stackModel(v);
        if (sm == null) return false;
        int n = sm.count.getInt(v);
        return n >= 1 && n <= 999 && !isAir(resolveId(v, sm));
    }

    private boolean smallClass(Class<?> c) {
        int n = 0;
        for (Class<?> k = c; k != null && k != Object.class; k = k.getSuperclass())
            for (Field f : k.getDeclaredFields()) if (!Modifier.isStatic(f.getModifiers()) && ++n > SMALL_CLASS_FIELDS) return false;
        return true;
    }

    private static final class PNode { final Object obj; final PNode parent; final Step via; final int hops;
        PNode(Object o, PNode pa, Step v, int h) { obj = o; parent = pa; via = v; hops = h; } }

    private static Step[] stepsOf(PNode n) {
        ArrayList<Step> l = new ArrayList<Step>();
        for (PNode x = n; x != null && x.via != null; x = x.parent) l.add(0, x.via);
        return l.toArray(new Step[0]);
    }

    /**
     * Bounded breadth-first search from an entity for stack objects reachable through small objects, arrays and containers.
     * Prefers a stack that is non-empty now (DataItem.initialValue holds ItemStack.EMPTY next to the real `value`).
     * `sawEmptyStack` is set when only empty stacks were found (an item entity not yet synced) so the caller can retry later.
     */
    private Step[] findStackPath(Object entity, boolean[] sawEmptyStack) {
        IdentityHashMap<Object, Boolean> seen = new IdentityHashMap<Object, Boolean>();
        ArrayDeque<PNode> q = new ArrayDeque<PNode>();
        PNode root = new PNode(entity, null, null, 0);
        seen.put(entity, Boolean.TRUE);
        q.add(root);
        Step[] emptyHit = null;
        int visited = 0;
        while (!q.isEmpty() && visited++ < PATH_MAX_NODES) {
            PNode n = q.poll();
            List<Object> kids = new ArrayList<Object>();
            List<Step> via = new ArrayList<Step>();
            if (Probe.isContainer(n.obj)) {
                Object[] el = Probe.elementsRetry(n.obj);
                if (el != null) for (int i = 0; i < el.length && i < 64; i++) { kids.add(el[i]); via.add(new Step(null, i)); }
            } else {
                for (Acc a : p.refFieldsOf(n.obj.getClass())) { kids.add(a.getObj(n.obj)); via.add(new Step(a, -1)); }
            }
            for (int i = 0; i < kids.size(); i++) {
                Object v = kids.get(i);
                if (v == null) continue;
                Class<?> c = v.getClass();
                if (c.isPrimitive() || v instanceof CharSequence || v instanceof Number || v instanceof Boolean) continue;
                if (seen.put(v, Boolean.TRUE) != null) continue;
                PNode child = new PNode(v, n, via.get(i), n.hops + 1);
                if (!Probe.isContainer(v)) {
                    if (stackModel(v) != null) {
                        if (nonEmptyStack(v)) return stepsOf(child);
                        if (emptyHit == null) emptyHit = stepsOf(child);
                        continue;
                    }
                    if (Probe.isJdkish(c) || p.isPlayerClass(c) || p.isEntityClass(c) || p.isWorldClass(c) || !smallClass(c)) continue;
                }
                if (child.hops < PATH_MAX_HOPS) q.add(child);
            }
        }
        if (emptyHit != null && sawEmptyStack != null) sawEmptyStack[0] = true;
        return null;
    }

    private Object follow(Object entity, Step[] path) {
        Object cur = entity;
        for (Step s : path) {
            if (cur == null) return null;
            if (s.acc != null) cur = s.acc.getObj(cur);
            else {
                Object[] el = Probe.elements(cur);
                cur = el != null && s.idx < el.length ? el[s.idx] : null;
            }
        }
        return cur;
    }

    private static String pathText(Step[] path) {
        StringBuilder b = new StringBuilder();
        for (Step s : path) {
            if (b.length() > 0) b.append(" -> ");
            b.append(s.acc != null ? s.acc.owner.getSimpleName() + "." + s.acc.f.getName() : "[" + s.idx + "]");
        }
        return b.toString();
    }

    private int searchesThisPoll;

    /** Per entity CLASS: is it an item entity, and how to reach its stack. */
    private ItemClass classify(Object e) {
        Class<?> c = e.getClass();
        Object cached = itemClasses.get(c);
        if (cached instanceof ItemClass) return (ItemClass) cached;
        if (cached instanceof Neg && System.currentTimeMillis() - ((Neg) cached).at < NEG_TTL) return null;
        if (searchesThisPoll >= 40) return null;                  // bound the cost per poll
        searchesThisPoll++;
        ItemClass res = null;
        boolean[] sawEmpty = new boolean[1];
        try {
            if (!Probe.isJdkish(c) && !p.isPlayerClass(c) && p.isEntityClass(c)) {
                Step[] path = findStackPath(e, sawEmpty);
                if (path != null) {
                    List<Object> sample = new ArrayList<Object>();
                    sample.add(e);
                    Probe.Model m = p.buildModel(c, sample);
                    if (m != null) { res = new ItemClass(); res.path = path; res.model = m; res.pathText = pathText(path); }
                }
            }
        } catch (Throwable ignored) { }
        if (res != null) {
            itemClasses.put(c, res);
            Log.info("Item entity class: " + c.getName() + " (stack via " + res.pathText + ")");
        } else if (!sawEmpty[0]) {
            itemClasses.put(c, new Neg(System.currentTimeMillis()));     // sawEmpty: a not-yet-synced item entity - do not cache, retry
        }
        return res;
    }

    /** Stack of an entity through its cached path; re-searches (bounded) when the path went stale. */
    private Object stackOf(Object e, ItemClass ic) {
        Object s = follow(e, ic.path);
        if (s != null && stackModel(s) != null) return s;
        if (searchesThisPoll >= 40) return null;
        searchesThisPoll++;
        Step[] np = findStackPath(e, null);
        if (np == null) return null;
        ic.path = np; ic.pathText = pathText(np);
        s = follow(e, np);
        return s != null && stackModel(s) != null ? s : null;
    }

    // ------------------------------------------------------------------ stack contents

    private Meta readMeta(Object stack, StackModel sm, int count, String id) {
        Meta m = meta.get(stack);
        if (m != null && m.count == count) { m.gen = gen; return m; }
        m = new Meta();
        m.count = count;
        m.gen = gen;
        m.key = id;

        Set<String> ench = new LinkedHashSet<String>();
        StringBuilder tags = new StringBuilder();
        try {
            for (Acc a : p.refFieldsOf(stack.getClass())) {
                Object v = a.getObj(stack);
                if (v == null || p.isEntityClass(v.getClass())) continue;        // back-reference to the item entity
                if (idFromItemLike(v, 0) != null) continue;                      // the Item / Holder: not tag data
                String txt = Tags.safeText(v, 1500);
                if (!txt.isEmpty() && !txt.equals("{}") && !txt.equals("[]") && !txt.equals("null")) {
                    if (tags.length() > 0) tags.append(" | ");
                    tags.append(txt);
                }
                Tags.enchantsFromText(txt, ench);
                Tags.findEnchantMaps(p, v, ench);
            }
        } catch (Throwable ignored) { }
        m.ench = join(ench);
        m.tags = tags.length() > 900 ? tags.substring(0, 900) + "..." : tags.toString();
        meta.put(stack, m);
        return m;
    }

    private static String join(Set<String> s) {
        StringBuilder b = new StringBuilder();
        for (String x : s) { if (b.length() > 0) b.append(','); b.append(x); }
        return b.toString();
    }

    // ------------------------------------------------------------------ polling

    /** Nearest `max` dropped items; null on a transient read failure (caller keeps the previous frame). */
    List<Snapshot.Item> poll(double[] eye, int max) {
        List<Snapshot.Item> out = new ArrayList<Snapshot.Item>();
        if (containers.isEmpty()) return out;
        long now = System.currentTimeMillis();
        searchesThisPoll = 0;
        Object[] elements = null;
        if (best == null || now - lastPick > 3000) {                    // re-pick: the container holding item entities, then the biggest
            lastPick = now;
            FieldPath bp = null; int bn = 0, bs = -1;
            for (FieldPath fp : containers) {
                Object c = fp.follow();
                Object[] el = c == null ? null : Probe.elementsRetry(c);
                if (el == null || el.length == 0) continue;
                int score = 0;
                Map<Class<?>, Object> samples = new HashMap<Class<?>, Object>();
                for (Object e : el) if (e != null && !samples.containsKey(e.getClass())) samples.put(e.getClass(), e);
                for (Object e : samples.values()) classify(e);                   // learn which classes are item entities
                for (Object e : el) if (e != null && itemClasses.get(e.getClass()) instanceof ItemClass) score++;
                if (score > bs || (score == bs && el.length > bn)) { bs = score; bn = el.length; bp = fp; elements = el; }
            }
            if (bp != null && bp != best) { best = bp; describe = "items: " + bp.describe(); Log.info("Entity container: " + describe + "  (" + bn + " entities, " + bs + " item entities)"); }
            if (bp == null) return out;
        }
        if (elements == null) {
            Object c = best == null ? null : best.follow();
            elements = c == null ? null : Probe.elementsRetry(c);
            if (elements == null) { best = null; return null; }
        }

        ++gen;
        double[] pos = new double[3];
        float[] size = new float[2];
        int seenEntities = 0, itemEnts = 0;
        for (Object e : elements) {
            if (e == null) continue;
            seenEntities++;
            ItemClass ic = classify(e);
            if (ic == null) continue;
            itemEnts++;
            try {
                Object stack = stackOf(e, ic);
                if (stack == null) continue;
                StackModel sm = stackModel(stack);
                if (sm == null) continue;
                int count = sm.count.getInt(stack);
                if (count < 1 || count > 999) continue;                  // empty / unsynced stack
                String id = resolveId(stack, sm);
                if (isAir(id)) continue;
                if (!ic.model.position(e, pos)) continue;
                float w = Float.NaN, h = Float.NaN;
                if (ic.model.size(e, size)) {
                    if (size[0] > 0.45f) continue;                       // not a dropped item (item frame, arrow, ...)
                    w = size[0]; h = size[1];
                }
                Meta m = readMeta(stack, sm, count, id);
                out.add(new Snapshot.Item(pos[0], pos[1], pos[2], w, h, count, m.key, m.ench, m.tags));
            } catch (Throwable ignored) { }
        }
        entitiesSeen = seenEntities; itemEntities = itemEnts;
        // forget stacks that vanished
        if (meta.size() > out.size() + 64) {
            java.util.Iterator<Map.Entry<Object, Meta>> it = meta.entrySet().iterator();
            while (it.hasNext()) if (it.next().getValue().gen != gen) it.remove();
        }
        final double ex = eye[0], ey = eye[1], ez = eye[2];
        Collections.sort(out, new Comparator<Snapshot.Item>() {      // nearest first (the encoder stops at the size budget)
            public int compare(Snapshot.Item a, Snapshot.Item b) {
                double da = (a.x - ex) * (a.x - ex) + (a.y - ey) * (a.y - ey) + (a.z - ez) * (a.z - ez);
                double db = (b.x - ex) * (b.x - ex) + (b.y - ey) * (b.y - ey) + (b.z - ez) * (b.z - ez);
                return Double.compare(da, db);
            }
        });
        if (!out.isEmpty()) lastNonEmpty = now;
        else if (seenEntities > 0 && now - lastDebug > 30000 && now - lastNonEmpty > 5000) { lastDebug = now; writeDebug(elements); }
        if (out.size() > max) return new ArrayList<Snapshot.Item>(out.subList(0, max));
        return out;
    }

    /** One-line summary for the HUD status. */
    String summary() { return "items " + itemEntities + "/" + entitiesSeen + " ent"; }

    // ------------------------------------------------------------------ diagnostics

    /**
     * Written when entities exist but no item was recognised: structure only (class / field names and types, counts) -
     * no player data, tokens or chat. Lets a failure on a game version that cannot be tested here be diagnosed.
     */
    private void writeDebug(Object[] elements) {
        try {
            StringBuilder b = new StringBuilder();
            b.append("esp_items_debug.txt  ").append(new Date()).append('\n');
            b.append("java ").append(System.getProperty("java.version")).append('\n');
            b.append("container: ").append(best == null ? "?" : best.describe()).append("\n");
            b.append("elements: ").append(elements.length).append("  container candidates: ").append(containers.size()).append('\n');
            Map<Class<?>, Integer> hist = new LinkedHashMap<Class<?>, Integer>();
            Map<Class<?>, Object> sampleOf = new HashMap<Class<?>, Object>();
            for (Object e : elements) if (e != null) {
                Integer n = hist.get(e.getClass());
                hist.put(e.getClass(), n == null ? 1 : n + 1);
                if (!sampleOf.containsKey(e.getClass())) sampleOf.put(e.getClass(), e);
            }
            b.append("\nALL ENTITY CLASSES IN THE CONTAINER (").append(hist.size()).append("):\n");
            boolean anyItemName = false;
            for (Map.Entry<Class<?>, Integer> en : hist.entrySet()) {
                Class<?> c = en.getKey();
                Object ic = itemClasses.get(c);
                if (c.getSimpleName().contains("Item")) anyItemName = true;
                b.append("  x").append(en.getValue()).append("  ").append(c.getName())
                 .append(p.isPlayerClass(c) ? "  [player]" : !p.isEntityClass(c) ? "  [not entity-like]" : "")
                 .append(ic instanceof ItemClass ? "  [ITEM ENTITY via " + ((ItemClass) ic).pathText + "]" : "").append('\n');
            }
            if (!anyItemName) b.append("\nNOTE: no class with \"Item\" in its name is in this container - dropped items are not stored here (or none exist nearby).\n");

            int shown = 0;
            for (Map.Entry<Class<?>, Integer> en : hist.entrySet()) {
                Class<?> c = en.getKey();
                if (!p.isEntityClass(c) || p.isPlayerClass(c) || itemClasses.get(c) instanceof ItemClass) continue;
                boolean itemish = c.getSimpleName().contains("Item");
                if (!itemish && shown >= 1) continue;                       // item-like classes first; otherwise one sample
                if (shown++ >= 4) break;
                b.append("\nREACHABLE FROM ").append(c.getName()).append(" (small objects only; the item stack should appear here):\n");
                dumpReach(b, sampleOf.get(c));
            }
            File dir = p.exportDirFile();
            if (!dir.isDirectory()) dir.mkdirs();
            FileOutputStream fos = new FileOutputStream(new File(dir, "esp_items_debug.txt"));
            try { fos.write(b.toString().getBytes("UTF-8")); } finally { fos.close(); }
            Log.info("No items recognised among " + elements.length + " entities - wrote esp_items_debug.txt");
        } catch (Throwable t) { Log.warn("Could not write items debug: " + t); }
    }

    /** Why `v` is not accepted as a stack although it may have the shape of one (an int, no doubles, few fields). */
    private String shapeNote(Object v) {
        try {
            Class<?> c = v.getClass();
            int total = 0, doubles = 0, ints = 0;
            for (Class<?> k = c; k != null && k != Object.class; k = k.getSuperclass())
                for (Field f : k.getDeclaredFields()) {
                    if (Modifier.isStatic(f.getModifiers())) continue;
                    total++;
                    if (f.getType() == double.class) doubles++; else if (f.getType() == int.class) ints++;
                }
            if (ints == 0 || doubles != 0 || total > 24) return null;
            StringBuilder b = new StringBuilder("   [stack-shaped, id NOT resolved; fields:");
            for (Acc a : p.refFieldsOf(c)) b.append(' ').append(a.f.getName()).append(a.getObj(v) == null ? "=null" : "=set");
            b.append("; String methods:");
            int n = 0;
            for (Method m : c.getMethods())
                if (m.getParameterTypes().length == 0 && m.getReturnType() == String.class && m.getDeclaringClass() != Object.class && n++ < 8) b.append(' ').append(m.getName());
            return b.append(']').toString();
        } catch (Throwable t) { return null; }
    }

    /** Lists the small objects reachable from an entity (class names, stack-likeness) - the search space of findStackPath. */
    private void dumpReach(StringBuilder b, Object entity) {
        if (entity == null) return;
        IdentityHashMap<Object, Boolean> seen = new IdentityHashMap<Object, Boolean>();
        ArrayDeque<PNode> q = new ArrayDeque<PNode>();
        seen.put(entity, Boolean.TRUE);
        q.add(new PNode(entity, null, null, 0));
        int lines = 0;
        while (!q.isEmpty() && lines < 80) {
            PNode n = q.poll();
            List<Object> kids = new ArrayList<Object>();
            List<Step> via = new ArrayList<Step>();
            if (Probe.isContainer(n.obj)) {
                Object[] el = Probe.elements(n.obj);
                if (el != null) for (int i = 0; i < el.length && i < 12; i++) { kids.add(el[i]); via.add(new Step(null, i)); }
            } else for (Acc a : p.refFieldsOf(n.obj.getClass())) { kids.add(a.getObj(n.obj)); via.add(new Step(a, -1)); }
            for (int i = 0; i < kids.size(); i++) {
                Object v = kids.get(i);
                if (v == null || v instanceof CharSequence || v instanceof Number || v instanceof Boolean) continue;
                if (seen.put(v, Boolean.TRUE) != null) continue;
                Class<?> c = v.getClass();
                if (p.isWorldClass(c)) continue;
                PNode child = new PNode(v, n, via.get(i), n.hops + 1);
                boolean container = Probe.isContainer(v);
                StackModel sm = container ? null : stackModel(v);
                // only deeper than the entity's own fields: skip the generic Entity/LivingEntity furniture unless it leads somewhere
                b.append("    ").append(pathText(stepsOf(child))).append(" : ").append(c.getName());
                if (sm != null) b.append("   <== STACK-LIKE count=").append(sm.count.getInt(v)).append(" id=").append(resolveId(v, sm));
                else if (!container) {
                    String note = shapeNote(v);
                    if (note != null) b.append(note);
                    else if (!Probe.isJdkish(c) && !p.isEntityClass(c) && !p.isPlayerClass(c) && !smallClass(c)) b.append("   [not expanded: more than " + SMALL_CLASS_FIELDS + " fields]");
                }
                b.append('\n'); lines++;
                if (sm == null && child.hops < 4 && (container || (!Probe.isJdkish(c) && !p.isEntityClass(c) && !p.isPlayerClass(c) && !p.isWorldClass(c) && smallClass(c)))) q.add(child);
            }
        }
    }
}
