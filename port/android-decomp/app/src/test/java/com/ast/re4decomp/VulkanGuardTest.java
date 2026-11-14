package com.ast.re4decomp;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/**
 * The crash guard of a Vulkan start (VulkanGuard, 0.99.6): a mark still there sends the game to OpenGL ES only when the
 * marked process crashed while starting; the player quitting, Android ending the app in the background or for memory
 * do not (a user's report with Android 17: the message came after quitting). Android 10 names no reason: the mark
 * alone counts.
 */
public class VulkanGuardTest {
  private static final int PID = 4711;
  private static final long AT = 1_762_700_000_000L;

  private static VulkanGuard.Verdict after(int reason) {
    return VulkanGuard.judge(true, PID, AT, 35, new VulkanGuard.Exit(PID, reason, 0, AT + 3_000, ""), 0);
  }

  @Test public void noMarkNoMessage() {
    assertFalse(VulkanGuard.judge(false, 0, 0, 35, null, 0).fallBack);
    assertFalse(VulkanGuard.judge(false, 0, 0, 29, null, 0).fallBack);
  }

  @Test public void aCrashWhileStartingGoesToOpenGlEs() {
    assertTrue(after(VulkanGuard.REASON_CRASH_NATIVE).fallBack);  // the GPU driver
    assertTrue(after(VulkanGuard.REASON_CRASH).fallBack);
    assertTrue(after(VulkanGuard.REASON_ANR).fallBack);
    assertTrue(after(VulkanGuard.REASON_INITIALIZATION_FAILURE).fallBack);
  }

  @Test public void quittingOrAndroidEndingTheAppDoesNot() {
    assertFalse(after(VulkanGuard.REASON_EXIT_SELF).fallBack);       // the menu's Quit (System.exit)
    assertFalse(after(VulkanGuard.REASON_USER_REQUESTED).fallBack);  // swiped away, force stop
    assertFalse(after(VulkanGuard.REASON_USER_STOPPED).fallBack);
    assertFalse(after(VulkanGuard.REASON_LOW_MEMORY).fallBack);      // in the background
    assertFalse(after(VulkanGuard.REASON_SIGNALED).fallBack);        // the restart helper's kill
    assertFalse(after(13).fallBack);                                 // other
    assertFalse(after(VulkanGuard.REASON_UNKNOWN).fallBack);
  }

  @Test public void android10NamesNoReasonTheMarkCounts() {
    assertTrue(VulkanGuard.judge(true, PID, AT, 29, null, 0).fallBack);
  }

  @Test public void noRecordOfTheMarkedProcessOnceIsNoCrashTwiceIs() {
    // (no record, another process's, or one from before the mark: the pid used again)
    VulkanGuard.Verdict none = VulkanGuard.judge(true, PID, AT, 35, null, 0);
    assertFalse(none.fallBack);
    assertTrue(none.unsure);
    VulkanGuard.Exit other = new VulkanGuard.Exit(PID + 1, VulkanGuard.REASON_CRASH_NATIVE, 11, AT + 3_000, "");
    assertFalse(VulkanGuard.judge(true, PID, AT, 35, other, 0).fallBack);
    VulkanGuard.Exit older = new VulkanGuard.Exit(PID, VulkanGuard.REASON_CRASH_NATIVE, 11, AT - 3_600_000, "");
    assertFalse(VulkanGuard.judge(true, PID, AT, 35, older, 0).fallBack);
    VulkanGuard.Verdict again = VulkanGuard.judge(true, PID, AT, 35, null, 1);
    assertTrue(again.fallBack);
    assertTrue(again.unsure);
  }

  @Test public void aMarkOfAnOlderVersionWithoutItsProcessIsNoCrash() {
    // 0.99.3 to 0.99.5 set the mark without the process: still there after an update, it says nothing
    assertFalse(VulkanGuard.judge(true, 0, 0, 37, null, 0).fallBack);
  }

  @Test public void theSystemsTimeALittleBeforeTheMarkStillCounts() {
    VulkanGuard.Exit justBefore = new VulkanGuard.Exit(PID, VulkanGuard.REASON_CRASH_NATIVE, 11, AT - 2_000, "");
    assertTrue(VulkanGuard.judge(true, PID, AT, 35, justBefore, 0).fallBack);
  }
}
