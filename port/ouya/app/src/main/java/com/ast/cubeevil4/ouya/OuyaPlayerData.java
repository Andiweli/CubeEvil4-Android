package com.ast.cubeevil4.ouya;

import android.os.Environment;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.Enumeration;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.TimeZone;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;

/**
 * The menu's MANAGEMENT on the OUYA (wish of the project owner, 14.11.2026; the main app's PlayerBackup and
 * PlayerWipe, for Android 4.1): Android 4.1 has no file dialog (ACTION_CREATE_DOCUMENT came with 4.4), so the exports
 * go to the folder CubeEvil4-Potato on the drive with the game data (the USB drive or the internal storage), and the
 * import lists the .gci and .zip files there and in the Download folders.
 *
 * <p>The .zip is the main app's format 1 (PlayerBackup): CubeEvil4-backup.json (mark cubeevil4_backup),
 * memory-card/*.gci, instant/slot-0.ce4save; no touch layout and no settings (the OUYA's are its own). The main app
 * imports it, and this one imports the main app's (its .gci files and its instant save; the rest is left out).
 */
final class OuyaPlayerData {
    static final String FOLDER = "CubeEvil4-Potato";
    static final String MARK = "cubeevil4_backup", MANIFEST = "CubeEvil4-backup.json";
    static final String CARD_FOLDER = "memory-card/", INSTANT = "instant/slot-0.ce4save";
    /** runtime/instant_store.cpp: a header of 32 bytes, 0x12000 of payload, the magic "CE4INST\0" */
    static final int INSTANT_BYTES = 32 + 0x12000;
    private static final byte[] INSTANT_MAGIC = {'C', 'E', '4', 'I', 'N', 'S', 'T', 0};
    /** "Delete All Data": the wish, kept until the next start of the game (PlayerWipe) */
    static final String PREFS = "player_wipe", WIPE_AT_START = "wipeAtStart";

    private OuyaPlayerData() {}

    /** The drive of the game's data: the part before /Android/data/ (the installer's folder), else the internal
     *  storage. */
    static File drive(File data) {
        String path = data.getAbsolutePath();
        int at = path.indexOf("/Android/data/");
        return at > 0 ? new File(path.substring(0, at)) : Environment.getExternalStorageDirectory();
    }

    static File exportFolder(File data) { return new File(drive(data), FOLDER); }

    static File instantFile(File data) { return new File(new File(data, "instant"), "slot-0.ce4save"); }

    static File cardFile(File data) { return new File(new File(data, "memcard"), "card-a.bin"); }

    /** A file name without folders or characters a FAT drive does not take */
    static String safeName(String name) {
        return name.replaceAll("[\\\\/:*?\"<>|]", "_");
    }

    /** "CubeEvil4-backup-20261114-153000.zip", as the main app names its own */
    static String zipName(long now) {
        return "CubeEvil4-backup-" + new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(new Date(now)) + ".zip";
    }

    /** The .gci and .zip files to import: the export folder and the Download folders of the drives, newest first */
    static List<File> candidates(File data) {
        Set<File> roots = new LinkedHashSet<File>();
        roots.add(drive(data));
        roots.add(Environment.getExternalStorageDirectory());
        for (String usb : new String[] {"/mnt/usbdrive", "/storage/usbdrive", "/mnt/usb", "/mnt/sdcard"})
            roots.add(new File(usb));
        List<File> found = new ArrayList<File>();
        Set<String> seen = new LinkedHashSet<String>();
        for (File root : roots) {
            for (String folder : new String[] {FOLDER, "Download", "download", "Downloads"}) {
                File[] files = new File(root, folder).listFiles();
                if (files == null)
                    continue;
                for (File f : files) {
                    String name = f.getName().toLowerCase(Locale.ROOT);
                    if (!f.isFile() || !(name.endsWith(".gci") || name.endsWith(".zip")))
                        continue;
                    String key;
                    try {
                        key = f.getCanonicalPath();
                    } catch (IOException e) {
                        key = f.getAbsolutePath();
                    }
                    if (seen.add(key))
                        found.add(f);
                }
            }
        }
        Collections.sort(found, new Comparator<File>() {
            @Override
            public int compare(File a, File b) {
                return a.lastModified() < b.lastModified() ? 1 : a.lastModified() > b.lastModified() ? -1 : 0;
            }
        });
        return found;
    }

    /** A file's bytes; an error when it is not there or larger than `max` */
    static byte[] read(File f, int max) throws IOException {
        if (!f.isFile())
            throw new IOException("not found");
        if (f.length() > max)
            throw new IOException("too large");
        InputStream in = new FileInputStream(f);
        try {
            return readAll(in, max);
        } finally {
            in.close();
        }
    }

    private static byte[] readAll(InputStream in, int max) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[65536];
        for (int n; (n = in.read(buffer)) > 0; ) {
            out.write(buffer, 0, n);
            if (out.size() > max)
                throw new IOException("too large");
        }
        return out.toByteArray();
    }

    static void write(File f, byte[] data) throws IOException {
        File parent = f.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs())
            throw new IOException("no folder " + parent);
        FileOutputStream out = new FileOutputStream(f);
        try {
            out.write(data);
            out.getFD().sync();
        } finally {
            out.close();
        }
    }

    /** The instant save (runtime/instant_store.cpp), null when there is none or it is not one */
    static byte[] instant(File data) {
        try {
            byte[] f = read(instantFile(data), INSTANT_BYTES);
            return instantOk(f) ? f : null;
        } catch (IOException e) {
            return null;
        }
    }

    static boolean instantOk(byte[] f) {
        if (f == null || f.length != INSTANT_BYTES)
            return false;
        for (int i = 0; i < INSTANT_MAGIC.length; i++)
            if (f[i] != INSTANT_MAGIC[i])
                return false;
        return true;
    }

    /** An imported instant save over the game's: written beside it, then renamed (the core sees the old or the new
     *  one; the main app's PlayerBackup.writeAtomically) */
    static void writeInstant(File data, byte[] f) throws IOException {
        if (!instantOk(f))
            throw new IOException("not an instant save");
        File slot = instantFile(data);
        File part = new File(slot.getParentFile(), slot.getName() + ".import");
        write(part, f);
        if (!part.renameTo(slot)) {
            part.delete();
            throw new IOException("cannot replace " + slot.getName());
        }
    }

    /** The ZIP (the manifest first) */
    static void writeZip(File target, List<String> names, List<byte[]> gcis, byte[] instant, String app,
            String device) throws IOException {
        File parent = target.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs())
            throw new IOException("no folder " + parent);
        ZipOutputStream zip = new ZipOutputStream(new FileOutputStream(target));
        try {
            entry(zip, MANIFEST, manifest(names, instant != null, app, device).getBytes("UTF-8"));
            for (int i = 0; i < gcis.size(); i++)
                entry(zip, CARD_FOLDER + names.get(i), gcis.get(i));
            if (instant != null)
                entry(zip, INSTANT, instant);
        } finally {
            zip.close();
        }
    }

    private static void entry(ZipOutputStream zip, String name, byte[] data) throws IOException {
        zip.putNextEntry(new ZipEntry(name));
        zip.write(data);
        zip.closeEntry();
    }

    private static String manifest(List<String> names, boolean instant, String app, String device) {
        try {
            JSONObject m = new JSONObject();
            m.put(MARK, 1);
            m.put("app", app);
            m.put("versionCode", 0);
            m.put("device", device);
            SimpleDateFormat f = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss'Z'", Locale.ROOT);
            f.setTimeZone(TimeZone.getTimeZone("UTC"));
            m.put("made", f.format(new Date()));
            JSONArray card = new JSONArray();
            for (String name : names)
                card.put(name);
            m.put("memoryCard", card);
            m.put("touchLayout", false);
            m.put("instantSave", instant);
            m.put("settings", false);
            return m.toString(1) + "\n";
        } catch (JSONException e) {
            throw new IllegalStateException(e);
        }
    }

    /** What a ZIP holds for this app: its .gci files (in any folder) and the instant save */
    static final class Zip {
        final List<String> names = new ArrayList<String>();
        final List<byte[]> files = new ArrayList<byte[]>();
        byte[] instant;
    }

    static Zip readZip(File f) throws IOException {
        Zip z = new Zip();
        ZipFile zip = new ZipFile(f);
        try {
            Enumeration<? extends ZipEntry> entries = zip.entries();
            while (entries.hasMoreElements()) {
                ZipEntry e = entries.nextElement();
                String name = e.getName();
                if (e.isDirectory())
                    continue;
                String lower = name.toLowerCase(Locale.ROOT);
                if (lower.endsWith(".gci")) {
                    InputStream in = zip.getInputStream(e);
                    try {
                        z.files.add(readAll(in, 4 << 20));
                    } finally {
                        in.close();
                    }
                    z.names.add(name.substring(name.lastIndexOf('/') + 1));
                } else if (name.equals(INSTANT)) {
                    InputStream in = zip.getInputStream(e);
                    try {
                        byte[] slot = readAll(in, INSTANT_BYTES);
                        z.instant = instantOk(slot) ? slot : null;
                    } finally {
                        in.close();
                    }
                }
            }
        } finally {
            zip.close();
        }
        if (z.files.isEmpty() && z.instant == null)
            throw new IOException("no save in it");
        return z;
    }

    /** "Delete All Data" at the start, before the core opens the card: the card's file and the instant save go; the
     *  core makes a new, formatted card (runtime/memcard.cpp); one line for the log */
    static String wipe(File data) {
        StringBuilder note = new StringBuilder("delete all data as asked in the menu:");
        File[] files = {cardFile(data), instantFile(data), new File(instantFile(data).getPath() + ".tmp"),
                new File(instantFile(data).getPath() + ".import")};
        for (File f : files)
            if (f.exists())
                note.append(' ').append(f.getName()).append(f.delete() ? " deleted" : " not deleted");
        return note.toString();
    }
}
