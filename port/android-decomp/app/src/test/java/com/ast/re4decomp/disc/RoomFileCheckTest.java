package com.ast.re4decomp.disc;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import java.io.StringReader;
import org.junit.Test;

public class RoomFileCheckTest {
  @Test
  public void differingFilesByFolder() throws Exception {
    String manifest = "aa\t1\tfiles/Evd/r100s00.evd\n"
        + "bb\t1\tfiles/Evd/r100s03.evd\n"
        + "cc\t1\tfiles/snd/r100.snd\n"
        + "dd\t1\tfiles/St1/r100.das\n"
        + "ee\t1\tfiles/dummy.dat\n";
    String catalog = "# disc\tpath\tbytes\tsha256\n"
        + "1\tfiles/Evd/r100s00.evd\t1\t11\n"
        + "1\tfiles/Evd/r100s03.evd\t1\t22\n"
        + "1\tfiles/snd/r100.snd\t1\t33\n"
        + "1\tfiles/St1/r100.das\t1\tdd\n"
        + "1\tfiles/dummy.dat\t1\t44\n"
        + "2\tfiles/Evd/r300s00.evd\t1\t55\n";
    RoomFileCheck c = RoomFileCheck.verify(new StringReader(manifest), new StringReader(catalog), 1);
    assertEquals(5, c.checked);
    assertEquals(4, c.bad.size());
    assertEquals("(root) 1, Evd 2, snd 1", c.byFolder());
    // the start screen's line: damaged or modified on purpose (a fan translation, report of 07.11.2026)
    String w = c.warning(1);
    assertTrue(w, w.startsWith("Disc 1 imported, but 4 of 5 files differ from the original disc (first: "));
    assertTrue(w, w.contains("damaged or modified (a fan translation or a mod, for example)"));
    assertTrue(w, w.contains("made for the original discs"));
  }

  @Test
  public void noWarningWhenEveryFileMatches() throws Exception {
    String manifest = "11\t1\tfiles/Evd/r100s00.evd\n";
    String catalog = "# disc\tpath\tbytes\tsha256\n1\tfiles/Evd/r100s00.evd\t1\t11\n";
    RoomFileCheck c = RoomFileCheck.verify(new StringReader(manifest), new StringReader(catalog), 1);
    assertEquals(0, c.bad.size());
    assertNull(c.warning(1));
  }
}
