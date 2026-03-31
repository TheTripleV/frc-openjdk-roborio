public class AllocTest3 {
    // Same as JdkTestAllocObjects but with debug prints
    static final long TARGET_MB = Long.getLong("target", 500);
    static volatile Object sink;

    public static void main(String[] args) throws Exception {
        System.err.println("TARGET_MB=" + TARGET_MB + " System.out=" + System.out);
        long count = TARGET_MB * 1024 * 1024 / 16;
        for (long c = 0; c < count; c++) {
            sink = new Object();
        }
        System.err.println("After loop System.out=" + System.out);
        if (System.out == null) {
            System.err.println("FAIL: System.out is null!");
        } else {
            System.out.println("PASS: AllocTest3 OK (" + TARGET_MB + " MB)");
        }
    }
}
