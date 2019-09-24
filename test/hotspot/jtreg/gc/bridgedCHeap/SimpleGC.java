/*
 * @test SimpleGC
 * @summary Do some simple GCs and at least should not crash
 * @run main/othervm -Xmx128m -Xms128m -XX:+UseBridgedCHeap -Xlog:gc,bridged SimpleGC
 */
public class SimpleGC {
  private static Object f;
  public static void main(String[] args) {
    long loop = 0xFFFFF;
    while ((loop--) > 0) {
      f = new byte[128];
    }
  }
}
