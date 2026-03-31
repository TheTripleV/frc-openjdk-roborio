// Adapted from JDK test gc/shenandoah/TestAllocIntArrays.java
// Changes: replaced jdk.test.lib.Utils.getRandomInstance() with new Random(),
//          reduced TARGET_MB for 64MB heap
import java.util.Random;

public class JdkTestAllocIntArrays {

    static final long TARGET_MB = Long.getLong("target", 500); // 500 MB allocation

    static volatile Object sink;

    public static void main(String[] args) throws Exception {
        final int min = 0;
        final int max = 384 * 1024;
        long count = TARGET_MB * 1024 * 1024 / (16 + 4 * (min + (max - min) / 2));
        Random r = new Random(0x1234);
        for (long c = 0; c < count; c++) {
            sink = new int[min + r.nextInt(max - min)];
        }
        System.out.println("PASS: JdkTestAllocIntArrays OK (" + count + " arrays, " + TARGET_MB + " MB target)");
    }
}
