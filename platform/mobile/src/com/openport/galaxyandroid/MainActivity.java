package com.openport.galaxyandroid;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.ProgressBar;
import android.widget.TextView;
import java.io.File;
import java.util.Locale;

/** Install the user's disc, then open the game. */
public class MainActivity extends Activity {
    private static final int PICK_ROM = 1001;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private TextView status;
    private Button extract;
    private ProgressBar progress;
    private boolean installing;
    private boolean launching;

    public static boolean checkDataDir(File dir) { return GameDataLocator.checkDataDir(dir); }
    public static File findGameDataFolder(Context context) { return GameDataLocator.findGameDataFolder(context); }
    public static boolean isGameDataReady() { return GameDataLocator.isGameDataReady(); }
    public static boolean isGameDataReady(Context context) { return GameDataLocator.isGameDataReady(context); }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        setContentView(R.layout.activity_main);
        status = findViewById(R.id.txtProgressStatus);
        extract = findViewById(R.id.btnSelectDisc);
        progress = findViewById(R.id.progressBar);
        extract.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivityForResult(intent, PICK_ROM);
        });
        if (isGameDataReady(this) && !getIntent().getBooleanExtra("require_rom", false)) launchGame();
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_ROM || result != RESULT_OK || data == null || data.getData() == null || installing) return;
        Uri uri = data.getData();
        String name = StorageUtils.getDisplayName(this, uri);
        if (name == null) name = "";
        String lower = name.toLowerCase(Locale.ROOT);
        if (!(lower.endsWith(".iso") || lower.endsWith(".rvz") || lower.endsWith(".wbfs") || lower.endsWith(".gcm"))) {
            status.setText(R.string.invalid_rom);
            return;
        }
        installing = true;
        extract.setEnabled(false);
        status.setText(R.string.status_cooking);
        progress.setVisibility(View.VISIBLE);
        progress.setIndeterminate(true);
        // Use the document grant instead of asking for broad file access.
        CookerTask.start(this, handler, uri.toString(), uri, name, true, new CookerTask.CookerTaskListener() {
            @Override public void onLog(String line) { Log.i("GalaxyInstall", line); }
            @Override public void onProgress(int percent, String message) {
                handler.post(() -> {
                    progress.setIndeterminate(percent < 0);
                    if (percent >= 0) progress.setProgress(percent);
                });
            }
            @Override public void onCompleted(boolean success, String message) {
                handler.post(() -> {
                    installing = false;
                    extract.setEnabled(true);
                    progress.setVisibility(View.GONE);
                    if (success && isGameDataReady(MainActivity.this)) launchGame();
                    else {
                        status.setText(R.string.status_error);
                        Log.e("GalaxyInstall", message == null ? "Installation failed" : message);
                    }
                });
            }
        });
    }

    private void launchGame() {
        if (launching) return;
        launching = true;
        CookerEngine.extractShaderManifestIfPresent(this);
        startActivity(new Intent(this, GameActivity.class));
        finish();
    }
}
