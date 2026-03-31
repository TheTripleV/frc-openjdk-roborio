public class AllocTest2 {
    static volatile Object sink;
    public static void main(String[] args) throws Exception {
        long targetMb = 500;
        long count = targetMb * 1024 * 1024 / 16;
        for (long c = 0; c < count; c++) {
            sink = new Object();
        }
        System.out.println("PASS: AllocTest2 OK (" + targetMb + " MB)");
    }
}
