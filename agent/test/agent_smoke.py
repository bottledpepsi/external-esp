#!/usr/bin/env python3
"""Agent smoke test: loads the REAL agent jar into the fake games (no Minecraft) and checks the UDP datagrams it sends.

    python3 agent/test/agent_smoke.py [--java-home DIR] [--timeout SECONDS] [case ...]

Cases (default: all): fake, fake-old, fake-third, realistic, fake-v5 (wire=5 with a session token).
What it proves: the agent finds players / camera / FOV / items in games with Minecraft-like structure and that what goes on
the wire matches the format documented in Snapshot.java. What it does NOT prove: anything about real Minecraft, real Windows,
or obfuscated real-game class layouts.
Needs: a JDK 8+ (javac, jar, java) and Python 3.8+. Exit code 0 = all cases passed.
"""
import argparse, os, re, shutil, socket, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
AGENT = os.path.dirname(HERE)
OUT = os.path.join(HERE, "out-smoke")


def tool(name, java_home):
    exe = name + (".exe" if os.name == "nt" else "")
    if java_home:
        return os.path.join(java_home, "bin", exe)
    return shutil.which(name) or name


def run(cmd, **kw):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True, **kw)
    if r.returncode != 0:
        sys.exit("command failed: %s\n%s" % (" ".join(cmd), r.stdout))
    return r.stdout


def java_major(java):
    out = subprocess.run([java, "-version"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True).stdout
    m = re.search(r'version "(\d+)(?:\.(\d+))?', out)
    if not m:
        return 8
    return int(m.group(2)) if m.group(1) == "1" else int(m.group(1))


def build(java_home):
    shutil.rmtree(OUT, ignore_errors=True)
    for d in ("agent", "fake", "real"):
        os.makedirs(os.path.join(OUT, d))
    javac, jar = tool("javac", java_home), tool("jar", java_home)
    srcs = [os.path.join(AGENT, "src", "mcradar", f) for f in os.listdir(os.path.join(AGENT, "src", "mcradar")) if f.endswith(".java")]
    run([javac, "--release", "8", "-Xlint:-options", "-d", os.path.join(OUT, "agent")] + srcs)
    jar_path = os.path.join(OUT, "esp-agent.jar")
    run([jar, "cfm", jar_path, os.path.join(AGENT, "manifest.mf"), "-C", os.path.join(OUT, "agent"), "."])
    t = os.path.join(HERE, "src")
    run([javac, "-d", os.path.join(OUT, "fake"), os.path.join(t, "com/mojang/authlib/GameProfile.java"), os.path.join(t, "FakeGame.java")])
    run([javac, "-d", os.path.join(OUT, "real"), os.path.join(t, "com/mojang/authlib/GameProfile.java"),
         os.path.join(t, "it/unimi/dsi/fastutil/ints/Int2ObjectLinkedOpenHashMap.java"),
         os.path.join(t, "it/unimi/dsi/fastutil/objects/Object2IntOpenHashMap.java"), os.path.join(t, "RealisticGame.java")])
    return jar_path


def lines_of(dg):
    return dg.decode("utf-8").split("\n")


def f(x):
    return float(x)


# ---- per-case expectations: each returns a list of problems found in the collected datagrams ----------------------------------
def common(dgs, problems):
    if not dgs:
        problems.append("no datagram received")
        return False
    if any(not d.startswith(b"MCRADAR 4\n") for d in dgs):
        problems.append("a datagram does not start with 'MCRADAR 4'")
    stamps = []
    for d in dgs:
        for l in lines_of(d):
            if l.startswith("T "):
                stamps.append(int(l[2:]))
    if len(stamps) != len(dgs):
        problems.append("%d datagrams but %d T lines" % (len(dgs), len(stamps)))
    if any(b < a for a, b in zip(stamps, stamps[1:])):
        problems.append("T (nanoTime) stamps are not non-decreasing")
    if max(len(d) for d in dgs) > 65507:
        problems.append("datagram larger than one UDP packet")
    return True


def players(dgs):
    """last datagram that has a local player -> {name: columns}"""
    for d in reversed(dgs):
        ls = lines_of(d)
        if any(l.startswith("L ") for l in ls):
            return {l.split(" ")[1]: l.split(" ") for l in ls if l[:2] in ("L ", "P ")}, ls
    return {}, []


def check_fake(dgs, problems, third=False, old=False):
    if not common(dgs, problems):
        return
    pl, ls = players(dgs)
    for n in ("Alice", "Bob", "Carol"):
        if n not in pl:
            problems.append("player %s missing" % n)
    if "Carol" in pl and not old and abs(f(pl["Carol"][9]) - 1.5) > 0.01:
        problems.append("Carol (sneaking) hitbox height should be 1.50, got %s" % pl["Carol"][9])
    if "Alice" in pl and not any(l.startswith("L Alice") for l in ls):
        problems.append("Alice is not the LOCAL player")
    cams = [l for d in dgs for l in lines_of(d) if l.startswith("C ")]
    if old:
        return                                         # old-style games have no camera object and no items
    if not cams:
        problems.append("no C (camera) line - the agent never found the game camera")
    else:
        c = cams[-1].split(" ")
        if not (62.5 <= f(c[6]) <= 77.5):
            problems.append("FOV %s outside the fake game's 63-77 sweep" % c[6])
        want = "2" if third else "1"
        if c[7] != want:
            problems.append("camera mode %s, expected %s" % (c[7], want))
    items = []
    for d in dgs:
        ls = lines_of(d)
        if "F 1" in ls:
            items = [l for l in ls if l.startswith("I ")]
    keys = sorted(l.split(" ")[7] for l in items)
    if keys != ["minecraft:cobblestone", "minecraft:diamond_sword", "minecraft:enchanted_book"]:
        problems.append("items %s (expected cobblestone, diamond_sword, enchanted_book; the item frame must be filtered)" % keys)
    sword = [l for l in items if l.split(" ")[7] == "minecraft:diamond_sword"]
    if sword and "minecraft:sharpness@5" not in sword[0]:
        problems.append("sword enchantments not parsed: %s" % sword[0])


TOKEN = "0123456789abcdef0123456789abcdef"


def check_v5(dgs, problems):
    """wire v5: every datagram starts 'MCRADAR 5' + 'K <token>', the rest is the v4 content"""
    if not dgs:
        problems.append("no datagram received")
        return
    bad = [d for d in dgs if not d.startswith(("MCRADAR 5\nK %s\n" % TOKEN).encode())]
    if bad:
        problems.append("%d of %d datagrams do not start with 'MCRADAR 5' + 'K <token>' (first bad: %r)" % (len(bad), len(dgs), bad[0][:60]))
    pl, _ = players(dgs)
    for n in ("Alice", "Bob", "Carol"):
        if n not in pl:
            problems.append("player %s missing" % n)
    if not any(l.startswith("T ") for d in dgs for l in lines_of(d)):
        problems.append("no T line in the v5 datagrams")
    # numbering: 'Q <session> <seq>' right after K, one session, seq 1, 2, 3, ... (loopback may rarely lose one, never reorder)
    qs = []
    for d in dgs:
        ls = lines_of(d)
        if len(ls) > 2 and ls[2].startswith("Q "):
            _, sess, seq = ls[2].split(" ")
            qs.append((sess, int(seq)))
    if len(qs) != len(dgs):
        problems.append("%d of %d datagrams have a Q line in third position" % (len(qs), len(dgs)))
    elif qs:
        if len({q[0] for q in qs}) != 1 or len(qs[0][0]) != 8:
            problems.append("session id is not one constant 8-hex-digit value: %s" % sorted({q[0] for q in qs})[:3])
        seqs = [q[1] for q in qs]
        if any(b <= a for a, b in zip(seqs, seqs[1:])):
            problems.append("datagram numbers are not strictly increasing")
        consecutive = sum(1 for a, b in zip(seqs, seqs[1:]) if b == a + 1)
        if consecutive < 0.98 * (len(seqs) - 1):
            problems.append("only %d of %d datagram numbers are consecutive" % (consecutive, len(seqs) - 1))
        if seqs[0] > 3:
            problems.append("numbering does not start at the beginning (first seen: %d)" % seqs[0])


def check_realistic(dgs, problems):
    if not common(dgs, problems):
        return
    pl, _ = players(dgs)
    for n in ("Alice",):
        if n not in pl:
            problems.append("player %s missing" % n)
    best = 0
    for d in dgs:
        ls = lines_of(d)
        if "F 1" in ls:
            best = max(best, len([l for l in ls if l.startswith("I ")]))
    if best != 5:
        problems.append("expected 5 items (the real-structure game), saw at most %d" % best)


CASES = {
    "fake": ("fake", "FakeGame", ["--username", "Alice", "wiggle"], lambda d, p: check_fake(d, p)),
    "fake-old": ("fake", "FakeGame", ["--username", "Alice", "old"], lambda d, p: check_fake(d, p, old=True)),
    "fake-third": ("fake", "FakeGame", ["--username", "Alice", "wiggle", "third"], lambda d, p: check_fake(d, p, third=True)),
    "realistic": ("real", "RealisticGame", [], lambda d, p: check_realistic(d, p)),
    "fake-v5": ("fake", "FakeGame", ["--username", "Alice", "wiggle"], lambda d, p: check_v5(d, p)),
}

# extra agent options and the negotiation line expected in esp_agent.log, per case (default: wire=4, no token)
EXTRA_OPTS = {"fake-v5": "wire=5;token=" + TOKEN}
NEGOTIATION = {"fake-v5": "overlay reads up to v5, agent writes up to v5 -> sending v5, session token set"}


def done(case, dgs):
    """stop early when the interesting state has been reached (the camera needs a few seconds of head movement)"""
    probs = []
    CASES[case][3](dgs, probs)
    return not probs


def run_case(case, java_home, jar, timeout):
    cp, main, gargs, check = CASES[case]
    java = tool("java", java_home)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))
    sock.settimeout(0.5)
    port = sock.getsockname()[1]
    export = tempfile.mkdtemp(prefix="esp-smoke-")
    cmd = [java]
    if java_major(java) >= 23:
        cmd.append("--sun-misc-unsafe-memory-access=allow")        # JDK 23+ warns about Unsafe memory access otherwise (README, Known limits)
    cmd += ["-javaagent:%s=port=%d;hz=60;%s;export=%s;user=Alice" % (jar, port, EXTRA_OPTS.get(case, "wire=4"), export.replace("\\", "/")),
            "-cp", os.path.join(OUT, cp), main] + gargs
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    dgs, t0, last_check = [], time.time(), 0
    try:
        while time.time() - t0 < timeout:
            try:
                dgs.append(sock.recvfrom(65536)[0])
            except socket.timeout:
                if proc.poll() is not None:
                    break
                continue
            if len(dgs) % 120 == 0 and len(dgs) != last_check:
                last_check = len(dgs)
                if done(case, dgs):
                    break
    finally:
        proc.kill()
        proc.wait()
        sock.close()
        try:
            with open(os.path.join(export, "esp_agent.log"), "r", encoding="utf-8", errors="replace") as lf:
                agent_log = lf.read()
        except OSError:
            agent_log = ""
        shutil.rmtree(export, ignore_errors=True)
    problems = []
    check(dgs, problems)
    want = NEGOTIATION.get(case, "overlay reads up to v4, agent writes up to v5 -> sending v4")
    if want not in agent_log:
        problems.append("agent log has no wire-version negotiation line %r (esp_agent.log: %r)" % (want, agent_log[:300]))
    return dgs, problems, time.time() - t0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--java-home")
    ap.add_argument("--timeout", type=float, default=45.0)
    ap.add_argument("cases", nargs="*")
    a = ap.parse_args()
    java_home = a.java_home or os.environ.get("JAVA_HOME")
    cases = a.cases or list(CASES)
    for c in cases:
        if c not in CASES:
            sys.exit("unknown case %s (have: %s)" % (c, ", ".join(CASES)))
    jar = build(java_home)
    print("java: %s (major %d)" % (tool("java", java_home), java_major(tool("java", java_home))))
    bad = 0
    for c in cases:
        dgs, problems, secs = run_case(c, java_home, jar, a.timeout)
        print("%-11s %s  (%d datagrams, %.1fs)" % (c, "PASS" if not problems else "FAIL", len(dgs), secs))
        for p in problems:
            print("    - " + p)
        bad += bool(problems)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
