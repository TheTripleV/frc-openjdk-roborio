/**
 * Synchronized/monitor stress test for Shenandoah GC.
 * Tests ObjectMonitor inflation, lock contention, and wait/notify
 * while Shenandoah runs concurrently. This is the "EvilSyncBug" analog
 * from the JDK test suite.
 *
 * The markWord::has_monitor() fix (Fix for SATB+monitor interference)
 * should be exercised heavily here.
 */
public class ShenandoahSyncStress {
    static volatile boolean running = true;
    static volatile long totalOps = 0;

    static class Counter {
        long value = 0;
        byte[] pad = new byte[64]; // padding to make objects non-trivial size
        synchronized void increment() { value++; }
        synchronized long get() { return value; }
    }

    static class SharedQueue {
        Object[] items = new Object[32];
        int head = 0, tail = 0, count = 0;

        synchronized void put(Object o) {
            while (count == items.length) {
                try { wait(1); } catch (InterruptedException e) { Thread.currentThread().interrupt(); return; }
            }
            items[tail] = o;
            tail = (tail + 1) % items.length;
            count++;
            notifyAll();
        }

        synchronized Object take() {
            while (count == 0) {
                try { wait(1); } catch (InterruptedException e) { Thread.currentThread().interrupt(); return null; }
            }
            Object o = items[head];
            items[head] = null;
            head = (head + 1) % items.length;
            count--;
            notifyAll();
            return o;
        }
    }

    // Stress lock inflation: alternate between locked and unlocked states
    static void lockInflatStress(int iterations) {
        Object[] objs = new Object[8];
        for (int i = 0; i < objs.length; i++) {
            objs[i] = new Object();
        }
        for (int iter = 0; iter < iterations; iter++) {
            // Lock in one order
            for (int i = 0; i < objs.length; i++) {
                synchronized (objs[i]) {
                    // Do something inside the lock to prevent optimization
                    objs[i].hashCode();
                }
            }
            // Allocate new objects to trigger GC barriers
            if (iter % 100 == 0) {
                objs = new Object[8];
                for (int i = 0; i < objs.length; i++) {
                    objs[i] = new Object();
                }
            }
        }
    }

    // Producer thread
    static class Producer implements Runnable {
        final SharedQueue queue;
        long produced = 0;

        Producer(SharedQueue q) { this.queue = q; }

        @Override
        public void run() {
            while (running) {
                // Allocate diverse objects to keep GC busy
                String[] strings = new String[16];
                for (int i = 0; i < strings.length; i++) {
                    strings[i] = "item_" + produced + "_" + i;
                }
                queue.put(strings);
                produced++;
                // Inflate lock on itself to stress monitor handling
                lockInflatStress(10);
            }
        }
    }

    // Consumer thread
    static class Consumer implements Runnable {
        final SharedQueue queue;
        long consumed = 0;

        Consumer(SharedQueue q) { this.queue = q; }

        @Override
        public void run() {
            while (running) {
                Object item = queue.take();
                if (item != null) {
                    // Access the object to trigger LRB
                    if (item instanceof String[]) {
                        String[] arr = (String[]) item;
                        consumed += arr.length;
                    }
                }
                lockInflatStress(5);
            }
        }
    }

    // Background allocator to keep GC busy
    static class Allocator implements Runnable {
        @Override
        public void run() {
            long sum = 0;
            while (running) {
                // Mixed allocations
                int[] ints = new int[64];
                Object[] objs = new Object[32];
                for (int i = 0; i < 32; i++) {
                    objs[i] = new long[i + 1];
                }
                sum += ints.length + objs.length;

                // Trigger hash computation on objects (tests markWord)
                for (Object o : objs) {
                    sum += System.identityHashCode(o);
                }
            }
            totalOps = sum;
        }
    }

    public static void main(String[] args) throws Exception {
        int duration = args.length > 0 ? Integer.parseInt(args[0]) : 30;
        System.out.println("=== Synchronized Stress Test ===");
        System.out.println("Duration: " + duration + "s");

        // Shared state
        Counter counter = new Counter();
        SharedQueue queue = new SharedQueue();

        // Start threads
        Thread[] threads = new Thread[6];
        threads[0] = new Thread(new Producer(queue), "producer1");
        threads[1] = new Thread(new Producer(queue), "producer2");
        threads[2] = new Thread(new Consumer(queue), "consumer1");
        threads[3] = new Thread(new Consumer(queue), "consumer2");
        threads[4] = new Thread(new Allocator(), "allocator1");
        threads[5] = new Thread(new Allocator(), "allocator2");

        for (Thread t : threads) {
            t.setDaemon(true);
            t.start();
        }

        long start = System.currentTimeMillis();
        long counterVal = 0;

        // Main thread also does synchronized work
        while (System.currentTimeMillis() - start < duration * 1000L) {
            synchronized (counter) {
                counter.increment();
            }
            counter.increment();
            counter.increment();
            counterVal = counter.get();

            // Periodically do lock inflation stress
            lockInflatStress(20);
        }

        running = false;
        for (Thread t : threads) {
            t.join(3000);
        }

        System.out.println("Counter value: " + counterVal);
        System.out.println("Total ops: " + totalOps);
        
        if (counterVal <= 0) {
            System.out.println("FAIL: Counter should be positive");
            System.exit(1);
        }
        System.out.println("PASS: Synchronized stress OK");
    }
}
