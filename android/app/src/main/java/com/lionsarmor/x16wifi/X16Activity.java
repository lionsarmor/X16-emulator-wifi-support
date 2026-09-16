package com.lionsarmor.x16wifi;

import android.os.Bundle;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Boots the X16 emulator core (libmain.so, this fork's build of x16emu with
 * the WiFi/ESP32 card) inside SDL2's standard Android activity.
 *
 * x16emu expects to open real files by path (the ROM, its SD-card/fsroot
 * folder), which doesn't map onto an APK's assets directly. So before SDL
 * hands off to native code, this copies what it needs out of assets/ into
 * the app's private files directory once, then tells x16emu where to find
 * them via the same command-line flags you'd use on desktop.
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
        File romFile = new File(filesDir, "rom.bin");
        if (!romFile.exists()) {
            copyAsset("rom.bin", romFile);
        }

        // The X16's "SD card" / host filesystem root. Starts empty; anything
        // dropped into it (e.g. adb push, or a future in-app file picker)
        // becomes visible to the emulated machine via DOS"CD:/", LOAD, etc.
        File sdRoot = new File(filesDir, "sdroot");
        if (!sdRoot.exists()) {
            sdRoot.mkdirs();
        }
    }

    private void copyAsset(String assetName, File destination) {
        try (InputStream in = getAssets().open(assetName);
             OutputStream out = new FileOutputStream(destination)) {
            byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
        } catch (Exception e) {
            Log.e(TAG, "Failed to copy asset " + assetName + " to " + destination, e);
        }
    }

    @Override
    protected String[] getArguments() {
        File filesDir = getFilesDir();
        String romPath = new File(filesDir, "rom.bin").getAbsolutePath();
        String sdRootPath = new File(filesDir, "sdroot").getAbsolutePath();

        return new String[] {
            "-rom", romPath,
            "-wifi",
            "-fsroot", sdRootPath,
            "-scale", "2",
        };
    }
}
