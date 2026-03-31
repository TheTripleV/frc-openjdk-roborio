// Adapted from JDK test gc/shenandoah/TestLotsOfCycles.java
// Changes: reduced TARGET_MB for 64MB heap
public class JdkTestLotsOfCycles {

    static final long TARGET_MB = Long.getLong("target", 500); // 500 MB, ~50+ cycles
    static final long STRIDE = 100_000;

    static volatile Object sink;

    public static void main(String[] args) throws Exception {
        long count = TARGET_MB * 1024 * 1024 / 16;
        long cycles = 0;
        for (long c = 0; c < count; c += STRIDE) {
            for (long s = 0; s < STRIDE; s++) {
                sink = new Object();
            }
            Thread.sleep(1);
            cycles++;
        }
        System.out.println("PASS: JdkTestLotsOfCycles OK (" + cycles + " strides, " + TARGET_MB + " MB total)");
    }
}
