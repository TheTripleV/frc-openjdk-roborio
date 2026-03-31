/**
 * Test 3: Multi-threaded allocation and synchronization stress.
 * Tests concurrent GC with thread contention - exercises monitors, forwarding pointers.
 * Run: java -XX:+UseShenandoahGC -Xmx64m -Xms32m -verbose:gc ShenandoahThreadStress
 */
public class ShenandoahThreadStress {
    static volatile boolean running = true;
    static final Object lock = new Object();
    static volatile Object sharedRef;
    static long totalAllocs = 0;

    public static void main(String[] args) throws Exception {
        System.out.println("=== Test 3: Thread Stress ===");

        int durationSec = 30;
        int threadCount = 4;
        if (args.length > 0) durationSec = Integer.parseInt(args[0]);
        if (args.length > 1) threadCount = Integer.parseInt(args[1]);

        Thread[] threads = new Thread[threadCount];

        // Allocator threads
        for (int t = 0; t < threadCount; t++) {
            final int tid = t;
            threads[t] = new Thread(() -> {
                long count = 0;
                while (running) {
                    // Allocate and share objects between threads
                    Object obj = new byte[512 + (tid * 128)];
                    sharedRef = obj; // concurrent write, may race

                    // Synchronized block - exercises monitor inflation/deflation
                    synchronized (lock) {
                        count++;
                        if (count % 10000 == 0) {
                            // Read shared reference under lock
                            Object ref = sharedRef;
                            if (ref != null) {
                                ref.hashCode(); // force access
                            }
                        }
                    }

                    // HashMap operations (common real-world pattern)
                    if (count % 100 == 0) {
                        java.util.HashMap<String, Object> map = new java.util.HashMap<>();
                        for (int i = 0; i < 20; i++) {
                            map.put("key" + i, new byte[64]);
                        }
                        // Read back
                        for (String key : map.keySet()) {
                            Object v = map.get(key);
                            if (v == null) {
                                System.out.println("FAIL: null value for " + key);
                                System.exit(1);
                            }
                        }
                    }
                }
                synchronized (ShenandoahThreadStress.class) {
                    totalAllocs += count;
                }
            }, "alloc-" + tid);
            threads[t].setDaemon(true);
            threads[t].start();
        }

        Thread.sleep(durationSec * 1000L);
        running = false;

        for (Thread t : threads) {
            t.join(5000);
        }

        System.out.println("Total allocations across threads: " + totalAllocs);
        System.out.println("PASS: Thread stress OK");
    }
}
