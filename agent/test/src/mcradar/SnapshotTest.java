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
        Snapshot full = new Snapshot();
        full.status = "agent ok: 1 other player(s)";
        full.sampleNs = 123456789012345L; full.hasSampleTime = true;
        full.camera = new Snapshot.Cam(97.9276, 65.62, -45.4497, -175.83f, 18.91f, 68.02f, 2);
        full.local = rec("Alice", 100, 64, -50);
        full.players.add(rec("Bob", 90.5, 70, -60.25));
        full.items.add(new Snapshot.Item(103, 64, -47, 0.25f, 0.25f, 1, "minecraft:diamond_sword",
                "minecraft:sharpness@5,minecraft:unbreaking@3", "{Enchantments:[{id:\"minecraft:sharpness\"}]}"));
        full.items.add(new Snapshot.Item(98, 64, -52, Float.NaN, Float.NaN, 37, "minecraft:cobblestone", null, null));
        write(dir, "full_v4.dgram", full);
        full.wire = 5; full.token = "0123456789abcdef0123456789abcdef"; full.session = 0xABCDEF01; full.seq = 7;
        write(dir, "full_v5.dgram", full);
        full.wire = 4; full.token = null; full.session = 0; full.seq = 0;

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
