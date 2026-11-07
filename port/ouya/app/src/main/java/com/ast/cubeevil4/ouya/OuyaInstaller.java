package com.ast.cubeevil4.ouya;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.os.Environment;
import android.os.Handler;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import com.ast.re4decomp.disc.DiscInstaller;
import com.ast.re4decomp.disc.GameCubeDisc;
import com.ast.re4decomp.disc.ReferenceCatalog;
import com.ast.re4decomp.disc.RvzImage;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * The OUYA app's disc installer (wishes of the project owner, 07.11.2026): disc 1 and 2 of the debug build or of
 * the US, European or Japanese release, from disc images (ISO, GCM, RVZ) on the OUYA's own storage or a USB
 * drive, into the internal storage or a USB drive as the player chooses (RE4-OUYA there). The discs are read,
 * checked against the release's code catalog and unpacked with the main app's classes
 * (com.ast.re4decomp.disc, copied by port/ouya/tools/sync_disc_import.py); the folder then holds disc1 and
 * disc2 as the main app's import does, which the game core reads (runtime/dvd.cpp). In the main app's look;
 * texts in English (the project's rule for app texts outside the in-game menu).
 */
final class OuyaInstaller extends ScrollView {
    /** The installed data folder (the app starts the game with it). */
    interface Host {
        void installed(File root);
        /** "Back to the game" (only with discs installed before): the game with them. */
        void cancelled();
    }

    static final String PREFS = "install", ROOT = "dataRoot", AGAIN = "installAgain";

    // the main app's colours (CoreMenu)
    private static final int PANEL = 0xff191f1c, SURFACE = 0xff222a25, BORDER = 0xff414b41;
    private static final int INK = 0xfff0eee5, MUTED = 0xffa6afa3, GOLD = 0xffd7bc7d, ACTIVE = 0xff45432e;

    /** A disc image found */
    static final class Found {
        final File file;
        final boolean rvz;
        final int disc;         // 1, 2; 0: not known yet (RVZ: after unpacking)
        final String release;   // GameCubeDisc.DEBUG ..., null for RVZ
        Found(File file, boolean rvz, int disc, String release) {
            this.file = file;
            this.rvz = rvz;
            this.disc = disc;
            this.release = release;
        }
    }

    private final Host host;
    private final Handler main = new Handler();
    private final LinearLayout body;
    private final TextView status;
    private final List<Found> found = new ArrayList<Found>();
    private final boolean canCancel;
    private final File previous;  // the data folder before (its memory card comes along), or null
    private boolean working;

    OuyaInstaller(Context context, Host host, File previous) {
        super(context);
        this.host = host;
        this.canCancel = previous != null;
        this.previous = previous;
        GameCubeDisc.importEverything = true;  // the game core reads every file through its own table
        GameCubeDisc.retailAllowed = true;
        setBackgroundColor(0xff000000);
        setFillViewport(true);
        LinearLayout frame = new LinearLayout(context);
        frame.setGravity(Gravity.CENTER);
        body = new LinearLayout(context);
        body.setOrientation(LinearLayout.VERTICAL);
        body.setPadding(dp(24), dp(18), dp(24), dp(18));
        body.setBackgroundDrawable(shape(PANEL, BORDER, 10));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(Math.min(dp(900),
                getResources().getDisplayMetrics().widthPixels * 9 / 10), ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.setMargins(0, dp(24), 0, dp(24));
        frame.addView(body, lp);
        addView(frame, new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        status = text("", 16, false);
        status.setTextColor(GOLD);
        search();
    }

    /** The data folder the app installed before, if its disc 1 is complete (else null). */
    static File installedRoot(Context context) {
        String path = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(ROOT, null);
        if (path == null)
            return null;
        File root = new File(path);
        return DiscInstaller.coreReady(new File(root, "disc1"), 1) ? root : null;
    }

    // --- where: the storage the OUYA has (internal and USB drives) ---------------------------------

    static List<File> storages() {
        List<File> roots = new ArrayList<File>();
        add(roots, Environment.getExternalStorageDirectory());
        // USB drives: the OUYA mounts them at /mnt/usbdrive (also as /storage/usbdrive); other Android 4.x
        // devices use these names
        String[] usb = {"/mnt/usbdrive", "/storage/usbdrive", "/mnt/usb", "/storage/usb", "/storage/usb0",
                "/storage/usb1", "/storage/usbdisk", "/mnt/usbdisk", "/mnt/sda1", "/mnt/sdb1", "/mnt/media_rw/sda1"};
        for (String path : usb)
            add(roots, new File(path));
        File[] mounts = new File("/storage").listFiles();
        if (mounts != null)
            for (File m : mounts)
                if (m.getName().toLowerCase(Locale.ROOT).contains("usb"))
                    add(roots, m);
        return roots;
    }

    private static void add(List<File> roots, File dir) {
        if (dir == null || !dir.isDirectory() || !dir.canRead())
            return;
        try {
            File canonical = dir.getCanonicalFile();
            for (File r : roots)
                if (r.getCanonicalFile().equals(canonical))
                    return;
        } catch (IOException e) {
            return;
        }
        // an empty mount point (no drive in it) has no space
        if (dir.getTotalSpace() <= 0)
            return;
        roots.add(dir);
    }

    private static boolean internal(File root) {
        try {
            return root.getCanonicalFile().equals(Environment.getExternalStorageDirectory().getCanonicalFile());
        } catch (IOException e) {
            return false;
        }
    }

    private static String nameOf(File root) {
        return internal(root) ? "Internal storage" : "USB storage (" + root.getPath() + ")";
    }

    // --- what: disc images on them -------------------------------------------------------------------

    private void search() {
        found.clear();
        for (File root : storages())
            look(root, 0);
        show();
    }

    private void look(File dir, int depth) {
        File[] files = dir.listFiles();
        if (files == null)
            return;
        for (File f : files) {
            String name = f.getName().toLowerCase(Locale.ROOT);
            if (f.isDirectory()) {
                // the app's own folder and Android's hidden ones are not searched
                if (depth < 2 && !name.startsWith(".") && !name.equals("re4-ouya") && !name.equals("android"))
                    look(f, depth + 1);
            } else if (name.endsWith(".iso") || name.endsWith(".gcm")) {
                try {
                    GameCubeDisc disc = new GameCubeDisc(f);
                    try {
                        found.add(new Found(f, false, disc.discNumber, disc.release));
                    } finally {
                        disc.close();
                    }
                } catch (IOException e) {
                    // not a disc of the game: left out
                }
            } else if (name.endsWith(".rvz")) {
                found.add(new Found(f, true, 0, null));
            }
        }
    }

    private void show() {
        body.removeAllViews();
        TextView title = text("CubeEvil4 for OUYA: install the game discs", 22, true);
        body.addView(title);
        separator();
        help("Put disc images of Resident Evil 4 for the GameCube (ISO, GCM or RVZ: disc 1, and disc 2 for the "
                + "later chapters) on the OUYA's storage or a USB drive. The debug build and the US, European and "
                + "Japanese releases work; both discs must be of the same one. The installer checks them and "
                + "unpacks the game files (about 1.4 GB a disc).");
        heading("Disc images found");
        if (found.isEmpty()) {
            help("None. Copy the images to the OUYA (for example into Download) or plug in a USB drive with them, "
                    + "then search again.");
        }
        for (Found f : found) {
            String what = f.rvz ? "RVZ image (disc and release known after unpacking)"
                    : "Disc " + f.disc + ", " + GameCubeDisc.releaseTitle(f.release);
            TextView row = text(what + "\n" + f.file.getPath(), 15, false);
            row.setPadding(dp(8), dp(4), dp(8), dp(4));
            body.addView(row);
        }
        Button again = button("Search again", new OnClickListener() {
            @Override
            public void onClick(View v) {
                if (!working)
                    search();
            }
        });
        body.addView(again, cell());
        heading("Install to");
        List<File> roots = storages();
        Button first = null;
        for (final File root : roots) {
            final boolean writable = root.canWrite();
            String label = nameOf(root) + ", " + gb(root.getUsableSpace()) + " free" + (writable ? "" : " (read only)");
            Button b = button(label, new OnClickListener() {
                @Override
                public void onClick(View v) {
                    if (!working && writable)
                        install(new File(root, "RE4-OUYA"));
                }
            });
            b.setEnabled(writable && !found.isEmpty());
            body.addView(b, cell());
            if (first == null && b.isEnabled())
                first = b;
        }
        help("The game data needs about 3 GB with both discs (an RVZ image needs another 1.5 GB while it is "
                + "unpacked). Saves go to the same place.");
        body.addView(status);
        if (canCancel) {
            body.addView(button("Back to the game (the discs installed before)", new OnClickListener() {
                @Override
                public void onClick(View v) {
                    if (!working)
                        host.cancelled();
                }
            }), cell());
        }
        if (first != null)
            first.requestFocus();
        else
            again.requestFocus();
    }

    // --- installing -----------------------------------------------------------------------------------

    private void install(final File root) {
        working = true;
        say("Starting ...");
        new Thread(new Runnable() {
            @Override
            public void run() {
                String problem = null;
                try {
                    installAll(root);
                } catch (IOException | RuntimeException e) {
                    problem = e.getMessage() != null ? e.getMessage() : e.toString();
                }
                final String result = problem;
                main.post(new Runnable() {
                    @Override
                    public void run() {
                        working = false;
                        if (result != null) {
                            say("Not installed: " + result);
                            return;
                        }
                        getContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                                .putString(ROOT, root.getAbsolutePath()).putBoolean(AGAIN, false).apply();
                        host.installed(root);
                    }
                });
            }
        }, "install").start();
    }

    private void installAll(File root) throws IOException {
        if (!root.isDirectory() && !root.mkdirs())
            throw new IOException("Cannot make the folder " + root);
        DiscInstaller.recover(root);
        final DiscInstaller.Catalogs catalogs = new DiscInstaller.Catalogs() {
            @Override
            public ReferenceCatalog forRelease(String release) throws IOException {
                return new ReferenceCatalog(getContext().getAssets().open(GameCubeDisc.catalog("references", release)));
            }
        };
        boolean[] done = new boolean[3];
        String release = null;
        // the plain images first (an RVZ of the same disc is then not needed)
        List<Found> order = new ArrayList<Found>();
        for (Found f : found)
            if (!f.rvz)
                order.add(f);
        for (Found f : found)
            if (f.rvz)
                order.add(f);
        File incoming = new File(root, ".incoming");
        for (Found f : order) {
            if (!f.rvz && (f.disc < 1 || f.disc > 2 || done[f.disc]))
                continue;
            File image = f.file;
            if (f.rvz) {
                if (done[1] && done[2])
                    continue;
                if (!incoming.isDirectory() && !incoming.mkdirs())
                    throw new IOException("Cannot make the folder " + incoming);
                long[] sizes = RvzImage.sizes(f.file);
                if (root.getUsableSpace() < RvzImage.storageNeeded(sizes[0], sizes[1]))
                    throw new IOException("Not enough free storage to unpack " + f.file.getName() + " (needs "
                            + RvzImage.gigabytes(RvzImage.storageNeeded(sizes[0], sizes[1])) + ")");
                image = new File(incoming, "disc.iso");
                final String name = f.file.getName();
                RvzImage.unpack(f.file, image, new RvzImage.Progress() {
                    @Override
                    public boolean step(long d, long total) {
                        progress("Unpacking " + name, d, total);
                        return true;
                    }
                });
            }
            try {
                GameCubeDisc disc = new GameCubeDisc(image);
                final int number;
                final String discRelease;
                try {
                    number = disc.discNumber;
                    discRelease = disc.release;
                } finally {
                    disc.close();
                }
                if (number < 1 || number > 2 || done[number])
                    continue;
                if (release != null && !release.equals(discRelease))
                    throw new IOException("Disc " + number + " is of another release ("
                            + GameCubeDisc.releaseTitle(discRelease) + ") than the other disc ("
                            + GameCubeDisc.releaseTitle(release) + ")");
                final String label = "Disc " + number + " (" + GameCubeDisc.releaseTitle(discRelease) + "): ";
                DiscInstaller.install(image, root, number, catalogs, new GameCubeDisc.Progress() {
                    @Override
                    public void update(String phase, long d, long total) {
                        progress(label + phase, d, total);
                    }
                });
                done[number] = true;
                release = discRelease;
            } finally {
                if (f.rvz)
                    image.delete();
            }
        }
        DiscInstaller.delete(incoming);
        if (!done[1])
            throw new IOException("Disc 1 was not among the images");
        // an older disc 2 of another release next to the new disc 1 would mix two builds' data
        if (!done[2] && DiscInstaller.release(new File(root, "disc2")) != null && !DiscInstaller.sameRelease(root))
            DiscInstaller.delete(new File(root, "disc2"));
        // the developer's folder of the first tests (disc0, ouya-app.ps1): the core would take it before disc1
        File old = new File(root, "disc0");
        if (old.isDirectory())
            DiscInstaller.delete(old);
        // the memory card of the folder before (the player's saves), when the new place has none yet
        copyCard(root);
    }

    private void copyCard(File root) throws IOException {
        if (previous == null)
            return;
        File from = new File(previous, "memcard"), to = new File(root, "memcard");
        File[] files = from.listFiles();
        if (files == null || files.length == 0 || to.isDirectory() && to.list() != null && to.list().length > 0)
            return;
        try {
            if (from.getCanonicalFile().equals(to.getCanonicalFile()))
                return;
        } catch (IOException e) {
            return;
        }
        if (!to.isDirectory() && !to.mkdirs())
            throw new IOException("Cannot make the folder " + to);
        byte[] buffer = new byte[64 * 1024];
        for (File f : files) {
            if (!f.isFile())
                continue;
            say("Copying the memory card: " + f.getName());
            java.io.InputStream in = new java.io.FileInputStream(f);
            try {
                java.io.OutputStream out = new java.io.FileOutputStream(new File(to, f.getName()));
                try {
                    for (int n; (n = in.read(buffer)) > 0;)
                        out.write(buffer, 0, n);
                } finally {
                    out.close();
                }
            } finally {
                in.close();
            }
        }
    }

    private long lastProgress;

    private void progress(final String phase, long done, long total) {
        final long now = System.currentTimeMillis();
        if (now - lastProgress < 250 && done < total)
            return;
        lastProgress = now;
        final int percent = total > 0 ? (int) (done * 100 / total) : 0;
        say(phase + "  " + percent + " %");
    }

    private void say(final String text) {
        main.post(new Runnable() {
            @Override
            public void run() {
                status.setText(text);
            }
        });
    }

    private static String gb(long bytes) {
        return String.format(Locale.ROOT, "%.1f GB", bytes / 1e9);
    }

    // --- the main app's look (OuyaMenu, CoreMenu) -------------------------------------------------------

    private int dp(int n) { return Math.round(n * getResources().getDisplayMetrics().density); }

    private TextView text(String label, int size, boolean bold) {
        TextView v = new TextView(getContext());
        v.setText(label);
        v.setTextSize(size);
        v.setTextColor(bold ? INK : MUTED);
        if (bold)
            v.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        return v;
    }

    private void help(String s) {
        TextView t = text(s, 15, false);
        t.setPadding(dp(7), dp(4), dp(7), dp(8));
        body.addView(t);
    }

    private void heading(String s) {
        TextView v = text(s.toUpperCase(Locale.ROOT), 14, true);
        v.setTextColor(MUTED);
        v.setPadding(dp(7), dp(16), dp(7), dp(8));
        body.addView(v);
    }

    private void separator() {
        View v = new View(getContext());
        v.setBackgroundColor(BORDER);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(1));
        lp.setMargins(0, dp(10), 0, dp(8));
        body.addView(v, lp);
    }

    private GradientDrawable shape(int fill, int stroke, int radius) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(fill);
        d.setCornerRadius(dp(radius));
        d.setStroke(dp(1), stroke);
        return d;
    }

    private Button button(String label, OnClickListener click) {
        Button b = new Button(getContext());
        StateListDrawable state = new StateListDrawable();
        GradientDrawable focus = shape(SURFACE, INK, 6);
        focus.setStroke(dp(2), INK);
        state.addState(new int[] {android.R.attr.state_focused}, focus);
        state.addState(new int[] {android.R.attr.state_pressed}, shape(ACTIVE, GOLD, 6));
        state.addState(new int[0], shape(SURFACE, BORDER, 6));
        b.setBackgroundDrawable(state);
        b.setTextSize(16);
        b.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        b.setTextColor(new ColorStateList(new int[][] {new int[] {-android.R.attr.state_enabled}, new int[0]},
                new int[] {0xff70796f, INK}));
        b.setPadding(dp(12), dp(8), dp(12), dp(8));
        b.setMinHeight(dp(48));
        b.setGravity(Gravity.LEFT | Gravity.CENTER_VERTICAL);
        b.setText(label);
        b.setOnClickListener(click);
        return b;
    }

    private LinearLayout.LayoutParams cell() {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.setMargins(dp(7), dp(7), dp(7), dp(7));
        return lp;
    }
}
