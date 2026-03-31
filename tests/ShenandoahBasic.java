/**
 * Test 1: Basic Shenandoah GC startup and simple allocation.
 * Verifies JVM starts with Shenandoah and can do basic operations.
 * Run: java -XX:+UseShenandoahGC ShenandoahBasic
 */
public class ShenandoahBasic {
    public static void main(String[] args) {
        System.out.println("=== Test 1: Shenandoah Basic Startup ===");

        // Verify Shenandoah is the active GC
        String gcName = java.lang.management.ManagementFactory.getGarbageCollectorMXBeans()
            .stream()
            .map(gc -> gc.getName())
            .filter(name -> name.contains("Shenandoah"))
            .findFirst()
            .orElse("UNKNOWN");
        System.out.println("GC Name: " + gcName);
        if (!gcName.contains("Shenandoah")) {
            System.out.println("FAIL: Shenandoah GC not active!");
            System.exit(1);
        }

        // Basic allocation
        long total = 0;
        for (int i = 0; i < 1000; i++) {
            String s = "Hello Shenandoah " + i;
            total += s.length();
        }
        System.out.println("Allocated strings, total chars: " + total);

        // Array allocation
        int[] arr = new int[10000];
        for (int i = 0; i < arr.length; i++) arr[i] = i;
        long sum = 0;
        for (int v : arr) sum += v;
        System.out.println("Array sum: " + sum);

        // Object allocation
        Object[] objects = new Object[100];
        for (int i = 0; i < objects.length; i++) {
            objects[i] = new Object();
        }
        System.out.println("Created " + objects.length + " objects");

        System.out.println("PASS: Basic startup OK");
    }
}
