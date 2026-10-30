package mcradar;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;

/**
 * How the worker waits for its next tick.
 *   sleep   (default, the behaviour so far) Thread.sleep(whole ms): on HotSpot/Windows a sleep that is not a multiple of 10 ms asks for a 1 ms timer for
 *           its duration, so the wake-up is usually within 1-2 ms; see docs/pacing.md for the source references and what is NOT known.
 *   hybrid  Thread.sleep until about 2 ms before the target, then spin on System.nanoTime(): precise at the cost of up to ~2 ms of one core per tick.
 * Neither ever returns before the target except by the sub-millisecond floor of the sleep mode.
 */
final class Pacer {
    static final long SPIN_NS = 2_000_000L;
    private static final MethodHandle ON_SPIN_WAIT = lookupSpinWait();
    final boolean hybrid;

    Pacer(boolean hybrid) { this.hybrid = hybrid; }

    static Pacer of(String mode) { return new Pacer("hybrid".equalsIgnoreCase(mode)); }

    String name() { return hybrid ? "hybrid" : "sleep"; }

    private static MethodHandle lookupSpinWait() {
        try { return MethodHandles.publicLookup().findStatic(Thread.class, "onSpinWait", MethodType.methodType(void.class)); }     // Java 9+
        catch (Throwable t) { return null; }
    }

    static void spinHint() {
        if (ON_SPIN_WAIT != null) { try { ON_SPIN_WAIT.invokeExact(); return; } catch (Throwable ignored) { } }
        Thread.yield();
    }

    void waitUntil(long targetNs) throws InterruptedException {
        long rem = targetNs - System.nanoTime();
        if (!hybrid) {
            long ms = rem / 1000000L;
            if (ms >= 1) Thread.sleep(ms);
            return;
        }
        while (rem > SPIN_NS + 1_000_000L) {                               // coarse part: whole milliseconds, leaving ~2-3 ms for the spin
            Thread.sleep((rem - SPIN_NS) / 1000000L);
            rem = targetNs - System.nanoTime();
        }
        while ((rem = targetNs - System.nanoTime()) > 0) {
            if (Thread.interrupted()) throw new InterruptedException();
            spinHint();
        }
    }
}
