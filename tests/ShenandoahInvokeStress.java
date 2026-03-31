/**
 * Test 4: Method handle / invoke dynamic stress.
 * Specifically targets the invokehandle path that was crashing.
 * Run: java -XX:+UseShenandoahGC -Xmx64m -Xms32m -verbose:gc ShenandoahInvokeStress
 */
import java.lang.invoke.*;
import java.util.*;
import java.util.function.*;
import java.util.stream.*;

public class ShenandoahInvokeStress {
    static volatile Object sink;

    public static void main(String[] args) throws Throwable {
        System.out.println("=== Test 4: InvokeHandle / Lambda Stress ===");

        int durationSec = 30;
        if (args.length > 0) durationSec = Integer.parseInt(args[0]);

        long start = System.currentTimeMillis();
        long deadline = start + durationSec * 1000L;
        int iterations = 0;

        System.out.println("Testing method handles and lambdas for " + durationSec + "s...");

        while (System.currentTimeMillis() < deadline) {
            // Lambda / invokedynamic
            testLambdas();

            // MethodHandle operations
            testMethodHandles();

            // Stream operations (heavy invokedynamic usage)
            testStreams();

            // String concatenation via invokedynamic (JDK 9+)
            testStringConcat(iterations);

            iterations++;
            if (iterations % 100 == 0) {
                System.out.println("  iteration " + iterations + ", elapsed " +
                    (System.currentTimeMillis() - start) + "ms");
            }
        }

        System.out.println("Completed " + iterations + " iterations");
        System.out.println("PASS: InvokeHandle stress OK");
    }

    static void testLambdas() {
        // Various functional interfaces
        Supplier<String> sup = () -> "hello" + System.nanoTime();
        Function<String, Integer> fn = String::length;
        Consumer<String> con = s -> sink = s;
        Predicate<String> pred = s -> s.length() > 3;

        String val = sup.get();
        int len = fn.apply(val);
        con.accept(val);
        boolean b = pred.test(val);
        sink = b ? val : "short";
    }

    static void testMethodHandles() throws Throwable {
        MethodHandles.Lookup lookup = MethodHandles.lookup();

        // Static method handle
        MethodHandle mh = lookup.findStatic(ShenandoahInvokeStress.class, "helperMethod",
            MethodType.methodType(String.class, int.class, String.class));
        String result = (String) mh.invoke(42, "test");
        sink = result;

        // Virtual method handle
        MethodHandle toString = lookup.findVirtual(Object.class, "toString",
            MethodType.methodType(String.class));
        sink = (String) toString.invoke(new Object());
        sink = (String) toString.invoke("already a string");
        sink = (String) toString.invoke(Integer.valueOf(123));

        // invokeExact
        MethodHandle exact = lookup.findStatic(String.class, "valueOf",
            MethodType.methodType(String.class, int.class));
        String s = (String) exact.invokeExact(999);
        sink = s;
    }

    static void testStreams() {
        List<String> list = new ArrayList<>();
        for (int i = 0; i < 100; i++) {
            list.add("item" + i);
        }

        // Stream operations exercise invokedynamic heavily
        long count = list.stream()
            .filter(s -> s.length() > 5)
            .map(String::toUpperCase)
            .count();
        sink = count;

        String joined = list.stream()
            .limit(10)
            .collect(Collectors.joining(", "));
        sink = joined;

        Optional<String> first = list.stream()
            .filter(s -> s.contains("50"))
            .findFirst();
        sink = first.orElse("none");

        Map<Integer, List<String>> grouped = list.stream()
            .collect(Collectors.groupingBy(String::length));
        sink = grouped;
    }

    static void testStringConcat(int iter) {
        // JDK 9+ uses invokedynamic for string concatenation
        String a = "hello";
        String b = "world";
        int n = iter;
        sink = a + " " + b + " " + n;
        sink = "Iteration: " + iter + " time: " + System.currentTimeMillis();
    }

    public static String helperMethod(int num, String text) {
        return text + "-" + num;
    }
}
