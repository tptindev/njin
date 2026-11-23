package org.njin.renderertest;

import android.os.Bundle;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import java.util.ArrayList;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import org.libsdl.app.SDLActivity;

public final class NjinActivity extends SDLActivity {
    @Override protected void onCreate(Bundle saved) {
        // cgltf uses ordinary file IO; extract this test's assets into its own
        // app storage before the SDL thread starts. No shared storage is used.
        try {
            for (String assetFolder : new String[]{"model-test-assets", "shader-test-assets"}) {
            File directory = new File(getFilesDir(), assetFolder);
            if (!directory.isDirectory() && !directory.mkdirs())
                throw new java.io.IOException("Cannot create test asset directory");
            for (String name : getAssets().list(assetFolder)) {
                try (InputStream in = getAssets().open(assetFolder + "/" + name);
                     FileOutputStream out = new FileOutputStream(new File(directory, name))) {
                    byte[] buffer = new byte[16384];
                    int count;
                    while ((count = in.read(buffer)) != -1) out.write(buffer, 0, count);
                }
            }
            }
        } catch (java.io.IOException error) {
            throw new IllegalStateException("Njin test assets could not be extracted", error);
        }
        super.onCreate(saved);
    }

    @Override protected String[] getArguments() {
        String renderer = getIntent().getStringExtra("renderer");
        if (renderer == null) renderer = "gles";
        ArrayList<String> arguments = new ArrayList<>();
        arguments.add("--renderer=" + renderer);
        arguments.add("--mobile");
        String suite = getIntent().getStringExtra("suite");
        if (suite != null) arguments.add("--suite=" + suite);
        if ("person".equals(suite)) {
            arguments.add("--model=" + new File(getFilesDir(), "model-test-assets/external-animation.glb").getAbsolutePath());
            arguments.add("--quick");
            if (getIntent().getBooleanExtra("ssr", false)) arguments.add("--ssr");
        }
        arguments.add("--frames=" + Math.max(24, Math.min(3600, getIntent().getIntExtra("frames", 24))));
        if (getIntent().getBooleanExtra("lifecycle", false)) arguments.add("--lifecycle");
        if (getIntent().getBooleanExtra("input_smoke", false)) arguments.add("--input-smoke");
        if (getIntent().getBooleanExtra("audio_smoke", false)) arguments.add("--audio-smoke");
        return arguments.toArray(new String[0]);
    }

    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        String orientation = intent.getStringExtra("orientation");
        if ("portrait".equals(orientation)) setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
        if ("landscape".equals(orientation)) setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
    }
}
