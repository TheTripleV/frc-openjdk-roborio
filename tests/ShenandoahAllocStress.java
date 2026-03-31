/**
 * Test 2: Allocation pressure to trigger Shenandoah concurrent GC cycles.
 * Allocates rapidly to force GC, verifies no crashes during collection.
 * Run: java -XX:+UseShenandoahGC -Xmx64m -Xms32m -verbose:gc ShenandoahAllocStress
 */
public class ShenandoahAllocStress {
    static volatile Object sink;

    public static void main(String[] args) throws Exception {
        System.out.println("=== Test 2: Allocation Stress ===");

        int durationSec = 30;
        if (args.length > 0) durationSec = Integer.parseInt(args[0]);

        long start = System.currentTimeMillis();
        long deadline = start + durationSec * 1000L;
        long allocCount = 0;
        int gcCyclesBefore = getGcCount();

        System.out.println("Running allocation stress for " + durationSec + "s...");

        while (System.currentTimeMillis() < deadline) {
            // Short-lived allocations (should be collected)
            for (int i = 0; i < 1000; i++) {
                byte[] b = new byte[1024]; // 1KB each
                sink = b; // prevent elimination
                allocCount++;
            }

            // Some longer-lived objects
            Object[] batch = new Object[100];
            for (int i = 0; i < batch.length; i++) {
                batch[i] = new byte[256];
            }
            sink = batch;

            // String concatenation (common pattern)
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < 50; i++) {
                sb.append("item").append(i).append(",");
            }
            sink = sb.toString();
        }

        int gcCyclesAfter = getGcCount();
        long elapsed = System.currentTimeMillis() - start;

        System.out.println("Allocations: " + allocCount);
        System.out.println("GC cycles: " + (gcCyclesAfter - gcCyclesBefore));
        System.out.println("Elapsed: " + elapsed + "ms");

        if (gcCyclesAfter <= gcCyclesBefore) {
            System.out.println("WARNING: No GC cycles triggered (heap may be too large)");
        }
        System.out.println("PASS: Allocation stress OK");
    }

    static int getGcCount() {
        return java.lang.management.ManagementFactory.getGarbageCollectorMXBeans()
            .stream()
            .mapToInt(gc -> (int) gc.getCollectionCount())
            .sum();
    }
}
