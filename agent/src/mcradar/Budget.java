package mcradar;

/** A wall-clock allowance for one piece of discovery work: callers ask expired() between steps and stop (without condemning anything) when it is. */
final class Budget {
    private final long deadlineNs;

    Budget(long millis) { deadlineNs = System.nanoTime() + millis * 1000000L; }

    boolean expired() { return System.nanoTime() - deadlineNs >= 0; }
}
