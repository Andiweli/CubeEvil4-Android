package com.ast.cubeevil4.texturepacker;

import static org.junit.Assert.*;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.os.SystemClock;
import android.text.Layout;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.util.ArrayList;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.GraphicsMode;

/**
 * The packer's screen with real text widths (Robolectric's native graphics) on a phone (360 dp wide), in every
 * language, with the system font at 100 % and 130 %, while a pack is ready, being converted and done (wish of the
 * project owner, 13.11.2026: larger text, the buttons side by side on one half each): no text cut off or shortened
 * ("…"), nothing outside the screen, each pair of buttons the same width, side by side, not overlapping. With the
 * environment variable PACKER_SHOTS set to a folder, pictures of English and German go there (packer-*.png).
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = 33, qualifiers = "w360dp-h780dp-port-xhdpi")
public class PackerScreenTest {
  static final String[] LANGUAGES = {"en", "de", "es", "fr", "it", "pt", "ru", "ar", "zh", "hi", "in"};
  static final String SUMMARY = "name\tResident Evil 4 HD Project\nimages\t5916\nbytes\t11929097886\nskipped\t11\n"
      + "group\tAttache case variations\t0\nvariant\tBlack Attache Case\t0\nvariant\tGold Attache Case\t0\n"
      + "variant\tOriginal Attache Case\t1\n"
      + "group\tButton Replacements\t0\nvariant\tGameCube\t1\nvariant\tXbox\t0\nvariant\tPlayStation\t0\n"
      + "group\tExtras\t1\nvariant\tExtras\t0\n";

  private static void state(int which) {
    Job.handle = 0;
    Job.summary = Summary.parse(SUMMARY);
    Job.choices = Job.summary.defaultChoices();
    Job.estimates = new long[] {298_000_000L, 803_000_000L, 2_570_000_000L};
    Job.size = 512;
    Job.failure = null;
    Job.cancelled = false;
    Job.state = which;
    Job.done = 2_431;
    Job.total = 5_916;
    Job.written = 295_000_000L;
    Job.startedAt = SystemClock.elapsedRealtime() - 9 * 60_000L;
    Job.resultName = "CubeEvil4-textures-512.c4tex";
    Job.resultBytes = 716_315_264L;
    Job.seconds = 21 * 60;
    Job.report = which == Job.DONE ? "CubeEvil4 Texture Packer 1.3, 2026-11-13\n..." : null;
    Job.reportName = "CubeEvil4-textures-512.txt";
  }

  private static List<MainActivity.Volume> volumes(String language) {
    List<MainActivity.Volume> out = new ArrayList<>();
    boolean de = language.equals("de");
    String[][] v = {{"external_primary", de ? "Interner gemeinsamer Speicher" : "Internal shared storage"},
        {"1234-abcd", de ? "SD-Karte von SanDisk" : "SanDisk SD card"}};
    long[] free = {23_400_000_000L, 112_800_000_000L};
    for (int i = 0; i < v.length; i++) {
      MainActivity.Volume vol = new MainActivity.Volume();
      vol.name = v[i][0];
      vol.label = v[i][1];
      vol.free = free[i];
      out.add(vol);
    }
    return out;
  }

  /** Everything inside the screen, every text whole, every pair of buttons side by side with the same width. */
  private static int check(View v, String where, StringBuilder problems) {
    int checks = 0;
    if (v.getVisibility() != View.VISIBLE) return 0;
    if (v instanceof TextView) {
      TextView t = (TextView) v;
      Layout l = t.getLayout();
      if (l != null && t.length() > 0) {
        int room = t.getWidth() - t.getTotalPaddingLeft() - t.getTotalPaddingRight();
        int height = t.getHeight() - t.getTotalPaddingTop() - t.getTotalPaddingBottom();
        for (int i = 0; i < l.getLineCount(); i++) {
          if (l.getEllipsisCount(i) > 0) problems.append(where).append(": shortened \"").append(t.getText()).append("\"\n");
          // the visible part: a space where the line wraps may hang over the edge (it is not drawn)
          CharSequence text = t.getText();
          int start = l.getLineStart(i), end = l.getLineEnd(i);
          while (end > start && Character.isWhitespace(text.charAt(end - 1))) end--;
          float visible = end > start ? Layout.getDesiredWidth(text, start, end, t.getPaint()) : 0;
          if (visible > room + 1)
            problems.append(where).append(": wider than its room (line ").append(i).append(" ").append(visible)
                .append(" px, room ").append(room).append(") \"").append(text).append("\"\n");
        }
        if (l.getHeight() > height + 1)
          problems.append(where).append(": cut off at the bottom \"").append(t.getText()).append("\"\n");
        checks++;
      }
    }
    if (v instanceof ViewGroup) {
      ViewGroup g = (ViewGroup) v;
      List<View> shown = new ArrayList<>();
      for (int i = 0; i < g.getChildCount(); i++) {
        View c = g.getChildAt(i);
        if (c.getVisibility() != View.VISIBLE) continue;
        shown.add(c);
        if (c.getLeft() < 0 || c.getRight() > g.getWidth())
          problems.append(where).append(": ").append(c.getClass().getSimpleName()).append(" outside its row (")
              .append(c.getLeft()).append("..").append(c.getRight()).append(" of ").append(g.getWidth()).append(")\n");
        checks += check(c, where, problems);
      }
      if (g instanceof LinearLayout && ((LinearLayout) g).getOrientation() == LinearLayout.HORIZONTAL
          && shown.size() == 2 && shown.get(0) instanceof Button && shown.get(1) instanceof Button) {
        View a = shown.get(0), b = shown.get(1);
        View left = a.getLeft() <= b.getLeft() ? a : b, right = left == a ? b : a;
        if (Math.abs(a.getWidth() - b.getWidth()) > 1)
          problems.append(where).append(": a pair of buttons with different widths\n");
        if (a.getTop() != b.getTop() || a.getHeight() != b.getHeight())
          problems.append(where).append(": a pair of buttons not side by side at the same height\n");
        if (left.getRight() > right.getLeft()) problems.append(where).append(": a pair of buttons overlaps\n");
        if (Math.abs(a.getWidth() * 2 - g.getWidth()) > 2)
          problems.append(where).append(": a button is not half of the row\n");
        checks++;
      }
    }
    return checks;
  }

  @Test public void everyLanguageFitsAPhone() throws Exception {
    String shots = System.getenv("PACKER_SHOTS");
    if (shots != null && !shots.isEmpty()) new File(shots).mkdirs();
    StringBuilder problems = new StringBuilder();
    int checks = 0;
    String[] names = {"ready", "converting", "done"};
    int[] states = {Job.READY, Job.CONVERTING, Job.DONE};
    for (String language : LANGUAGES)
      for (float scale : new float[] {1f, 1.3f})
        for (int s = 0; s < states.length; s++) {
          RuntimeEnvironment.setQualifiers("+" + language);
          RuntimeEnvironment.setFontScale(scale);
          state(states[s]);
          try (var controller = Robolectric.buildActivity(MainActivity.class).setup()) {
            MainActivity activity = controller.get();
            activity.useVolumes(volumes(language));
            ScrollView scroll = (ScrollView) ((ViewGroup) activity.findViewById(android.R.id.content)).getChildAt(0);
            View page = scroll.getChildAt(0);
            page.setLayoutDirection(activity.getResources().getConfiguration().getLayoutDirection());
            int width = Math.round(360 * activity.getResources().getDisplayMetrics().density);
            page.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
            page.layout(0, 0, width, page.getMeasuredHeight());
            String where = language + " " + Math.round(scale * 100) + " % " + names[s];
            checks += check(page, where, problems);
            if (shots != null && !shots.isEmpty() && (language.equals("en") || language.equals("de"))) {
              Bitmap picture = Bitmap.createBitmap(width, page.getMeasuredHeight(), Bitmap.Config.ARGB_8888);
              Canvas canvas = new Canvas(picture);
              canvas.drawColor(MainActivity.BACKGROUND);
              page.draw(canvas);
              String file = "packer-" + language + "-" + names[s] + (scale > 1f ? "-130" : "") + ".png";
              try (FileOutputStream out = new FileOutputStream(new File(shots, file))) {
                picture.compress(Bitmap.CompressFormat.PNG, 100, out);
              }
            }
          }
        }
    assertEquals("", problems.toString());
    assertTrue("texts and button pairs were checked: " + checks, checks > 11 * 2 * 3 * 20);
  }
}
