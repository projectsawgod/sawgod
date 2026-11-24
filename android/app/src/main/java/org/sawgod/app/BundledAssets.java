package org.sawgod.app;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.*;

/** Incremental APK asset installation; independent of Android for JVM regression tests. */
final class BundledAssets {
    interface Source { InputStream open(String path) throws IOException; }
    interface DirectorySync { void sync(File directory) throws IOException; }

    private static Map<String, String> parse(byte[] manifest) throws IOException {
        Map<String, String> rows = new LinkedHashMap<>();
        for (String row : new String(manifest, StandardCharsets.UTF_8).split("\n")) {
            if (row.length() < 67 || !row.substring(0, 66).matches("[0-9a-f]{64}  "))
                throw new IOException("Invalid bundled manifest row");
            String path = row.substring(66);
            // Only authored asset roots can ever be installed or pruned. Never accept traversal.
            if (!path.matches("(gb_maps|text|soundfonts)/[A-Za-z0-9_./ -]+"))
                throw new IOException("Invalid bundled asset path: " + path);
            for (String part : path.split("/", -1))
                if (part.isEmpty() || part.equals(".") || part.equals(".."))
                    throw new IOException("Invalid bundled asset path: " + path);
            if (rows.put(path, row.substring(0, 64)) != null)
                throw new IOException("Duplicate bundled asset: " + path);
        }
        if (rows.isEmpty()) throw new IOException("Empty bundled manifest");
        return rows;
    }

    private static File destination(File root, String path) throws IOException {
        File file = new File(root, path);
        if (!file.getCanonicalFile().equals(file.getAbsoluteFile()))
            throw new IOException("Bundled asset crosses a symbolic link: " + path);
        return file;
    }

    private static String hash(File file) throws IOException, NoSuchAlgorithmException {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream in = new FileInputStream(file)) {
            byte[] buffer = new byte[65536];
            int count;
            while ((count = in.read(buffer)) != -1) digest.update(buffer, 0, count);
        }
        StringBuilder hex = new StringBuilder(64);
        for (byte b : digest.digest()) hex.append(String.format(Locale.ROOT, "%02x", b & 255));
        return hex.toString();
    }

    private static void replace(File dest, InputStream input) throws IOException {
        File temp = null;
        try (InputStream in = input) {
            // Unique sibling avoids touching an unlisted user file or following a stale symlink.
            temp = File.createTempFile(".bundled-", ".new", dest.getParentFile());
            try (FileOutputStream out = new FileOutputStream(temp)) {
                byte[] buffer = new byte[65536];
                int count;
                while ((count = in.read(buffer)) != -1) out.write(buffer, 0, count);
                // A directory fsync persists names, not contents. Flush only files actually replaced.
                out.getFD().sync();
            }
            Files.move(temp.toPath(), dest.toPath(), StandardCopyOption.ATOMIC_MOVE,
                       StandardCopyOption.REPLACE_EXISTING);
        } finally {
            if (temp != null) Files.deleteIfExists(temp.toPath());
        }
    }

    static void install(File directory, byte[] manifest, Source source, DirectorySync sync)
            throws IOException, NoSuchAlgorithmException {
        File root = directory.getCanonicalFile();
        File stamp = new File(root, ".bundled-assets");
        Map<String, String> wanted = parse(manifest), installed = null;
        byte[] previous = stamp.isFile() ? Files.readAllBytes(stamp.toPath()) : null;
        if (previous != null) {
            try { installed = parse(previous); }
            catch (IOException ignored) { /* Legacy digest or invalid stamp: hash existing files. */ }
        }
        boolean changed = false;
        Set<File> parents = new LinkedHashSet<>();
        for (Map.Entry<String, String> row : wanted.entrySet()) {
            File dest = destination(root, row.getKey());
            for (File dir = dest.getParentFile(); dir != null; dir = dir.getParentFile()) {
                parents.add(dir);
                if (dir.equals(root)) break;
            }
            if (dest.isFile() && row.getValue().equals(installed == null ? hash(dest) : installed.get(row.getKey())))
                continue;
            File parent = dest.getParentFile();
            if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create " + parent);
            replace(dest, source.open(row.getKey()));
            changed = true;
        }
        if (installed != null)
            for (String path : installed.keySet())
                if (!wanted.containsKey(path)) {
                    File dest = destination(root, path);
                    if (Files.deleteIfExists(dest.toPath())) changed = true;
                    for (File dir = dest.getParentFile(); dir != null; dir = dir.getParentFile()) {
                        if (dir.isDirectory()) parents.add(dir);
                        if (dir.equals(root)) break;
                    }
                }
        if (!Arrays.equals(previous, manifest) || changed) {
            // Sync all asset ancestors before publishing, including matching files recovered from
            // an interrupted install. Their renames may not yet have reached durable storage.
            List<File> directories = new ArrayList<>(parents);
            directories.sort(new Comparator<File>() {
                @Override public int compare(File a, File b) {
                    return Integer.compare(b.getPath().length(), a.getPath().length());
                }
            });
            for (File dir : directories) sync.sync(dir);
            replace(stamp, new ByteArrayInputStream(manifest));
            sync.sync(root);
        }
    }
}
