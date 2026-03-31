// Tests if touching System.out first protects it from becoming null
public class AllocTest4 {
    static volatile Object sink;
    // Force early loading of System.out by a static initializer
    static final java.io.PrintStream savedOut = System.out;
    
    public static void main(String[] args) throws Exception {
        long targetMb = 500;
        long count = targetMb * 1024 * 1024 / 16;
        for (long c = 0; c < count; c++) {
            sink = new Object();
        }
        // Use savedOut to verify - if System.out is null but savedOut isn't,
        // that tells us the issue is with the static field update
        if (System.out == null) {
            if (savedOut != null) {
                savedOut.println("System.out is null but savedOut=" + savedOut);
                savedOut.println("This confirms a static field update bug!");
            }
        } else {
            System.out.println("PASS: AllocTest4 OK (" + targetMb + " MB, System.out=" + System.out + ")");
        }
    }
}
