package org.sawgod.app;

import android.os.Bundle;
import android.view.View;
import org.libsdl.app.SDLActivity;
import java.io.*;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import java.security.NoSuchAlgorithmException;

/** SDL owns rendering, touch/gamepad input and the system document picker. */
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
        BundledAssets.install(getFilesDir(), manifest, new BundledAssets.Source() {
            @Override public InputStream open(String path) throws IOException {
                return getAssets().open(path);
            }
        }, new BundledAssets.DirectorySync() {
            @Override public void sync(File directory) throws IOException {
                try {
                    FileDescriptor fd = Os.open(directory.getPath(), OsConstants.O_RDONLY, 0);
                    try { Os.fsync(fd); }
                    finally { Os.close(fd); }
                } catch (ErrnoException error) {
                    throw new IOException("Cannot sync bundled asset directory", error);
                }
            }
        });
    }
}
