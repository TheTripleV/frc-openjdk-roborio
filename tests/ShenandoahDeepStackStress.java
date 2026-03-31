/**
 * Deep recursive call stack stress test for Shenandoah GC.
 * Specifically designed to trigger concurrent stack scanning of deeply nested
 * compiled frames - the scenario that caused sender_for_compiled_frame crashes.
 * 
 * Analog of JDK's TestLotsOfCycles + deep stack patterns.
 */
public class ShenandoahDeepStackStress {
    static final int DEPTH = 200;
    static volatile boolean running = true;
    static volatile long result = 0;
    static volatile int gcCycles = 0;

    // Object that will be live across deep recursive calls
    static class TreeNode {
        TreeNode left, right;
        long value;
        String tag;
        byte[] payload;

        TreeNode(int depth) {
            this.value = depth;
            this.tag = "node_" + depth;
            this.payload = new byte[16]; // small to avoid humongous
            if (depth > 0) {
                this.left = new TreeNode(depth - 1);
                if (depth > 1) this.right = new TreeNode(depth - 2);
            }
        }

        long sum() {
            return value + (left != null ? left.sum() : 0) + (right != null ? right.sum() : 0);
        }
    }

    // Deep recursive method that keeps compiled frames live during GC
    static long deepRecurse(int depth, Object[] live) {
        if (depth <= 0) {
            // At the bottom: do some allocation to trigger GC
            Object[] arr = new Object[32];
            for (int i = 0; i < arr.length; i++) {
                arr[i] = new long[4];
            }
            return arr.length;
        }
        // Keep a local object alive across the recursive call
        String marker = "depth_" + depth;
        long[] data = new long[depth % 8 + 1];
        data[0] = depth;
        long sub = deepRecurse(depth - 1, live);
        // Use the local refs to keep them live in the frame
        return sub + marker.length() + data[0];
    }

    // Moderately deep compiled call chain with method calls back and forth
    static int chainA(int n) {
        if (n <= 0) return 0;
        Object o = new Object();
        String s = o.toString();
        return chainB(n - 1) + s.length();
    }

    static int chainB(int n) {
        if (n <= 0) return 0;
        int[] arr = new int[n % 16 + 1];
        arr[0] = n;
        return chainA(n - 1) + arr[0];
    }

    static void allocThread() {
        // Background thread that keeps GC busy
        long sum = 0;
        while (running) {
            Object[] objs = new Object[64];
            for (int i = 0; i < objs.length; i++) {
                objs[i] = new int[i + 1];
            }
            sum += objs.length;
            // Occasionally trigger GC countin
            for (Object o : objs) {
                if (o == null) sum++;
            }
        }
        result = sum;
    }

    public static void main(String[] args) throws Exception {
        int duration = args.length > 0 ? Integer.parseInt(args[0]) : 30;
        System.out.println("=== Deep Stack Stress Test ===");
        System.out.println("Duration: " + duration + "s, Stack depth: " + DEPTH);

        // Launch background allocator thread to keep GC active
        Thread allocThread = new Thread(ShenandoahDeepStackStress::allocThread, "alloc");
        allocThread.setDaemon(true);
        allocThread.start();

        // Build initial tree
        TreeNode root = new TreeNode(12);
        long treeSum = root.sum();
        System.out.println("Initial tree sum: " + treeSum);

        long start = System.currentTimeMillis();
        long iterations = 0;
        long errors = 0;

        try {
            while (System.currentTimeMillis() - start < duration * 1000L) {
                // Test 1: Deep recursive calls while GC runs concurrently
                Object[] live = new Object[4];
                live[0] = new int[100];
                live[1] = new String("keepalive_" + iterations);
                long res = deepRecurse(DEPTH, live);
                if (res <= 0) errors++;

                // Test 2: Chain of compiled calls
                int chainRes = chainA(50);
                if (chainRes < 0) errors++;

                // Test 3: Rebuild tree periodically (allocates new objects)
                if (iterations % 100 == 0) {
                    root = new TreeNode(8);
                    treeSum = root.sum();
                }

                iterations++;
            }
        } finally {
            running = false;
        }

        allocThread.join(2000);
        System.out.println("Iterations: " + iterations);
        System.out.println("Errors: " + errors);
        System.out.println("Final tree sum: " + treeSum);
        
        if (errors > 0) {
            System.out.println("FAIL: Got " + errors + " errors");
            System.exit(1);
        }
        System.out.println("PASS: Deep stack stress OK");
    }
}
