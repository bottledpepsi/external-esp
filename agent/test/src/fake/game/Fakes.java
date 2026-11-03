package fake.game;

/** Stand-ins for game classes, in a package that is not the agent's own (the agent never walks into its own package). */
public final class Fakes {
    private Fakes() { }

    /** The real camera: a position, the look angles, and some constant floats. */
    public static final class Cam { public double x, y, z; public float xRot, yRot, eyeHeight = 1.62f, eyeHeightOld = 1.62f; }

    /** The look-alike: the local player's render state. Feet position; bodyRot lags the head, yRot is the head turned relative to the body. */
    public static final class State { public double x, y, z; public float bodyRot, yRot, xRot, scale = 1f; }

    /** Holds the method that builds the projection (as GameRenderer.getFov(Camera, float, boolean)); a sibling of the camera's holder, not an ancestor. */
    public static final class Renderer {
        public static volatile float fov = 77f;
        public float getFov(Cam cam, float partialTick, boolean useSetting) { return fov; }
        public int unrelated(Cam cam, float partialTick, boolean useSetting) { return 50; }   // wrong return type: never taken for it
    }

    public static final class Root {
        public static Root INSTANCE = new Root();
        public Cam camera = new Cam();
        public State state = new State();
        public Renderer gameRenderer = new Renderer();
    }
}
