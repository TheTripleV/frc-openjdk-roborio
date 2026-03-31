import java.io.*;
public class DiagTest {
    static volatile Object sink;
    public static void main(String[] args) throws Exception {
        System.err.println("Before: out=" + System.out);
        long count = 500L * 1024 * 1024 / 16;
        for (long c = 0; c < count; c++) {
            sink = new Object();
        }
        System.err.println("After alloc: out=" + System.out);
        if (System.out != null) {
            System.out.println("PASS: System.out is alive after 500MB alloc");
        } else {
            System.err.println("FAIL: System.out became null after " + count + " allocations");
        }
    }
}
