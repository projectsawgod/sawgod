package org.sawgod.app;

import java.io.*;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;

/** Run with assertions enabled; optional staged APK assets argument measures real manifest installs. */
public final class BundledAssetsTest {
    private static int opens, syncs;
    private static byte[] bytes(String value) { return value.getBytes(StandardCharsets.UTF_8); }
    private static byte[] manifest(Map<String, byte[]> assets) throws Exception {
        StringBuilder result = new StringBuilder();
        for (Map.Entry<String, byte[]> row : assets.entrySet()) {
            for (byte b : MessageDigest.getInstance("SHA-256").digest(row.getValue()))
                result.append(String.format(Locale.ROOT, "%02x", b & 255));
            result.append("  ").append(row.getKey()).append('\n');
        }
        return bytes(result.toString());
    }
    private static void sync(File dir) throws IOException {
        ++syncs;
        try (FileChannel channel = FileChannel.open(dir.toPath(), StandardOpenOption.READ)) { channel.force(true); }
    }
    private static void install(Path root, Map<String, byte[]> assets) throws Exception {
        opens = syncs = 0;
        BundledAssets.install(root.toFile(), manifest(assets), path -> {
            ++opens;
            return new ByteArrayInputStream(assets.get(path));
        }, BundledAssetsTest::sync);
    }
    private static void regressions(Path root) throws Exception {
        Map<String, byte[]> assets = new LinkedHashMap<>();
        assets.put("text/en.csv", bytes("old"));
        assets.put("gb_maps/nested/map.json", bytes("map"));
        install(root, assets);
        assert opens == 2;
        Path stamp = root.resolve(".bundled-assets");
        assert Arrays.equals(Files.readAllBytes(stamp), manifest(assets));
        install(root, assets);
        assert opens == 0 && syncs == 0 : "unchanged launch must write nothing";
        Files.write(stamp, new byte[32]); // Legacy SHA-256 digest.
        install(root, assets);
        assert opens == 0 : "legacy stamp must hash, not recopy";
        assert syncs >= 4 : "migration must persist recovered file ancestors before the stamp";
        Files.delete(stamp);
        Files.write(root.resolve("text/en.csv"), bytes("wrong"));
        install(root, assets);
        assert opens == 1 : "unknown install must copy only hash mismatches";
        Files.delete(root.resolve("text/en.csv"));
        install(root, assets);
        assert opens == 1 : "missing destination must be repaired";
        Files.write(root.resolve("text/en.csv.new"), bytes("unlisted sibling"));
        assets.put("text/en.csv", bytes("changed"));
        install(root, assets);
        assert opens == 1;
        assert Arrays.equals(Files.readAllBytes(root.resolve("text/en.csv.new")), bytes("unlisted sibling"));
        Files.write(root.resolve("sawgod.toml"), bytes("config"));
        Files.write(root.resolve("text/custom.csv"), bytes("unlisted"));
        assets.remove("gb_maps/nested/map.json");
        install(root, assets);
        assert opens == 0;
        assert !Files.exists(root.resolve("gb_maps/nested/map.json"));
        assert Files.exists(root.resolve("text/custom.csv")) && Files.exists(root.resolve("sawgod.toml"));
        byte[] before = Files.readAllBytes(stamp);
        assets.put("text/en.csv", bytes("next"));
        try {
            BundledAssets.install(root.toFile(), manifest(assets), path -> new InputStream() {
                public int read() throws IOException { throw new IOException("injected copy failure"); }
            }, BundledAssetsTest::sync);
            throw new AssertionError("copy failure ignored");
        } catch (IOException expected) { }
        assert Arrays.equals(before, Files.readAllBytes(stamp));
        assert Arrays.equals(Files.readAllBytes(root.resolve("text/en.csv")), bytes("changed"));
        install(root, assets);
        assert opens == 1;
        assets.put("text/en.csv", bytes("after sync failure"));
        before = Files.readAllBytes(stamp);
        try {
            BundledAssets.install(root.toFile(), manifest(assets), path -> new ByteArrayInputStream(assets.get(path)),
                                  dir -> { throw new IOException("injected directory sync failure"); });
            throw new AssertionError("sync failure ignored");
        } catch (IOException expected) { }
        assert Arrays.equals(before, Files.readAllBytes(stamp));
        install(root, assets);
        assert opens == 1 : "retry after incomplete transaction must not trust new destination";
        for (String path : new String[]{"../sawgod.toml", "text/../sawgod.toml", "saves/slot.sav", "text//file"}) {
            Map<String, byte[]> invalid = Collections.singletonMap(path, bytes("bad"));
            try {
                install(root, invalid);
                throw new AssertionError("unsafe manifest accepted: " + path);
            } catch (IOException expected) { }
        }
        // Invalid installed manifests cannot authorize removal of user files.
        Files.write(stamp, manifest(Collections.singletonMap("sawgod.toml", bytes("config"))));
        install(root, assets);
        assert opens == 0 && Files.exists(root.resolve("sawgod.toml"));
        System.out.println("BundledAssets regressions PASS (migration, delta, missing, prune, failures, path safety)");
    }
    private static void measure(Path root, Path source) throws Exception {
        byte[] manifest = Files.readAllBytes(source.resolve("bundled-files.txt"));
        for (String scenario : new String[]{"fresh install", "unchanged reinstall", "one changed asset", "legacy digest migration", "legacy full recopy baseline"}) {
            if (scenario.equals("one changed asset")) {
                String text = new String(manifest, StandardCharsets.UTF_8);
                // Only the stored hash differs; package payload and 638-file inventory stay real.
                Files.write(root.resolve(".bundled-assets"), bytes(text.replaceFirst("[0-9a-f]{64}", String.join("", Collections.nCopies(64, "0")))));
            }
            if (scenario.equals("legacy digest migration")) Files.write(root.resolve(".bundled-assets"), new byte[32]);
            opens = syncs = 0;
            long start = System.nanoTime();
            if (scenario.equals("legacy full recopy baseline")) {
                for (String row : new String(manifest, StandardCharsets.UTF_8).split("\n")) {
                    String path = row.substring(66);
                    Path dest = root.resolve(path), temp = dest.resolveSibling(dest.getFileName() + ".new");
                    try (InputStream in = Files.newInputStream(source.resolve(path)); FileOutputStream out = new FileOutputStream(temp.toFile())) {
                        ++opens;
                        byte[] buffer = new byte[65536];
                        int count;
                        while ((count = in.read(buffer)) != -1) out.write(buffer, 0, count);
                        out.getFD().sync();
                    }
                    Files.move(temp, dest, StandardCopyOption.REPLACE_EXISTING);
                }
            } else {
                BundledAssets.install(root.toFile(), manifest, path -> {
                    ++opens;
                    return Files.newInputStream(source.resolve(path));
                }, BundledAssetsTest::sync);
            }
            System.out.printf(Locale.ROOT, "%s: %.2f ms, %d asset copies, %d directory syncs%n", scenario,
                              (System.nanoTime() - start) / 1e6, opens, syncs);
        }
    }
    public static void main(String[] args) throws Exception {
        Path scratch = Paths.get(args[0]);
        regressions(Files.createDirectories(scratch.resolve("regressions")));
        if (args.length > 1) measure(Files.createDirectories(scratch.resolve("measure")), Paths.get(args[1]));
    }
}
