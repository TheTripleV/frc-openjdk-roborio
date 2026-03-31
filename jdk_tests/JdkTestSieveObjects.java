// Adapted from JDK test gc/shenandoah/TestSieveObjects.java
// Changes: replaced jdk.test.lib.Utils.getRandomInstance() with new Random(),
//          reduced WINDOW and PAYLOAD to fit in 64MB heap (original used 1G)
import java.util.Random;

public class JdkTestSieveObjects {

    // Original: COUNT=100M, WINDOW=1M, PAYLOAD=100 (too large for 64MB heap)
    // Scaled for 64MB: WINDOW=200K * ~86 bytes = ~17MB live data
    static final int COUNT   = 5_000_000;
    static final int WINDOW  =   200_000;
    static final int PAYLOAD =        50;

    static final MyObject[] arr = new MyObject[WINDOW];

    public static void main(String[] args) throws Exception {
        int rIdx = 0;
        Random rng = new Random(0xABCD);
        for (int c = 0; c < COUNT; c++) {
            MyObject v = arr[rIdx];
            if (v != null) {
                if (v.x != rIdx) {
                    throw new IllegalStateException("Illegal value at index " + rIdx + ": " + v.x);
                }
                if (rng.nextInt(1000) > 100) {
                    arr[rIdx] = null;
                }
            } else {
                if (rng.nextInt(1000) > 500) {
                    arr[rIdx] = new MyObject(rIdx);
                }
            }
            rIdx++;
            if (rIdx >= WINDOW) {
                rIdx = 0;
            }
        }
        System.out.println("PASS: JdkTestSieveObjects OK (" + COUNT + " iterations)");
    }

    public static class MyObject {
        public int x;
        public byte[] payload;

        public MyObject(int x) {
            this.x = x;
            this.payload = new byte[PAYLOAD];
        }
    }
}
