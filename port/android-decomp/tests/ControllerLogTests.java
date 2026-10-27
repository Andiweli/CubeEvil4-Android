package com.ast.re4decomp;

public final class ControllerLogTests {
  static int failures;
  static void check(boolean ok, String what) { if (!ok) { failures++; System.err.println("FAIL: " + what); } }
  public static void main(String[] args) {
    ControllerLog log = new ControllerLog();
    check(log.firstKey(5, 98) && !log.firstKey(5, 98), "a key is named once per device");
    check(log.firstKey(6, 98), "the same key on another device is named");
    check(log.firstAxis(5, 32) && !log.firstAxis(5, 32), "an axis is named once");
    check(log.firstDevice(5) && !log.firstDevice(5), "a device is described once");
    log.forget(5);
    check(log.firstDevice(5) && log.firstKey(5, 98) && log.firstAxis(5, 32), "a changed device is reported again");
    check(!log.firstKey(6, 98), "other devices keep their state");
    ControllerLog noisy = new ControllerLog();
    int named = 0;
    for (int code = 0; code < 400; code++) if (noisy.firstKey(1, code)) named++;
    check(named == ControllerLog.LIMIT, "at most " + ControllerLog.LIMIT + " keys are named");
    check(ControllerLog.readAxis(0) && ControllerLog.readAxis(11) && ControllerLog.readAxis(15) && ControllerLog.readAxis(23), "read axes");
    check(!ControllerLog.readAxis(32) && !ControllerLog.readAxis(37) && !ControllerLog.readAxis(2), "unread axes");
    if (failures != 0) { System.err.println(failures + " failures"); System.exit(1); }
    System.out.println("ControllerLogTests: ok");
  }
}
