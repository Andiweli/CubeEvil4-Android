package com.ast.cubeevil4.texturepacker;

import static org.junit.Assert.*;

import java.util.Arrays;
import org.junit.Test;

/** The space check and the time estimate before a conversion (Estimates), and reading the pack's summary (Summary). */
public class EstimatesTest {
  private static final long MB = 1L << 20, GB = 1L << 30;

  @Test public void theFileNeedsItsSizeAndTheReserve() {
    assertTrue(Estimates.fits(800 * MB, 900 * MB));
    assertTrue(Estimates.fits(800 * MB, 800 * MB + Estimates.RESERVE));
    assertFalse(Estimates.fits(800 * MB, 800 * MB + Estimates.RESERVE - 1));
    assertFalse(Estimates.fits(800 * MB, 100 * MB));
  }

  @Test public void theLargestSizeThatFitsIsNamed() {
    long[] e = {300 * MB, 800 * MB, 2600 * MB};
    assertEquals(2, Estimates.largestFitting(e, 10 * GB));
    assertEquals(1, Estimates.largestFitting(e, GB));
    assertEquals(0, Estimates.largestFitting(e, 500 * MB));
    assertEquals(-1, Estimates.largestFitting(e, 300 * MB));
  }

  @Test public void littleCoresCountLess() {
    // Snapdragon 865: one Cortex-A77 at 2.84 GHz, three at 2.42, four Cortex-A55 at 1.8
    long[] khz = {1_804_800, 1_804_800, 1_804_800, 1_804_800, 2_419_200, 2_419_200, 2_419_200, 2_841_600};
    int[] parts = {0xd05, 0xd05, 0xd05, 0xd05, 0xd0d, 0xd0d, 0xd0d, 0xd0d};
    double sd865 = Estimates.units(khz, parts);
    assertEquals(2.8416 + 3 * 2.4192 + 0.4 * 4 * 1.8048, sd865, 0.01);
    // without "CPU part" the slow cluster counts as little the same way
    int[] unknown = new int[8];
    Arrays.fill(unknown, -1);
    assertEquals(sd865, Estimates.units(khz, unknown), 0.01);
    // Helio G85: two Cortex-A75 at 2.0 GHz, six Cortex-A55 at 1.8 (close in clock, told apart by the part)
    long[] g85 = {1_800_000, 1_800_000, 1_800_000, 1_800_000, 1_800_000, 1_800_000, 2_000_000, 2_000_000};
    int[] g85parts = {0xd05, 0xd05, 0xd05, 0xd05, 0xd05, 0xd05, 0xd0a, 0xd0a};
    assertEquals(2 * 2.0 + 0.4 * 6 * 1.8, Estimates.units(g85, g85parts), 0.01);
    // nothing readable: still a number
    assertTrue(Estimates.units(new long[4], null) > 0);
  }

  @Test public void theTimeFollowsThePcsRuns() {
    // the owner's HD Project (11.8 GB) at 512: the PC's 4:22 at PC_UNITS
    long pc = Estimates.seconds(11_800_000_000L, 716_000_000L, Estimates.PC_UNITS);
    assertTrue("PC at 512: " + pc, pc > 230 && pc < 300);
    // the Retroid (about 13 units): the plan's 20 minutes, give or take
    long retroid = Estimates.seconds(11_800_000_000L, 800_000_000L, 13.0);
    assertTrue("Retroid at 512: " + retroid, retroid > 15 * 60 && retroid < 25 * 60);
    // a bigger file takes longer
    assertTrue(Estimates.seconds(11_800_000_000L, 2_600_000_000L, 13.0) > retroid * 2);
  }

  @Test public void minutesAreRounded() {
    assertEquals(1, Estimates.roundedMinutes(10));
    assertEquals(4, Estimates.roundedMinutes(4 * 60 + 10));
    assertEquals(20, Estimates.roundedMinutes(21 * 60));
    assertEquals(75, Estimates.roundedMinutes(74 * 60));
  }

  @Test public void cpuinfoGivesEachCoresPart() {
    int[] parts = Estimates.parseParts(Arrays.asList(
        "processor\t: 0", "BogoMIPS\t: 38.40", "CPU implementer\t: 0x41", "CPU part\t: 0xd05", "",
        "processor\t: 1", "CPU part\t: 0xd0d", "", "Hardware\t: Qualcomm"), 3);
    assertArrayEquals(new int[] {0xd05, 0xd0d, -1}, parts);
    assertTrue(Estimates.isLittle(0xd05));
    assertFalse(Estimates.isLittle(0xd0d));
  }

  @Test public void theSummaryIsRead() {
    Summary s = Summary.parse("name\tResident Evil 4 HD Project\nimages\t5916\nbytes\t11800000000\nskipped\t11\n"
        + "group\tAttache case variations\t0\nvariant\tBlack\t0\nvariant\tGold\t1\n"
        + "group\tExtras\t1\nvariant\tExtras\t0\n");
    assertEquals("Resident Evil 4 HD Project", s.name);
    assertEquals(5916, s.images);
    assertEquals(11_800_000_000L, s.bytes);
    assertEquals(11, s.skipped);
    assertEquals(2, s.groups.size());
    assertEquals(Arrays.asList("Black", "Gold"), s.groups.get(0).variants);
    assertEquals(Arrays.asList(false, true), s.groups.get(0).sameAsMain);
    assertFalse(s.groups.get(0).addOn);
    assertTrue(s.groups.get(1).addOn);
    assertArrayEquals(new int[] {-1, -1}, s.defaultChoices());
    assertEquals(0, Summary.parse("").images);
  }
}
