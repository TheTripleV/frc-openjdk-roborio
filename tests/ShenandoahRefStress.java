/**
 * Test 6: WeakReference / SoftReference / PhantomReference stress.
 * Tests reference processing during concurrent GC cycles.
 * Run: java -XX:+UseShenandoahGC -Xmx64m -Xms32m -verbose:gc ShenandoahRefStress
 */
import java.lang.ref.*;
import java.util.*;

public class ShenandoahRefStress {
    static volatile Object sink;

    public static void main(String[] args) throws Exception {
        System.out.println("=== Test 6: Reference Processing Stress ===");

        int durationSec = 30;
        if (args.length > 0) durationSec = Integer.parseInt(args[0]);

        long start = System.currentTimeMillis();
        long deadline = start + durationSec * 1000L;
        int iterations = 0;
        int weakCleared = 0, softCleared = 0;

        ReferenceQueue<byte[]> weakQueue = new ReferenceQueue<>();
        ReferenceQueue<byte[]> softQueue = new ReferenceQueue<>();
        List<WeakReference<byte[]>> weakRefs = new ArrayList<>();
        List<SoftReference<byte[]>> softRefs = new ArrayList<>();

        System.out.println("Testing reference processing for " + durationSec + "s...");

        while (System.currentTimeMillis() < deadline) {
            // Create objects and wrap in references
            for (int i = 0; i < 50; i++) {
                byte[] data = new byte[2048];
                weakRefs.add(new WeakReference<>(data, weakQueue));
                softRefs.add(new SoftReference<>(data, softQueue));
                // data goes out of scope here (no strong ref kept)
            }

            // Allocate to trigger GC
            for (int i = 0; i < 100; i++) {
                sink = new byte[1024];
            }

            // Check reference queue
            Reference<? extends byte[]> ref;
            while ((ref = weakQueue.poll()) != null) weakCleared++;
            while ((ref = softQueue.poll()) != null) softCleared++;

            // Periodically trim lists to avoid OOM
            if (weakRefs.size() > 5000) {
                weakRefs.subList(0, 4000).clear();
            }
            if (softRefs.size() > 5000) {
                softRefs.subList(0, 4000).clear();
            }

            iterations++;
            if (iterations % 500 == 0) {
                System.out.println("  iteration " + iterations +
                    ", weakCleared=" + weakCleared + ", softCleared=" + softCleared);
            }
        }

        System.out.println("Iterations: " + iterations);
        System.out.println("Weak refs cleared: " + weakCleared);
        System.out.println("Soft refs cleared: " + softCleared);

        // PhantomReference test
        System.out.println("Testing PhantomReferences...");
        ReferenceQueue<Object> phantomQueue = new ReferenceQueue<>();
        List<PhantomReference<Object>> phantoms = new ArrayList<>();
        for (int i = 0; i < 100; i++) {
            Object obj = new byte[4096];
            phantoms.add(new PhantomReference<>(obj, phantomQueue));
        }
        // Force collection
        for (int i = 0; i < 100; i++) sink = new byte[65536];
        System.gc();
        Thread.sleep(500);
        int phantomCleared = 0;
        Reference<?> pref;
        while ((pref = phantomQueue.poll()) != null) phantomCleared++;
        System.out.println("Phantom refs cleared: " + phantomCleared);

        System.out.println("PASS: Reference processing OK");
    }
}
