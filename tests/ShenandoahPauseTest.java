/**
 * Test 5: Measure GC pause times.
 * Allocates continuously while measuring max pause via System.nanoTime() gaps.
 * Run: java -XX:+UseShenandoahGC -Xmx64m -Xms32m -verbose:gc ShenandoahPauseTest
 */
public class ShenandoahPauseTest {
    static volatile Object sink;

    public static void main(String[] args) throws Exception {
        System.out.println("=== Test 5: Pause Time Measurement ===");

        int durationSec = 60;
        if (args.length > 0) durationSec = Integer.parseInt(args[0]);

        long start = System.currentTimeMillis();
        long deadline = start + durationSec * 1000L;
        long maxPauseNs = 0;
        long totalPauses = 0;  // pauses > 1ms
        long longPauses = 0;   // pauses > 10ms
        int iterations = 0;

        System.out.println("Measuring pause times for " + durationSec + "s...");
        System.out.println("(Pauses are detected as gaps > 1ms between nanoTime samples)");

        while (System.currentTimeMillis() < deadline) {
            long before = System.nanoTime();

            // Do some allocation work
            for (int i = 0; i < 100; i++) {
                sink = new byte[1024];
            }

            long after = System.nanoTime();
            long pauseNs = after - before;

            // Allocation of 100KB shouldn't take more than 1ms normally
            if (pauseNs > 1_000_000) { // > 1ms
                totalPauses++;
                if (pauseNs > maxPauseNs) maxPauseNs = pauseNs;
                if (pauseNs > 10_000_000) { // > 10ms
                    longPauses++;
                    if (pauseNs > 50_000_000) { // > 50ms
                        System.out.printf("  WARNING: Long pause detected: %.1fms at iteration %d%n",
                            pauseNs / 1_000_000.0, iterations);
                    }
                }
            }
            iterations++;
        }

        long elapsed = System.currentTimeMillis() - start;
        System.out.println();
        System.out.println("=== Results ===");
        System.out.println("Duration: " + elapsed + "ms");
        System.out.println("Iterations: " + iterations);
        System.out.printf("Max pause: %.2fms%n", maxPauseNs / 1_000_000.0);
        System.out.println("Pauses > 1ms: " + totalPauses);
        System.out.println("Pauses > 10ms: " + longPauses);

        // For FRC, we want < 20ms pauses ideally
        if (maxPauseNs > 100_000_000) { // > 100ms
            System.out.println("WARNING: Max pause > 100ms - may cause robot brownout");
        } else if (maxPauseNs > 20_000_000) { // > 20ms
            System.out.println("NOTE: Max pause > 20ms - acceptable but could be improved");
        } else {
            System.out.println("EXCELLENT: All pauses < 20ms - great for FRC!");
        }

        System.out.println("PASS: Pause test completed");
    }
}
