// Adapted from JDK test gc/shenandoah/TestArrayCopyStress.java
// Changes: replaced jdk.test.lib.Utils.getRandomInstance() with new Random()
import java.util.Random;

public class JdkTestArrayCopyStress {

    private static final int ARRAY_SIZE = 1000;
    private static final int ITERATIONS = 10000;

    static class Foo {
        int num;
        Foo(int num) { this.num = num; }
    }

    public static void main(String[] args) throws Exception {
        for (int i = 0; i < ITERATIONS; i++) {
            testConjoint();
        }
        System.out.println("PASS: JdkTestArrayCopyStress OK (" + ITERATIONS + " iterations)");
    }

    private static final Random rng = new Random(0x5678);

    private static void testConjoint() {
        Foo[] array = new Foo[ARRAY_SIZE];
        for (int i = 0; i < ARRAY_SIZE; i++) {
            array[i] = new Foo(i);
        }
        int src_idx = rng.nextInt(ARRAY_SIZE);
        int dst_idx = rng.nextInt(ARRAY_SIZE);
        int len = rng.nextInt(Math.min(ARRAY_SIZE - src_idx, ARRAY_SIZE - dst_idx));
        System.arraycopy(array, src_idx, array, dst_idx, len);

        for (int i = 0; i < ARRAY_SIZE; i++) {
            if (i >= dst_idx && i < dst_idx + len) {
                assertEquals(array[i].num, i - (dst_idx - src_idx));
            } else {
                assertEquals(array[i].num, i);
            }
        }
    }

    private static void assertEquals(int a, int b) {
        if (a != b) throw new RuntimeException("assertEquals failed: " + a + " != " + b);
    }
}
