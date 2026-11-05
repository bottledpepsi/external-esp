package mcradar;

import java.io.File;
import java.io.OutputStream;
import java.io.PrintStream;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/** EntityKinds (class names -> category) and the entity scan end to end on the synthetic fixture worlds. */
public final class EntityKindsTest {
    private static int fails = 0, checks = 0;
    private static void check(boolean ok, String what) { checks++; if (!ok) { fails++; System.out.println("FAIL: " + what); } }

    private static String enc(Snapshot s, int wire) { s.wire = wire; return new String(s.encode(), java.nio.charset.Charset.forName("UTF-8")); }

    // a small class tree with Mojang-style names
    static class Entity { }
    static class LivingEntity extends Entity { }
    static class Mob extends LivingEntity { }
    static class PathfinderMob extends Mob { }
    static class Monster extends PathfinderMob { }
    static class Animal extends PathfinderMob { }
    static class Zombie extends Monster { }
    static class BossZombie extends Zombie { }
    static class Skeleton extends Monster { }
    static class Cow extends Animal { }
    static class Enderman extends Monster { }          // neutral although it extends Monster
    static class Wolf extends Animal { }               // neutral although it extends Animal
    static class Slimy extends Monster { }             // unknown name, hostile by its base
    static class Dodo extends Animal { }               // unknown name, passive by its base
    static class Blob extends Mob { }                  // unknown name, only a Mob
    static class Projectile extends Entity { }
    static class AbstractArrow extends Projectile { }
    static class Arrow extends AbstractArrow { }
    static class WeirdShot extends Projectile { }
    static class ExperienceOrb extends Entity { }
    static class Player extends LivingEntity { }
    static class RemotePlayer extends Player { }
    static class ItemEntity extends Entity { }
    static class ArmorStand extends LivingEntity { }
    static class Boat extends Entity { }
    static class ZombieEntity extends Monster { }      // Yarn style
    static class a extends Entity { }                  // obfuscated
    static class bgb extends a { }
    static class ModdedCreeper extends Monster { }

    public static void main(String[] args) throws Exception {
        PrintStream err = System.err;
        System.setErr(new PrintStream(new OutputStream() { public void write(int b) { } }));
        try {
            // ---- the tables ----
            check(EntityKinds.classify(Zombie.class) == Snapshot.KIND_HOSTILE && EntityKinds.classify(Skeleton.class) == Snapshot.KIND_HOSTILE, "zombie, skeleton: hostile");
            check(EntityKinds.classify(BossZombie.class) == Snapshot.KIND_HOSTILE, "a subclass of a listed class inherits its category");
            check(EntityKinds.classify(Cow.class) == Snapshot.KIND_PASSIVE, "cow: passive");
            check(EntityKinds.classify(Enderman.class) == Snapshot.KIND_NEUTRAL && EntityKinds.classify(Wolf.class) == Snapshot.KIND_NEUTRAL, "the leaf name wins over the base (enderman, wolf: neutral)");
            check(EntityKinds.classify(Slimy.class) == Snapshot.KIND_HOSTILE && EntityKinds.classify(Dodo.class) == Snapshot.KIND_PASSIVE, "an unknown name is decided by its base class");
            check(EntityKinds.classify(Blob.class) == Snapshot.KIND_OTHER && EntityKinds.classify(PathfinderMob.class) == Snapshot.KIND_OTHER, "a Mob that is nothing else: other");
            check(EntityKinds.classify(Arrow.class) == Snapshot.KIND_PROJECTILE && EntityKinds.classify(WeirdShot.class) == Snapshot.KIND_PROJECTILE, "arrows and anything under Projectile: projectile");
            check(EntityKinds.classify(ExperienceOrb.class) == Snapshot.KIND_XP, "experience orb");
            check(EntityKinds.classify(Player.class) == -1 && EntityKinds.classify(RemotePlayer.class) == -1, "players have their own records");
            check(EntityKinds.classify(ItemEntity.class) == -1 && EntityKinds.classify(ArmorStand.class) == -1 && EntityKinds.classify(Boat.class) == -1, "items, armor stands, boats: not drawn here");
            check(EntityKinds.classify(ZombieEntity.class) == Snapshot.KIND_HOSTILE, "Yarn-style names work too");
            check(EntityKinds.classify(a.class) == -1 && EntityKinds.classify(bgb.class) == -1 && EntityKinds.classify(Entity.class) == -1, "obfuscated names and a bare Entity: nothing, never a guess");
            check(EntityKinds.classify(null) == -1 && EntityKinds.classifyNames(new String[0]) == -1 && EntityKinds.classifyNames(null) == -1, "null and empty chains");
            check(EntityKinds.classify(new Object() { }.getClass()) == -1, "an anonymous class has no name: no match");
            check(EntityKinds.classify(int[].class) == -1, "an array class");
            check(EntityKinds.classify(ModdedCreeper.class) == Snapshot.KIND_HOSTILE, "a mod class under Monster");
            // chain: leaf first, Object excluded, bounded
            String[] ch = EntityKinds.chain(BossZombie.class);
            check(ch.length == 7 && ch[0].equals("BossZombie") && ch[1].equals("Zombie") && ch[ch.length - 1].equals("Entity"), "chain order: " + java.util.Arrays.toString(ch));
            check(EntityKinds.label(ZombieEntity.class).equals("Zombie") && EntityKinds.label(Cow.class).equals("Cow") && EntityKinds.label(new Object() { }.getClass()).equals("") && EntityKinds.label(null).equals(""), "labels drop a trailing Entity");
            check(EntityKinds.classifyNames(new String[] {"Entity"}) == -1 && EntityKinds.classifyNames(new String[] {"Zombie", "Entity"}) == Snapshot.KIND_HOSTILE, "chains as plain names");
            check(EntityKinds.classifyNames(new String[] {"Zombie", "Player"}) == -1, "a player anywhere in the chain excludes it");

            // ---- the scan, end to end ----
            File fx = new File(args.length > 0 ? args[0] : "test/fixtures");
            File dir = new File(fx, "synthetic-realistic");
            for (int wire : new int[] {5, 6}) {
                FixtureGame g = FixtureGame.load(dir, new HashSet<String>());
                Probe p = new Probe(g.inst(), AgentConfig.parse("user=Alice;wire=" + wire + ";export=" + new File(System.getProperty("java.io.tmpdir"), "esp-ent-" + wire).getPath().replace('\\', '/')));
                for (int i = 0; i < 3 && !p.discover(); i++) { }
                double t = 0;
                Snapshot last = null, fresh = null;
                for (int i = 0; i < 200; i++) { t += 0.006; g.tick(t); last = p.read(); p.pollItemsIfDue(); if (last.itemsFresh) fresh = last; Thread.sleep(2); }
                check(fresh != null && fresh.items.size() >= 3, "wire " + wire + ": items still found");
                if (wire == 5) {
                    check(fresh != null && fresh.entities.isEmpty() && p.items().entities().isEmpty(), "wire 5: the entities are not even scanned for");
                    check(!enc(fresh, 5).contains("\nE "), "wire 5: no E lines");
                } else {
                    List<Snapshot.Ent> es = fresh.entities;
                    check(!es.isEmpty(), "wire 6: mobs found");
                    Set<Integer> ids = new HashSet<Integer>();
                    boolean allMob = true, near = false;
                    for (Snapshot.Ent e : es) { ids.add(e.id); allMob &= e.kind == Snapshot.KIND_OTHER && e.label.equals("Mob"); near |= Math.abs(e.x - 105) < 3 && Math.abs(e.z + 55) < 3; }
                    check(ids.size() == es.size(), "wire 6: one id per entity object (" + ids.size() + "/" + es.size() + ")");
                    check(allMob, "wire 6: the fixture's mobs are 'other' with the label Mob");
                    check(near, "wire 6: a mob at the fixture position (105, 64, -55)");
                    check(!es.isEmpty() && es.get(0).w > 0.5f && es.get(0).h > 1.5f, "wire 6: the hitbox size is read");
                    check(p.items().entities().size() == es.size() && last.status.contains("mobs " + es.size()), "wire 6: the status counts them: " + last.status);
                    // no entity is a player or an item
                    for (Snapshot.Ent e : es) check(!(Math.abs(e.x - 100) < 0.01 && Math.abs(e.z + 50) < 0.01), "wire 6: Alice is not among the entities");
                    String enc = enc(fresh, 6);
                    check(enc.contains("\nE ") && enc.contains(" Mob\n"), "wire 6: the datagram carries E lines");
                }
            }
        } finally {
            System.setErr(err);
        }
        System.out.println(fails == 0 ? "ALL OK (" + checks + " checks)" : fails + " FAILURES of " + checks);
        System.exit(fails == 0 ? 0 : 1);
    }
}
