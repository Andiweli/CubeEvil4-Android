import com.ast.re4decomp.disc.RoomFileCheck;
import java.io.*;

public final class RoomFileCheckTests {
  static int failures;
  static void check(boolean ok, String what) {
    if (!ok) { failures++; System.err.println("FAIL: " + what); }
  }
  public static void main(String[] args) throws Exception {
    String rooms = "# disc\tpath\tdecoded_size\tdecoded_sha256\tsource_sha256\n"
        + "1\tSt1/r100.das\t1\taa\tAAAA\n1\tSt2/r222.das\t1\tbb\tBBBB\n2\tSt3/r300.das\t1\tcc\tCCCC\n";
    String good = "# sha256\tbytes\tpath\naaaa\t5\tSt1/r100.das\nbbbb\t5\tSt2/r222.das\ncccc\t5\tSt3/r300.das\n";
    RoomFileCheck ok = RoomFileCheck.verify(new StringReader(good), new StringReader(rooms), 1);
    check(ok.checked == 2 && ok.bad.isEmpty() && ok.warning(1) == null, "all rooms match (case-insensitive)");
    String damaged = "aaaa\t5\tSt1/r100.das\n0000\t5\tSt2/r222.das\n";
    RoomFileCheck bad = RoomFileCheck.verify(new StringReader(damaged), new StringReader(rooms), 1);
    check(bad.bad.size() == 1 && bad.bad.get(0).equals("St2/r222.das"), "a differing hash is named");
    check(bad.warning(1).contains("1 of 2") && bad.warning(1).contains("St2/r222.das"), "warning text");
    RoomFileCheck missing = RoomFileCheck.verify(new StringReader("aaaa\t5\tSt1/r100.das\n"), new StringReader(rooms), 1);
    check(missing.bad.size() == 1, "a missing file counts");
    RoomFileCheck second = RoomFileCheck.verify(new StringReader(good), new StringReader(rooms), 2);
    check(second.checked == 1 && second.bad.isEmpty(), "disc 2 looks at its own rooms only");
    try {
      RoomFileCheck.verify(new StringReader(good), new StringReader("1\tonly\tthree\n"), 1);
      check(false, "invalid catalog throws");
    } catch (IOException expected) { }
    if (failures != 0) { System.err.println(failures + " failures"); System.exit(1); }
    System.out.println("RoomFileCheckTests: ok");
  }
}
