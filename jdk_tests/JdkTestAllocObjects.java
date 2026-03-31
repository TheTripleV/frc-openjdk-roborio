// Adapted from JDK test gc/shenandoah/TestAllocObjects.java
// Changes: reduced TARGET_MB for 64MB heap
public class JdkTestAllocObjects {

    static final long TARGET_MB = Long.getLong("target", 500); // 500 MB allocation

    static volatile Object sink;

    public static void main(String[] args) throws Exception {
        long count = TARGET_MB * 1024 * 1024 / 16;
        for (long c = 0; c < count; c++) {
            sink = new Object();
        }
        System.out.println("PASS: JdkTestAllocObjects OK (" + TARGET_MB + " MB allocated)");
    }
}
