package com.ast.cubeevil4.texturepacker;

import java.util.ArrayList;
import java.util.List;

/** What a pack holds, from PackerNative.summary: its name, images, size, files left out, and its groups of choices. */
final class Summary {
  static final class Group {
    String name = "";
    /** an add-on: a folder of extra images that is either added or not */
    boolean addOn;
    final List<String> variants = new ArrayList<>();
    /** per variant: its images are the same as the main folder's */
    final List<Boolean> sameAsMain = new ArrayList<>();
  }

  String name = "";
  int images;
  long bytes;
  int skipped;
  final List<Group> groups = new ArrayList<>();

  static Summary parse(String text) {
    Summary s = new Summary();
    if (text == null) return s;
    Group group = null;
    for (String line : text.split("\n")) {
      String[] f = line.split("\t", -1);
      if (f.length < 2) continue;
      switch (f[0]) {
        case "name": s.name = f[1]; break;
        case "images": s.images = parseInt(f[1]); break;
        case "bytes": s.bytes = parseLong(f[1]); break;
        case "skipped": s.skipped = parseInt(f[1]); break;
        case "group":
          group = new Group();
          group.name = f[1];
          group.addOn = f.length > 2 && f[2].equals("1");
          s.groups.add(group);
          break;
        case "variant":
          if (group != null) {
            group.variants.add(f[1]);
            group.sameAsMain.add(f.length > 2 && f[2].equals("1"));
          }
          break;
        default: break;
      }
    }
    return s;
  }

  /** No choice made: each group as in the main folder (an add-on: left out). */
  int[] defaultChoices() {
    int[] c = new int[groups.size()];
    java.util.Arrays.fill(c, -1);
    return c;
  }

  private static int parseInt(String s) {
    try {
      return Integer.parseInt(s.trim());
    } catch (NumberFormatException e) {
      return 0;
    }
  }

  private static long parseLong(String s) {
    try {
      return Long.parseLong(s.trim());
    } catch (NumberFormatException e) {
      return 0;
    }
  }
}
