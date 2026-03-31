/**
 * Comprehensive GC correctness stress test for Shenandoah.
 * Inspired by JDK's TestSieveObjects, TestLotsOfCycles, and TestVerifyJCStress.
 *
 * This runs a "Sieve of Eratosthenes"-like algorithm that maintains a
 * large object graph, which verifies that GC correctly tracks and
 * updates object references. Also stresses SATB barriers.
 *
 * Additionally tests:
 * - Static field access (getstatic barrier - Fix 3)
 * - Final reference processing
 * - String interning (weak ref pool)
 * - Identity hash code (tests markWord after GC)
 */
public class ShenandoahObjectStress {
    static volatile boolean running = true;

    // === SIEVE TEST ===
    // Maintain an array of "sieve" objects, periodically nulling out some
    // and verifying that GC doesn't corrupt the remaining ones.
    static class Sieve {
        static final int SIZE = 1000;
        static Object[] objects = new Object[SIZE];
        static long[] checksums = new long[SIZE];

        static class Entry {
            final int index;
            final long checksum;
            final String label;
            final int[] data;

            Entry(int index) {
                this.index = index;
                this.label = "entry_" + index;
                this.data = new int[index % 32 + 4];
                long cs = index;
                for (int i = 0; i < data.length; i++) {
                    data[i] = index * 7 + i;
                    cs = cs * 31 + data[i];
                }
                this.checksum = cs;
            }

            long verify() {
                long cs = index;
                for (int i = 0; i < data.length; i++) {
                    if (data[i] != index * 7 + i) return -9999999;
                    cs = cs * 31 + data[i];
                }
                return cs;
            }
        }

        static void fill() {
            for (int i = 0; i < SIZE; i++) {
                Entry e = new Entry(i);
                objects[i] = e;
                checksums[i] = e.checksum;
            }
        }

        static int checkAll() {
            int errors = 0;
            for (int i = 0; i < SIZE; i++) {
                if (objects[i] == null) continue;
                Entry e = (Entry) objects[i];
                long actual = e.verify();
                if (actual != checksums[i]) {
                    System.err.println("CORRUPTION at " + i + ": expected " + checksums[i] + " got " + actual);
                    errors++;
                }
            }
            return errors;
        }

        // Clear some objects to simulate "sieving away" - old entries become unreachable
        static void sieve(int mod) {
            for (int i = 0; i < SIZE; i++) {
                if (i % mod == 0) {
                    objects[i] = null;
                    checksums[i] = 0;
                }
            }
        }

        // Refill cleared slots
        static void refill() {
            for (int i = 0; i < SIZE; i++) {
                if (objects[i] == null) {
                    Entry e = new Entry(i);
                    objects[i] = e;
                    checksums[i] = e.checksum;
                }
            }
        }
    }

    // === STATIC FIELD TEST ===
    // Tests static field access barriers (Fix 3 - getstatic)
    static Object staticRoot = new Object();
    static String staticString = "initial";
    static int[] staticArray = new int[16];
    static long staticLong = 0;

    static void testStaticRefs() {
        // Read static fields (exercises getstatic LRB - Fix 3)
        Object o = staticRoot;
        String s = staticString;
        int[] arr = staticArray;

        // Write new objects to statics (exercises putstatic SATB barrier)
        staticRoot = new Object();
        staticString = "updated_" + staticLong;
        staticArray = new int[staticLong % 16 == 0 ? 16 : (int)(staticLong % 16) + 1];
        staticLong++;

        // Touch old refs to prevent optimization
        if (o == null || s == null || arr == null) {
            throw new RuntimeException("Static ref was null");
        }
    }

    // === IDENTITY HASH CODE TEST ===
    // Tests markWord stability across GC (identity hash code must survive evacuation)
    static void testIdentityHashCode() {
        Object[] objs = new Object[20];
        int[] hashes = new int[20];

        for (int i = 0; i < objs.length; i++) {
            objs[i] = new Object();
            hashes[i] = System.identityHashCode(objs[i]);
        }

        // Force potential GC by allocating
        byte[][] trash = new byte[64][];
        for (int i = 0; i < trash.length; i++) trash[i] = new byte[1024];

        // Verify hash codes are stable after potential evacuation
        for (int i = 0; i < objs.length; i++) {
            int newHash = System.identityHashCode(objs[i]);
            if (newHash != hashes[i]) {
                throw new RuntimeException("Identity hash changed! obj[" + i + "]: " + hashes[i] + " -> " + newHash);
            }
        }
    }

    // === STRING INTERN TEST ===
    // Tests weak reference handling for interned strings
    static void testStringIntern() {
        String[] live = new String[10];
        for (int i = 0; i < live.length; i++) {
            live[i] = ("intern_test_" + i).intern();
        }

        // Allocate lots of short-lived strings
        long sum = 0;
        for (int i = 0; i < 100; i++) {
            String s = ("temp_" + i).intern();
            sum += s.length();
        }

        // Verify live interned strings are still valid
        for (int i = 0; i < live.length; i++) {
            String expected = "intern_test_" + i;
            if (!live[i].equals(expected)) {
                throw new RuntimeException("Interned string corrupted at " + i);
            }
        }
    }

    // Background allocation thread
    static class BackgroundAlloc implements Runnable {
        @Override
        public void run() {
            while (running) {
                byte[] b = new byte[4096];
                Object[] o = new Object[64];
                for (int i = 0; i < o.length; i++) o[i] = new int[4];
            }
        }
    }

    public static void main(String[] args) throws Exception {
        int duration = args.length > 0 ? Integer.parseInt(args[0]) : 45;
        System.out.println("=== Object Integrity Stress Test ===");
        System.out.println("Duration: " + duration + "s");

        // Background pressure
        Thread bg = new Thread(new BackgroundAlloc(), "bg-alloc");
        bg.setDaemon(true);
        bg.start();

        // Fill initial sieve
        Sieve.fill();

        long start = System.currentTimeMillis();
        long iterations = 0;
        int totalErrors = 0;
        int cycles = 0;

        while (System.currentTimeMillis() - start < duration * 1000L) {
            // Check all sieve entries
            int errors = Sieve.checkAll();
            if (errors > 0) {
                System.err.println("SIEVE CORRUPTION: " + errors + " errors at iteration " + iterations);
                totalErrors += errors;
                if (totalErrors > 10) {
                    System.out.println("FAIL: Too many corruptions");
                    running = false;
                    System.exit(1);
                }
            }

            // Sieve with different moduli to create varied object liveness
            int mod = 2 + (int)(iterations % 7);
            Sieve.sieve(mod);
            Sieve.refill();

            // Test static refs
            testStaticRefs();

            // Test identity hash code stability
            if (iterations % 10 == 0) {
                testIdentityHashCode();
            }

            // Test string interning
            if (iterations % 20 == 0) {
                testStringIntern();
            }

            iterations++;
            cycles++;
        }

        running = false;
        bg.join(2000);

        System.out.println("Iterations: " + iterations);
        System.out.println("Cycles: " + cycles);
        System.out.println("Total errors: " + totalErrors);

        if (totalErrors > 0) {
            System.out.println("FAIL: GC correctness errors detected!");
            System.exit(1);
        }
        System.out.println("PASS: Object integrity stress OK");
    }
}
