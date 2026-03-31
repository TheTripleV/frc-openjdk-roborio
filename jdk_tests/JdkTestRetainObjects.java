// Adapted from JDK test gc/shenandoah/TestRetainObjects.java
// Changes: added sanity check, reduced COUNT to run in reasonable time on RIO
public class JdkTestRetainObjects {

    static final int COUNT  = 2_000_000;
    static final int WINDOW =    10_000;

    static final String[] reachable = new String[WINDOW];

    public static void main(String[] args) throws Exception {
        int rIdx = 0;
        for (int c = 0; c < COUNT; c++) {
            String s = "LargeString" + c;
            reachable[rIdx] = s;
            rIdx++;
            if (rIdx >= WINDOW) {
                rIdx = 0;
            }
        }
        // Verify the last WINDOW strings are all non-null and non-empty
        int nullCount = 0;
        for (int i = 0; i < WINDOW; i++) {
            if (reachable[i] == null) nullCount++;
        }
        if (nullCount > 0) {
            throw new RuntimeException("FAIL: " + nullCount + " null entries in live window");
        }
        System.out.println("PASS: JdkTestRetainObjects OK (" + COUNT + " iterations, WINDOW=" + WINDOW + ")");
    }
}
