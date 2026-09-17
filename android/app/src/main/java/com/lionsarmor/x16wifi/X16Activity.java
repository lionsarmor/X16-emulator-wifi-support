package com.lionsarmor.x16wifi;

import android.os.Bundle;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Arrays;

/**
 * Boots the X16 emulator core (libmain.so, this fork's build of x16emu with
 * the WiFi/ESP32 card) inside SDL2's standard Android activity.
 *
 * x16emu expects to open real files by path (the ROM, its SD-card/fsroot
 * folder), which doesn't map onto an APK's assets directly. So before SDL
 * hands off to native code, this copies what it needs out of assets/ into
 * the app's private files directory every launch (cheap - a few hundred KB
 * at most - and guarantees what runs always matches what's actually in the
 * current APK, rather than stale files left over from a previous install),
 * then tells x16emu where to find them via the same command-line flags
 * you'd use on desktop.
 *
 * A bundled app's own files (its .PRG, overlay .BIN files, assets) live
 * under assets/app/ and get extracted into the emulated SD card root. Which
 * .PRG to boot into comes from assets/launcher.cfg (a single "PRG=NAME.PRG"
 * line), the same convention this fork's tools/bundle-x16-app.sh uses for
 * the Windows launcher. If there's no assets/app/ at all, this just boots
 * to bare Commander BASIC with an empty SD card, same as before.
 */
public class X16Activity extends SDLActivity {
    private static final String TAG = "X16Activity";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        prepareFiles();
        super.onCreate(savedInstanceState);
    }

    private void prepareFiles() {
        File filesDir = getFilesDir();
        copyAssetFile("rom.bin", new File(filesDir, "rom.bin"));

        // The X16's "SD card" / host filesystem root. Rebuilt fresh every
        // launch from assets/app/, if present, so it never goes stale.
        File sdRoot = new File(filesDir, "sdroot");
        deleteRecursive(sdRoot);
        sdRoot.mkdirs();
        copyAssetTree("app", sdRoot);
    }

    private void deleteRecursive(File file) {
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) {
                for (File child : children) {
                    deleteRecursive(child);
                }
            }
        }
        file.delete();
    }

    private void copyAssetFile(String assetPath, File destination) {
        try (InputStream in = getAssets().open(assetPath);
             OutputStream out = new FileOutputStream(destination)) {
            byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
        } catch (IOException e) {
            Log.e(TAG, "Failed to copy asset " + assetPath + " to " + destination, e);
        }
    }

    /**
     * Recursively copies an assets/ subtree onto the filesystem. Android's
     * AssetManager has no direct "is this a file or a directory" check, so
     * this uses the standard trick: list() returns the names inside a real
     * directory, and returns null/empty for a file (or for a genuinely
     * empty directory, which is harmless to skip - there's nothing in it
     * either way).
     */
    private void copyAssetTree(String assetPath, File destDir) {
        String[] children = null;
        try {
            children = getAssets().list(assetPath);
        } catch (IOException e) {
            // Not a directory this APK has - fine, e.g. no bundled app.
        }

        if (children == null || children.length == 0) {
            return;
        }

        destDir.mkdirs();
        for (String child : children) {
            String childAssetPath = assetPath.isEmpty() ? child : assetPath + "/" + child;
            String[] grandchildren = null;
            try {
                grandchildren = getAssets().list(childAssetPath);
            } catch (IOException e) {
                // Treat as a file below.
            }

            if (grandchildren != null && grandchildren.length > 0) {
                copyAssetTree(childAssetPath, new File(destDir, child));
            } else {
                copyAssetFile(childAssetPath, new File(destDir, child));
            }
        }
    }

    /** Reads the one "KEY=value" line this needs out of assets/launcher.cfg. */
    private String readLauncherConfig(String key) {
        try (InputStream in = getAssets().open("launcher.cfg")) {
            byte[] data = new byte[4096];
            int len = in.read(data);
            if (len <= 0) {
                return null;
            }
            String contents = new String(data, 0, len, "UTF-8");
            for (String line : contents.split("\n")) {
                line = line.trim();
                if (line.startsWith(key + "=")) {
                    return line.substring(key.length() + 1).trim();
                }
            }
        } catch (IOException e) {
            // No launcher.cfg bundled - fine, means no specific PRG to boot.
        }
        return null;
    }

    @Override
    protected String[] getArguments() {
        File filesDir = getFilesDir();
        String romPath = new File(filesDir, "rom.bin").getAbsolutePath();
        String sdRootPath = new File(filesDir, "sdroot").getAbsolutePath();

        String prg = readLauncherConfig("PRG");

        String[] baseArgs = {
            "-rom", romPath,
            "-wifi",
            "-fsroot", sdRootPath,
            "-startin", sdRootPath,
            "-scale", "2",
            // The activity is locked to landscape (see AndroidManifest.xml)
            // to match the X16's native display and avoid the touch/mouse
            // coordinate corruption that came from fighting a portrait
            // default on startup. -widescreen (an existing, already-tested
            // flag - see main.c) stretches the emulated display from 4:3 to
            // 16:9, which is much closer to a real phone/tablet's landscape
            // aspect than plain 4:3, so it fills far more of the screen -
            // real content like DESK COMMANDER's chat and screensaver get
            // meaningfully more room to work with instead of a narrow
            // letterboxed strip.
            "-widescreen",
        };

        if (prg == null || prg.isEmpty()) {
            return baseArgs;
        }

        String prgPath = new File(sdRootPath, prg).getAbsolutePath();
        String[] withPrg = Arrays.copyOf(baseArgs, baseArgs.length + 4);
        withPrg[baseArgs.length] = "-prg";
        withPrg[baseArgs.length + 1] = prgPath;
        withPrg[baseArgs.length + 2] = "-run";
        withPrg[baseArgs.length + 3] = "-rtc";
        return withPrg;
    }
}
