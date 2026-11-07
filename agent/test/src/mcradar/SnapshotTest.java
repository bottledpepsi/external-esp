package mcradar;

import java.nio.charset.Charset;
import java.util.Locale;

/**
 * Unit tests for the wire encoder (Snapshot.encode / Snapshot.pct). Lives in package mcradar so it can see the
 * package-private classes; needs no JUnit: run with agent/test/run-java-tests.sh (prints FAIL lines, exit code 1 on failure).
 */
public final class SnapshotTest {
    private static int fails = 0, checks = 0;

    private static void check(boolean ok, String what) {
        checks++;
        if (!ok) { fails++; System.out.println("FAIL: " + what); }
    }

    /**
     * What the encoder must write for the full sample at wire 5 (token 0123..., session abcdef01, number 7). The same text is in tests/wire_v5_sample.h
     * (kV5Full), which the C++ parser tests parse. It is a literal on both sides, not a file, so nothing between two repositories can alter it.
     */
    static final String V5_FULL =
            "MCRADAR 5\n" +
            "K 0123456789abcdef0123456789abcdef\n" +
            "Q abcdef01 7\n" +
            "T 123456789012345\n" +
            "S agent ok: 1 other player(s)\n" +
            "C 97.9276 65.6200 -45.4497 -175.83 18.91 68.02 2\n" +
            "L Alice 100.000 64.000 -50.000 12.50 -3.25 17.50 0.60 1.80 069a79f4-44e9-4726-a5be-fca90e38aaf5\n" +
            "P Bob 90.500 70.000 -60.250 12.50 -3.25 17.50 0.60 1.80 069a79f4-44e9-4726-a5be-fca90e38aaf5\n" +
            "F 1\n" +
            "I 103.000 64.000 -47.000 0.25 0.25 1 minecraft:diamond_sword minecraft:sharpness@5,minecraft:unbreaking@3 %7BEnchantments:%5B%7Bid:%22minecraft:sharpness%22%7D%5D%7D\n" +
            "I 98.000 64.000 -52.000 - - 37 minecraft:cobblestone - -\n";

    /**
     * What the encoder must write for the full sample plus three entities at wire 6 (same token, session and number as V5_FULL). The same text is
     * in tests/wire_v6_sample.h (kV6Full), which the C++ parser tests parse.
     */
    static final String V6_FULL =
            "MCRADAR 6\n" +
            "K 0123456789abcdef0123456789abcdef\n" +
            "Q abcdef01 7\n" +
            "T 123456789012345\n" +
            "S agent ok: 1 other player(s)\n" +
            "C 97.9276 65.6200 -45.4497 -175.83 18.91 68.02 2\n" +
            "L Alice 100.000 64.000 -50.000 12.50 -3.25 17.50 0.60 1.80 069a79f4-44e9-4726-a5be-fca90e38aaf5\n" +
            "P Bob 90.500 70.000 -60.250 12.50 -3.25 17.50 0.60 1.80 069a79f4-44e9-4726-a5be-fca90e38aaf5\n" +
            "F 1\n" +
            "I 103.000 64.000 -47.000 0.25 0.25 1 minecraft:diamond_sword minecraft:sharpness@5,minecraft:unbreaking@3 %7BEnchantments:%5B%7Bid:%22minecraft:sharpness%22%7D%5D%7D\n" +
            "I 98.000 64.000 -52.000 - - 37 minecraft:cobblestone - -\n" +
            "E 1234567 1 92.500 64.000 -47.250 0.60 1.95 20.00 Zombie\n" +
            "E 77 2 99.000 64.000 -44.000 0.90 1.40 10.00 Cow\n" +
            "E 4294967295 4 97.000 66.500 -48.000 0.50 0.50 - Arrow\n";

    /** The full sample plus the three entities of V6_FULL. */
    private static Snapshot fullSampleWithEntities() {
        Snapshot s = fullSample();
        s.entities.add(new Snapshot.Ent(1234567, Snapshot.KIND_HOSTILE, 92.5, 64, -47.25, 0.6f, 1.95f, 20f, "Zombie"));
        s.entities.add(new Snapshot.Ent(77, Snapshot.KIND_PASSIVE, 99, 64, -44, 0.9f, 1.4f, 10f, "Cow"));
        s.entities.add(new Snapshot.Ent(-1, Snapshot.KIND_PROJECTILE, 97, 66.5, -48, 0.5f, 0.5f, Float.NaN, "Arrow"));   // id -1 = 4294967295 on the wire
        return s;
    }

    /** The "full" sample snapshot (camera, local player, one player, two items): the source of full_v4.dgram and of V5_FULL. */
    private static Snapshot fullSample() {
        Snapshot full = new Snapshot();
        full.status = "agent ok: 1 other player(s)";
        full.sampleNs = 123456789012345L; full.hasSampleTime = true;
        full.camera = new Snapshot.Cam(97.9276, 65.62, -45.4497, -175.83f, 18.91f, 68.02f, 2);
        full.local = rec("Alice", 100, 64, -50);
        full.players.add(rec("Bob", 90.5, 70, -60.25));
        full.items.add(new Snapshot.Item(103, 64, -47, 0.25f, 0.25f, 1, "minecraft:diamond_sword",
                "minecraft:sharpness@5,minecraft:unbreaking@3", "{Enchantments:[{id:\"minecraft:sharpness\"}]}"));
        full.items.add(new Snapshot.Item(98, 64, -52, Float.NaN, Float.NaN, 37, "minecraft:cobblestone", null, null));
        return full;
    }

    /** First difference between two texts, readable in a CI log. */
    private static String firstDifference(String want, String got) {
        int n = Math.min(want.length(), got.length()), i = 0;
        while (i < n && want.charAt(i) == got.charAt(i)) i++;
        String[] w = want.split("\n", -1), g = got.split("\n", -1);
        int line = 0;
        for (int k = 0; k < Math.min(w.length, g.length); k++) { if (!w[k].equals(g[k])) { line = k; break; } line = k + 1; }
        return "first difference at char " + i + " (line " + (line + 1) + ")\n  want: " + (line < w.length ? w[line] : "<end>") + "\n  got:  " + (line < g.length ? g[line] : "<end>")
                + "\n  lengths: want " + want.length() + ", got " + got.length();
    }

    private static String enc(Snapshot s) { return new String(s.encode(), Charset.forName("UTF-8")); }

    private static Snapshot.Rec rec(String name, double x, double y, double z) {
        return new Snapshot.Rec(name, x, y, z, 12.5f, -3.25f, 17.5f, 0.6f, 1.8f, "069a79f4-44e9-4726-a5be-fca90e38aaf5");
    }

    /**
     * The datagrams the C++ parser tests read (tests/data/wire/*.dgram): produced by the REAL encoder, so the two languages are tested
     * against each other. Written by "SnapshotTest --dump DIR"; agent/test/run-java-tests.sh fails when they differ from the committed ones.
     */
    private static void dump(java.io.File dir) throws java.io.IOException {
        dir.mkdirs();
        Snapshot full = fullSample();
        write(dir, "full_v4.dgram", full);
        Snapshot v6 = fullSampleWithEntities();
        v6.wire = 6; v6.token = "0123456789abcdef0123456789abcdef"; v6.session = 0xABCDEF01; v6.seq = 7;
        write(dir, "full_v6.dgram", v6);
        Snapshot ins = fullSampleWithEntities();
        ins.wire = 6; ins.token = "0123456789abcdef0123456789abcdef"; ins.session = 0xABCDEF01; ins.seq = 8;
        ins.hasInspect = true; ins.inspectId = 1234567;
        ins.inspect.add(new String[] {"class", "Zombie > Monster > PathfinderMob > Mob > LivingEntity > Entity"});
        ins.inspect.add(new String[] {"position path", "Entity.position -> x,y,z"});
        ins.inspect.add(new String[] {"Entity.tickCount : int", "1200"});
        ins.inspect.add(new String[] {"Mob.name : String", "\"Evil Name\\nX 9 k v\""});
        write(dir, "inspect_v6.dgram", ins);

        Snapshot bare = new Snapshot();
        bare.itemsFresh = false;
        write(dir, "bare_v4.dgram", bare);

        Snapshot uni = new Snapshot();
        uni.status = "agent ok: \u00e9\u00e8 \ud83d\ude00";
        uni.local = rec("Alice", 1, 2, 3);
        uni.players.add(rec("J\u00fcrgen", 4, 5, 6));
        uni.players.add(rec("\u0416\u0435\u043d\u044f", 7, 8, 9));
        uni.players.add(rec("\ud83d\ude00", 10, 11, 12));
        write(dir, "unicode_v4.dgram", uni);

        Snapshot crowd = new Snapshot();
        crowd.sampleNs = 5000000000L; crowd.hasSampleTime = true;
        crowd.camera = new Snapshot.Cam(0, 65.62, 0, 0f, 0f, 70f, 1);
        crowd.local = rec("Me", 0, 64, 0);
        for (int i = 0; i < 40; i++) crowd.players.add(rec("Player" + i, i * 2.5, 64, -i * 1.5));
        for (int i = 0; i < 60; i++)
            crowd.items.add(new Snapshot.Item(i, 64, i / 2.0, 0.25f, 0.25f, 1 + i % 64, "minecraft:item_" + i, i % 3 == 0 ? "minecraft:efficiency@" + (i % 5 + 1) : null, null));
        write(dir, "crowd_v4.dgram", crowd);
    }

    private static void write(java.io.File dir, String name, Snapshot s) throws java.io.IOException {
        java.io.FileOutputStream o = new java.io.FileOutputStream(new java.io.File(dir, name));
        try { o.write(s.encode()); } finally { o.close(); }
    }

    public static void main(String[] args) throws Exception {
        if (args.length == 2 && args[0].equals("--dump")) { dump(new java.io.File(args[1])); return; }
        Locale saved = Locale.getDefault();

        // ---- layout of a full snapshot ----
        Snapshot s = new Snapshot();
        s.status = "agent ok: 1 other player(s)";
        s.sampleNs = 123456789012345L; s.hasSampleTime = true;
        s.camera = new Snapshot.Cam(97.9276, 65.62, -45.4497, -175.83f, 18.91f, 68.02f, 2);
        s.local = rec("Alice", 100, 64, -50);
        s.players.add(rec("Bob", 90.5, 70, -60.25));
        s.items.add(new Snapshot.Item(103, 64, -47, 0.25f, 0.25f, 1, "minecraft:diamond_sword",
                "minecraft:sharpness@5,minecraft:unbreaking@3", "{Enchantments:[{id:\"minecraft:sharpness\"}]}"));
        s.items.add(new Snapshot.Item(98, 64, -52, Float.NaN, Float.NaN, 37, "minecraft:cobblestone", null, null));
        String[] lines = enc(s).split("\n", -1);
        check(lines[0].equals("MCRADAR 4"), "header line");
        check(lines[1].equals("T 123456789012345"), "T line carries the nanoTime stamp");
        check(lines[2].equals("S agent ok: 1 other player(s)"), "S line");
        check(lines[3].equals("C 97.9276 65.6200 -45.4497 -175.83 18.91 68.02 2"), "C line: " + lines[3]);
        check(lines[4].equals("L Alice 100.000 64.000 -50.000 12.50 -3.25 17.50 0.60 1.80 069a79f4-44e9-4726-a5be-fca90e38aaf5"), "L line: " + lines[4]);
        check(lines[5].startsWith("P Bob 90.500 70.000 -60.250 "), "P line: " + lines[5]);
        check(lines[6].equals("F 1"), "F line precedes the items");
        check(lines[7].equals("I 103.000 64.000 -47.000 0.25 0.25 1 minecraft:diamond_sword minecraft:sharpness@5,minecraft:unbreaking@3 "
                + "%7BEnchantments:%5B%7Bid:%22minecraft:sharpness%22%7D%5D%7D"), "I line (percent-encoded tags): " + lines[7]);
        check(lines[8].equals("I 98.000 64.000 -52.000 - - 37 minecraft:cobblestone - -"), "unknown size / ench / tags are '-': " + lines[8]);
        check(lines[9].isEmpty() && lines.length == 10, "datagram ends with exactly one newline");
        check(lines[4].split(" ").length == 11, "L has 11 columns");

        // ---- the v5 sample is exactly the literal the C++ tests parse ----
        {
            Snapshot v5 = fullSample();
            v5.wire = 5; v5.token = "0123456789abcdef0123456789abcdef"; v5.session = 0xABCDEF01; v5.seq = 7;
            String got = enc(v5);
            check(got.equals(V5_FULL), "v5 sample differs from V5_FULL (and tests/wire_v5_sample.h): " + firstDifference(V5_FULL, got));
            check(V5_FULL.indexOf('\r') < 0 && V5_FULL.endsWith("\n") && !V5_FULL.endsWith("\n\n"), "V5_FULL has LF line ends and one final newline");
        }

        // ---- the v6 sample (entities) is exactly the literal the C++ tests parse ----
        {
            Snapshot v6 = fullSampleWithEntities();
            v6.wire = 6; v6.token = "0123456789abcdef0123456789abcdef"; v6.session = 0xABCDEF01; v6.seq = 7;
            String got = enc(v6);
            check(got.equals(V6_FULL), "v6 sample differs from V6_FULL (and tests/wire_v6_sample.h): " + firstDifference(V6_FULL, got));
            check(V6_FULL.indexOf('\r') < 0 && V6_FULL.endsWith("\n") && !V6_FULL.endsWith("\n\n"), "V6_FULL has LF line ends and one final newline");
            Snapshot v5 = fullSampleWithEntities();
            v5.wire = 5;
            check(!enc(v5).contains("\nE "), "v5 never carries E lines (the overlay would not read them)");
            Snapshot stale = fullSampleWithEntities();
            stale.wire = 6; stale.itemsFresh = false;
            check(!enc(stale).contains("\nE "), "no item list this time = no entity list either (they travel together)");
            Snapshot hostile = new Snapshot();
            hostile.wire = 6;
            hostile.entities.add(new Snapshot.Ent(5, Snapshot.KIND_OTHER, 1, 2, 3, Float.NaN, Float.NaN, Float.NaN, "Evil Name\nE 9 1 0 0 0 - - - X"));
            String[] hl = enc(hostile).split("\n", -1);
            int eLines = 0;
            for (String l : hl) if (l.startsWith("E ")) eLines++;
            check(eLines == 1 && hl[hl.length - 2].equals("E 5 0 1.000 2.000 3.000 - - - Evil%20Name%0AE%209%201%200%200%200%20%2D%20%2D%20%2D%20X"), "a label cannot start another line: " + hl[hl.length - 2]);
            Snapshot big = new Snapshot();
            big.wire = 6;
            for (int i = 0; i < 5000; i++) big.entities.add(new Snapshot.Ent(i, 1, i, 64, i, 0.6f, 1.8f, 20f, "Zombie"));
            check(enc(big).length() <= Snapshot.MAX_BYTES + 200 && enc(big).split("\n").length > 100, "the E lines stop at the datagram size budget");
        }

        // ---- v5: the session token is the second line, only at wire >= 5 and only when there is one ----
        {
            Snapshot k = new Snapshot();
            k.wire = 5; k.token = "0123456789abcdef0123456789abcdef";
            k.sampleNs = 42L; k.hasSampleTime = true;
            String[] kl = enc(k).split("\n");
            check(kl[0].equals("MCRADAR 5") && kl[1].equals("K 0123456789abcdef0123456789abcdef") && kl[2].equals("T 42"), "v5: header, K, then T: " + kl[0] + "|" + kl[1]);
            Snapshot k4 = new Snapshot();
            k4.wire = 4; k4.token = "0123456789abcdef0123456789abcdef";
            check(!enc(k4).contains("K "), "v4 never carries a K line");
            Snapshot q = new Snapshot();
            q.wire = 5; q.token = "0123456789abcdef0123456789abcdef"; q.session = 0xABCDEF01; q.seq = 7;
            q.sampleNs = 42L; q.hasSampleTime = true;
            String[] ql = enc(q).split("\n");
            check(ql[1].startsWith("K ") && ql[2].equals("Q abcdef01 7") && ql[3].equals("T 42"), "v5: Q follows K, precedes T: " + ql[2]);
            q.session = 1; q.seq = 0xFFFFFFFFL + 5;
            check(enc(q).contains("\nQ 00000001 4\n"), "the datagram number wraps at 2^32 and the session id is 8 hex digits: " + enc(q).split("\n")[2]);
            q.seq = 0;
            check(!enc(q).contains("\nQ "), "seq 0 = not numbered, no Q line");
            Snapshot q4 = new Snapshot();
            q4.wire = 4; q4.session = 5; q4.seq = 3;
            check(!enc(q4).contains("\nQ "), "v4 never carries a Q line");
            Snapshot k5 = new Snapshot();
            k5.wire = 5;
            check(enc(k5).startsWith("MCRADAR 5\nS ") && !enc(k5).contains("K "), "v5 without a token has no K line");
        }

        // ---- optional parts ----
        Snapshot none = new Snapshot();
        none.itemsFresh = false;
        String bare = enc(none);
        check(bare.equals("MCRADAR 4\nS ok\n"), "empty snapshot without stamp/items: " + bare.replace("\n", "|"));

        // ---- numbers: NaN / Infinity become '-' ----
        Snapshot nan = new Snapshot();
        nan.local = new Snapshot.Rec("N", 1, 2, 3, Float.NaN, Float.POSITIVE_INFINITY, Float.NEGATIVE_INFINITY, Float.NaN, Float.NaN, null);
        String l = enc(nan).split("\n")[2];
        check(l.equals("L N 1.000 2.000 3.000 - - - - - -"), "non-finite floats and a missing uuid: " + l);

        // ---- names: whitespace -> '_', empty -> '?' ----
        Snapshot nm = new Snapshot();
        nm.players.add(rec("two words\tand tab", 0, 0, 0));
        nm.players.add(rec("", 0, 0, 0));
        nm.players.add(rec(null, 0, 0, 0));
        String[] nl = enc(nm).split("\n");
        check(nl[2].startsWith("P two_words_and_tab "), "whitespace in names: " + nl[2]);
        check(nl[3].startsWith("P ? ") && nl[4].startsWith("P ? "), "empty and null names");

        // ---- status cannot inject extra lines ----
        Snapshot inj = new Snapshot();
        inj.status = "ok\nL Evil 0 0 0";
        String[] il = enc(inj).split("\n");
        check(il.length == 3 && il[1].equals("S ok L Evil 0 0 0"), "newline in status is flattened");

        // ---- percent encoding ----
        check(Snapshot.pct(null).equals("-") && Snapshot.pct("").equals("-"), "pct of empty is '-'");
        check(Snapshot.pct("a b").equals("a%20b"), "space");
        check(Snapshot.pct("-").equals("%2D"), "a literal '-' must not read as 'unknown'");
        check(Snapshot.pct("\u00e9").equals("%C3%A9"), "UTF-8 bytes of e-acute");
        check(Snapshot.pct("\ud83d\ude00").equals("%F0%9F%98%80"), "surrogate pair -> 4 UTF-8 bytes");
        check(Snapshot.pct("100%").equals("100%25"), "percent sign");
        check(Snapshot.pct("a\nb").equals("a%0Ab"), "newline");

        // ---- locale independence (decimal comma, non-ASCII digits) ----
        for (Locale loc : new Locale[] {Locale.GERMANY, Locale.FRANCE, new Locale("ar", "EG"), new Locale("th", "TH", "TH")}) {
            Locale.setDefault(loc);
            Snapshot ls = new Snapshot();
            ls.local = rec("A", 1.5, 2.25, 3.125);
            ls.camera = new Snapshot.Cam(1.5, 2.5, 3.5, 4.5f, 5.5f, 70f, 1);
            ls.items.add(new Snapshot.Item(1.5, 2, 3, 0.25f, 0.25f, 12, "k", null, "\u00e9"));
            String e = enc(ls);
            check(e.contains("L A 1.500 2.250 3.125 12.50 -3.25 17.50 0.60 1.80 ") && e.contains("C 1.5000 2.5000 3.5000 4.50 5.50 70.00 1")
                    && e.contains("%C3%A9") && e.matches("(?s)[\\x00-\\x7F]*"), "ASCII digits and '.' under locale " + loc);
        }
        Locale.setDefault(saved);

        // ---- size cap: many items stay under the UDP limit, nearest (first) items survive, no partial line ----
        Snapshot big = new Snapshot();
        big.local = rec("Alice", 0, 64, 0);
        for (int i = 0; i < 3000; i++)
            big.items.add(new Snapshot.Item(i, 64, 0, 0.25f, 0.25f, 1, "minecraft:diamond_sword", "minecraft:sharpness@5",
                    "{lots:of:tag:text:" + i + "}"));
        byte[] bytes = big.encode();
        check(bytes.length <= Snapshot.MAX_BYTES, "datagram <= MAX_BYTES, was " + bytes.length);
        check(bytes.length < 65507, "fits one UDP datagram");
        String[] bl = new String(bytes, Charset.forName("UTF-8")).split("\n", -1);
        check(bl[bl.length - 1].isEmpty(), "last line is complete");
        int nItems = 0;
        for (String x : bl) if (x.startsWith("I ")) nItems++;
        check(nItems > 100 && nItems < 3000, "kept " + nItems + " items");
        check(bl[bl.length - 2].startsWith("I " + (nItems - 1) + ".000 "), "items are kept in order (nearest first)");

        System.out.println(fails == 0 ? "ALL OK (" + checks + " checks)" : fails + " FAILURES of " + checks);
        System.exit(fails == 0 ? 0 : 1);
    }
}
