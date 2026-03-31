/**
 * Array and object copy stress test for Shenandoah GC.
 * Tests arraycopy_prologue barrier (copy barriers), covariant array stores,
 * and array allocation patterns. Analog of JDK's TestArrayCopyStress and
 * TestAllocIntArrays / TestAllocObjectArrays.
 *
 * Also tests Shenandoah's clone barrier (ShenandoahCloneBarrier).
 */
public class ShenandoahArrayStress {
    static volatile boolean running = true;
    static volatile long totalCopied = 0;

    static class DataHolder {
        int id;
        String name;
        long[] data;
        DataHolder next;

        DataHolder(int id) {
            this.id = id;
            this.name = "holder_" + id;
            this.data = new long[id % 32 + 1];
            for (int i = 0; i < data.length; i++) data[i] = id + i;
        }

        long checksum() {
            long s = id;
            for (long v : data) s += v;
            return s;
        }
    }

    // Test int array allocations with various sizes
    static void testIntArrays() {
        int[][] arrays = new int[16][];
        long sum = 0;
        for (int i = 0; i < arrays.length; i++) {
            arrays[i] = new int[i * 16 + 1];
            for (int j = 0; j < arrays[i].length; j++) {
                arrays[i][j] = i + j;
                sum += arrays[i][j];
            }
        }
        // Verify (keeps arrays live)
        for (int[] arr : arrays) {
            sum -= arr[0]; // touch each
        }
        totalCopied += sum;
    }

    // Test object array copies (tests arraycopy with reference barriers)
    static void testObjectArrayCopy() {
        DataHolder[] src = new DataHolder[20];
        for (int i = 0; i < src.length; i++) {
            src[i] = new DataHolder(i);
        }

        // System.arraycopy with object arrays - triggers arraycopy_prologue
        DataHolder[] dst = new DataHolder[20];
        System.arraycopy(src, 0, dst, 0, src.length);

        // Verify copy integrity (exercises load reference barrier on dst elements)
        long sum = 0;
        for (int i = 0; i < dst.length; i++) {
            if (dst[i] == null || dst[i].id != src[i].id) {
                throw new RuntimeException("Array copy corrupted at index " + i);
            }
            sum += dst[i].checksum();
        }
        totalCopied += sum;
    }

    // Test covariant array store checks
    static void testCovariantArrays() {
        Object[] objArray = new String[10];
        for (int i = 0; i < 10; i++) {
            objArray[i] = "str_" + i;  // ArrayStoreCheck
        }

        // Partial copy - covariant store
        Object[] dst = new Object[10];
        System.arraycopy(objArray, 0, dst, 0, objArray.length);

        long sum = 0;
        for (Object o : dst) {
            if (o != null) sum += o.hashCode();
        }
        totalCopied += sum;
    }

    // Test large array allocations (humongous-threshold tests)
    static void testLargeArrays() {
        // Near humongous threshold (default is 50% of region size = 128KB)
        // Use mid-size arrays that stress GC but aren't humongous
        int[] large1 = new int[8000];   // ~32KB
        long[] large2 = new long[4000]; // ~32KB
        Object[] large3 = new Object[2000]; // ~8KB on 32-bit

        long sum = large1.length + large2.length + large3.length;
        // Touch elements to ensure they're not optimized away
        large1[0] = 42;
        large2[0] = 42L;
        large3[0] = new Object();
        sum += large1[0] + large2[0];
        totalCopied += sum;
    }

    // Test Object.clone() which exercises ShenandoahCloneBarrier
    static void testClone() throws CloneNotSupportedException {
        int[] src = new int[32];
        for (int i = 0; i < src.length; i++) src[i] = i * i;

        // Arrays are Cloneable
        int[] clone = src.clone();

        long sum = 0;
        for (int i = 0; i < clone.length; i++) {
            if (clone[i] != src[i]) {
                throw new RuntimeException("Clone mismatch at " + i + ": " + clone[i] + " != " + src[i]);
            }
            sum += clone[i];
        }
        totalCopied += sum;
    }

    // Test linked list traversal (pointer chasing under GC)
    static void testLinkedList() {
        // Build a list
        DataHolder head = null;
        for (int i = 0; i < 50; i++) {
            DataHolder node = new DataHolder(i);
            node.next = head;
            head = node;
        }

        // Traverse (exercises load reference barrier on .next)
        long sum = 0;
        DataHolder curr = head;
        while (curr != null) {
            sum += curr.checksum();
            curr = curr.next; // LRB on .next field
        }
        totalCopied += sum;
    }

    // Background allocator
    static class AllocThread implements Runnable {
        @Override
        public void run() {
            try {
                while (running) {
                    testIntArrays();
                    testObjectArrayCopy();
                    testLinkedList();
                }
            } catch (Exception e) {
                System.err.println("AllocThread error: " + e);
                running = false;
            }
        }
    }

    public static void main(String[] args) throws Exception {
        int duration = args.length > 0 ? Integer.parseInt(args[0]) : 30;
        System.out.println("=== Array and Copy Stress Test ===");
        System.out.println("Duration: " + duration + "s");

        // Launch background threads
        Thread[] threads = new Thread[3];
        for (int i = 0; i < threads.length; i++) {
            threads[i] = new Thread(new AllocThread(), "alloc-" + i);
            threads[i].setDaemon(true);
            threads[i].start();
        }

        long start = System.currentTimeMillis();
        long iterations = 0;
        long errors = 0;

        try {
            while (System.currentTimeMillis() - start < duration * 1000L) {
                try {
                    testIntArrays();
                    testObjectArrayCopy();
                    testCovariantArrays();
                    testLargeArrays();
                    testClone();
                    testLinkedList();
                } catch (Exception e) {
                    System.err.println("Error at iteration " + iterations + ": " + e.getMessage());
                    errors++;
                    if (errors > 5) {
                        System.out.println("FAIL: Too many errors");
                        System.exit(1);
                    }
                }
                iterations++;
            }
        } finally {
            running = false;
        }

        for (Thread t : threads) t.join(2000);

        System.out.println("Iterations: " + iterations);
        System.out.println("Total copied: " + totalCopied);
        System.out.println("Errors: " + errors);

        if (errors > 0) {
            System.out.println("FAIL: Got " + errors + " errors");
            System.exit(1);
        }
        System.out.println("PASS: Array stress OK");
    }
}
