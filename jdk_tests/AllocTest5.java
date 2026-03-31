// Tests if System.out becomes null at field level (reflection) vs expression level
// Distinguishes: (A) GC wrote null to field vs (B) LRB returns null at read-time
import java.lang.reflect.Field;

public class AllocTest5 {
    static volatile Object sink;

    public static void main(String[] args) throws Exception {
        long targetMb = 500;
        long count = targetMb * 1024 * 1024 / 16;
        for (long c = 0; c < count; c++) {
            sink = new Object();
        }
        
        // Read System.out via normal access
        java.io.PrintStream directOut = System.out;
        
        // Read System.out via reflection (bypasses LRB, reads raw field value)
        Field f = System.class.getField("out");
        f.setAccessible(true);
        java.io.PrintStream reflOut = (java.io.PrintStream) f.get(null);
        
        java.io.PrintStream err = System.err;
        err.println("direct System.out = " + directOut);
        err.println("reflect System.out = " + reflOut);
        
        if (directOut == null && reflOut != null) {
            err.println("BUG: LRB returns null for a non-null field (C1 barrier issue)");
        } else if (directOut == null && reflOut == null) {
            err.println("BUG: Field is genuinely null (GC wrote null to System.out field)");
        } else {
            err.println("PASS: System.out = " + directOut);
        }
    }
}
