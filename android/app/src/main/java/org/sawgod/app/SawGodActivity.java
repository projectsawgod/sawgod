package org.sawgod.app;

import android.os.Bundle;
import android.view.View;
import org.libsdl.app.SDLActivity;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/** SDL owns rendering, gamepad input and the system document picker. No touch overlay. */
public class SawGodActivity extends SDLActivity {
    @Override protected String[] getLibraries() {
        return new String[] { "SDL3", "fluidsynth", "sawgod" };
    }

    @Override protected void onCreate(Bundle state) {
        // Install authored assets before SDL's native main starts. Never replace config, saves or ROM data.
        try {
            installBundledFiles();
        } catch (IOException | NoSuchAlgorithmException error) {
            throw new IllegalStateException("Cannot install SawGod assets", error);
        }
        super.onCreate(state);
        immersive();
    }

    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (focused) immersive();
    }

    @SuppressWarnings("deprecation")
    private void immersive() {
        getWindow().getDecorView().setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_LAYOUT_STABLE |
            View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
    }

    private void installBundledFiles() throws IOException, NoSuchAlgorithmException {
        byte[] manifest;
        try (InputStream in = getAssets().open("bundled-files.txt")) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[8192];
            int count;
            while ((count = in.read(buffer)) != -1) out.write(buffer, 0, count);
            manifest = out.toByteArray();
        }
        byte[] digest = MessageDigest.getInstance("SHA-256").digest(manifest);
        File stamp = new File(getFilesDir(), ".bundled-assets");
        boolean current = stamp.isFile() && java.util.Arrays.equals(digest, Files.readAllBytes(stamp.toPath()));
        String[] rows = new String(manifest, StandardCharsets.UTF_8).split("\n");
        for (String row : rows) {
            String path = row.substring(66);
            File dest = new File(getFilesDir(), path);
            if (current && dest.isFile()) continue;
            File parent = dest.getParentFile();
            if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create " + parent);
            File temp = new File(parent, dest.getName() + ".new");
            try (InputStream in = getAssets().open(path); FileOutputStream out = new FileOutputStream(temp)) {
                byte[] buffer = new byte[65536];
                int count;
                while ((count = in.read(buffer)) != -1) out.write(buffer, 0, count);
                out.getFD().sync();
            }
            if (!temp.renameTo(dest)) throw new IOException("Cannot install " + path);
        }
        Files.write(stamp.toPath(), digest);
    }
}
