package mcradar;

import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

/**
 * Which wire-v6 category (Snapshot.KIND_*) an entity belongs to, decided from its class: the simple name of the class and of every superclass up to
 * Object. Pure: no game access, unit tested with plain Java classes (EntityKindsTest).
 *
 * This only works when the game's class names are readable (Mojang or Yarn mappings; a client without obfuscation). With vanilla obfuscated names
 * ("bgb", "a") nothing matches, classify() returns -1 for everything and no E lines are written - the feature is simply absent, never wrong. Names that
 * mean nothing to the tables below are not guessed at: a class that is a living thing by its bases (LivingEntity / Mob) but is not listed is KIND_OTHER.
 *
 * Order of decision: not interesting (players, items, vehicles, markers ...) -> -1; experience orb -> XP; an exact class name in the neutral / hostile /
 * passive / projectile tables; then the base classes (Projectile ..., Monster ..., Animal ...); then Mob / LivingEntity -> OTHER.
 */
final class EntityKinds {
    private EntityKinds() { }

    private static Set<String> set(String... s) { return new HashSet<String>(Arrays.asList(s)); }

    /** Never drawn here: players and items have their own records; the rest is scenery, vehicles or invisible helpers. */
    private static final Set<String> EXCLUDED = set(
            "Player", "AbstractClientPlayer", "LocalPlayer", "RemotePlayer", "ServerPlayer", "ClientPlayerEntity", "OtherClientPlayerEntity", "PlayerEntity",
            "AbstractClientPlayerEntity", "ServerPlayerEntity",
            "ItemEntity", "ItemFrame", "GlowItemFrame", "ItemFrameEntity", "GlowItemFrameEntity", "HangingEntity", "AbstractDecorationEntity", "Painting", "PaintingEntity",
            "ArmorStand", "ArmorStandEntity", "Boat", "ChestBoat", "BoatEntity", "AbstractBoat", "Minecart", "AbstractMinecart", "AbstractMinecartContainer", "MinecartEntity",
            "AbstractMinecartEntity", "FallingBlockEntity", "PrimedTnt", "TntEntity", "LightningBolt", "LightningEntity", "AreaEffectCloud", "AreaEffectCloudEntity",
            "Marker", "MarkerEntity", "Interaction", "InteractionEntity", "DisplayEntity", "Display", "BlockDisplay", "ItemDisplay", "TextDisplay", "EndCrystal",
            "EndCrystalEntity", "LeashFenceKnotEntity", "LeashFenceKnot", "EvokerFangs", "EvokerFangsEntity");

    private static final Set<String> XP = set("ExperienceOrb", "ExperienceOrbEntity", "XpOrb", "EntityXPOrb");

    private static final Set<String> NEUTRAL = set(
            "Enderman", "EndermanEntity", "ZombifiedPiglin", "ZombifiedPiglinEntity", "PigZombie", "Wolf", "WolfEntity", "IronGolem", "IronGolemEntity", "Bee", "BeeEntity",
            "Llama", "LlamaEntity", "TraderLlama", "TraderLlamaEntity", "PolarBear", "PolarBearEntity", "Dolphin", "DolphinEntity", "Panda", "PandaEntity",
            "Goat", "GoatEntity", "SpiderJockey", "Spider", "SpiderEntity", "CaveSpider", "CaveSpiderEntity");

    private static final Set<String> HOSTILE = set(
            "Zombie", "ZombieEntity", "Husk", "HuskEntity", "Drowned", "DrownedEntity", "ZombieVillager", "ZombieVillagerEntity", "Skeleton", "SkeletonEntity", "Stray", "StrayEntity",
            "WitherSkeleton", "WitherSkeletonEntity", "Bogged", "BoggedEntity", "Creeper", "CreeperEntity", "Witch", "WitchEntity", "Slime", "SlimeEntity", "MagmaCube", "MagmaCubeEntity",
            "Ghast", "GhastEntity", "Blaze", "BlazeEntity", "Phantom", "PhantomEntity", "Pillager", "PillagerEntity", "Vindicator", "VindicatorEntity", "Evoker", "EvokerEntity",
            "Ravager", "RavagerEntity", "Vex", "VexEntity", "Guardian", "GuardianEntity", "ElderGuardian", "ElderGuardianEntity", "Shulker", "ShulkerEntity", "Silverfish", "SilverfishEntity",
            "Endermite", "EndermiteEntity", "Hoglin", "HoglinEntity", "Zoglin", "ZoglinEntity", "PiglinBrute", "PiglinBruteEntity", "Wither", "WitherEntity", "EnderDragon", "EnderDragonEntity",
            "Warden", "WardenEntity", "Breeze", "BreezeEntity", "Creaking", "CreakingEntity", "Giant", "GiantEntity", "Illusioner", "IllusionerEntity");

    private static final Set<String> PASSIVE = set(
            "Cow", "CowEntity", "Mooshroom", "MooshroomEntity", "MushroomCow", "Pig", "PigEntity", "Sheep", "SheepEntity", "Chicken", "ChickenEntity", "Horse", "HorseEntity",
            "Donkey", "DonkeyEntity", "Mule", "MuleEntity", "SkeletonHorse", "SkeletonHorseEntity", "ZombieHorse", "ZombieHorseEntity", "Cat", "CatEntity", "Ocelot", "OcelotEntity",
            "Rabbit", "RabbitEntity", "Fox", "FoxEntity", "Turtle", "TurtleEntity", "Cod", "CodEntity", "Salmon", "SalmonEntity", "Squid", "SquidEntity", "GlowSquid", "GlowSquidEntity",
            "Bat", "BatEntity", "Parrot", "ParrotEntity", "Villager", "VillagerEntity", "WanderingTrader", "WanderingTraderEntity", "Allay", "AllayEntity", "Axolotl", "AxolotlEntity",
            "Frog", "FrogEntity", "Tadpole", "TadpoleEntity", "Camel", "CamelEntity", "Sniffer", "SnifferEntity", "Strider", "StriderEntity", "TropicalFish", "TropicalFishEntity",
            "Pufferfish", "PufferfishEntity", "PufferFish", "Armadillo", "ArmadilloEntity", "SnowGolem", "SnowGolemEntity", "Piglin", "PiglinEntity", "Happy_Ghast", "HappyGhast");

    private static final Set<String> PROJECTILE_NAMES = set(
            "Arrow", "ArrowEntity", "SpectralArrow", "SpectralArrowEntity", "ThrownTrident", "TridentEntity", "Snowball", "SnowballEntity", "ThrownEnderpearl", "EnderPearlEntity",
            "ThrownEgg", "EggEntity", "Fireball", "SmallFireball", "LargeFireball", "FireballEntity", "SmallFireballEntity", "WitherSkull", "WitherSkullEntity", "ShulkerBullet",
            "ShulkerBulletEntity", "FishingHook", "FishingBobberEntity", "ThrownPotion", "PotionEntity", "ThrownExperienceBottle", "ExperienceBottleEntity", "FireworkRocketEntity",
            "DragonFireball", "DragonFireballEntity", "WindCharge", "WindChargeEntity", "BreezeWindCharge", "LlamaSpit", "LlamaSpitEntity", "EyeOfEnder", "EyeOfEnderEntity", "ThrownEnderEye");

    private static final Set<String> PROJECTILE_BASES = set(
            "Projectile", "ProjectileEntity", "AbstractArrow", "PersistentProjectileEntity", "ThrowableProjectile", "ThrownItemEntity", "AbstractHurtingProjectile",
            "ExplosiveProjectileEntity", "ThrowableItemProjectile", "AbstractWindCharge");

    private static final Set<String> HOSTILE_BASES = set("Monster", "HostileEntity", "Enemy", "PatrollingMonster", "PatrolEntity", "AbstractIllager", "RaiderEntity", "Raider",
            "AbstractSkeleton", "AbstractSkeletonEntity", "SpellcasterIllager", "SpellcastingIllagerEntity", "ZombieEntity", "GuardianEntity", "Guardian");

    private static final Set<String> PASSIVE_BASES = set("Animal", "AnimalEntity", "PassiveEntity", "AgeableMob", "WaterAnimal", "WaterCreatureEntity", "AmbientCreature", "AbstractFish",
            "FishEntity", "SchoolingFishEntity", "AbstractSchoolingFish", "TameableAnimal", "TameableEntity", "AbstractHorse", "AbstractHorseEntity", "AbstractVillager", "MerchantEntity",
            "AbstractChestedHorse", "AbstractDonkeyEntity");

    private static final Set<String> LIVING_BASES = set("Mob", "MobEntity", "LivingEntity", "PathfinderMob", "PathAwareEntity", "FlyingMob", "FlyingEntity", "Creature");

    /** The simple name of `c` and of every superclass below Object, leaf first. Anonymous / array classes give "" (never a match). */
    static String[] chain(Class<?> c) {
        java.util.ArrayList<String> l = new java.util.ArrayList<String>();
        for (Class<?> k = c; k != null && k != Object.class && l.size() < 24; k = k.getSuperclass()) {
            String n = k.getSimpleName();
            l.add(n == null ? "" : n);
        }
        return l.toArray(new String[0]);
    }

    /** Snapshot.KIND_* for a chain of class names (leaf first), or -1 when the entity is not one of the categories (or the names do not say). */
    static int classifyNames(String[] chain) {
        if (chain == null || chain.length == 0) return -1;
        for (String n : chain) if (EXCLUDED.contains(n)) return -1;
        for (String n : chain) if (XP.contains(n)) return Snapshot.KIND_XP;
        final String leaf = chain[0];
        if (NEUTRAL.contains(leaf)) return Snapshot.KIND_NEUTRAL;
        if (HOSTILE.contains(leaf)) return Snapshot.KIND_HOSTILE;
        if (PASSIVE.contains(leaf)) return Snapshot.KIND_PASSIVE;
        if (PROJECTILE_NAMES.contains(leaf)) return Snapshot.KIND_PROJECTILE;
        // a subclass (a mod's "BossZombie extends Zombie"): the nearest known name up the chain decides, the leaf excluded
        for (int i = 1; i < chain.length; i++) {
            String n = chain[i];
            if (NEUTRAL.contains(n)) return Snapshot.KIND_NEUTRAL;
            if (HOSTILE.contains(n)) return Snapshot.KIND_HOSTILE;
            if (PASSIVE.contains(n)) return Snapshot.KIND_PASSIVE;
            if (PROJECTILE_NAMES.contains(n)) return Snapshot.KIND_PROJECTILE;
        }
        for (String n : chain) if (PROJECTILE_BASES.contains(n)) return Snapshot.KIND_PROJECTILE;
        for (String n : chain) if (HOSTILE_BASES.contains(n)) return Snapshot.KIND_HOSTILE;
        for (String n : chain) if (PASSIVE_BASES.contains(n)) return Snapshot.KIND_PASSIVE;
        for (String n : chain) if (LIVING_BASES.contains(n)) return Snapshot.KIND_OTHER;
        return -1;
    }

    static int classify(Class<?> c) { return c == null ? -1 : classifyNames(chain(c)); }

    /** The class name shown as a label: the leaf's simple name, with a trailing "Entity" removed ("ZombieEntity" -> "Zombie"); "" for anonymous classes. */
    static String label(Class<?> c) {
        String n = c == null ? "" : c.getSimpleName();
        if (n == null) return "";
        if (n.endsWith("Entity") && n.length() > 6) n = n.substring(0, n.length() - 6);
        return n;
    }
}
