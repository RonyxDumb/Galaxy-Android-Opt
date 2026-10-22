package com.openport.galaxyandroid;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.widget.Toast;
import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;

/** Recovery is shown only after a recorded native crash, never as a settings panel. */
final class CrashReport {
    static final int EXPORT = 1002;

    static boolean prompt(Activity activity, Runnable retry) {
        File report = new File(activity.getFilesDir(), "galaxy_crash.log");
        if (report.length() == 0) return false;
        new AlertDialog.Builder(activity)
                .setTitle(R.string.crash_title)
                .setMessage(R.string.crash_message)
                .setCancelable(false)
                .setPositiveButton(R.string.crash_retry, (dialog, which) -> {
                    File previous = new File(activity.getFilesDir(), "galaxy_crash.log.prev");
                    if (!report.renameTo(previous) && !report.delete()) {
                        Toast.makeText(activity, R.string.crash_export_error, Toast.LENGTH_LONG).show();
                        return;
                    }
                    retry.run();
                })
                .setNegativeButton(R.string.crash_export, (dialog, which) -> {
                    Intent save = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                    save.addCategory(Intent.CATEGORY_OPENABLE);
                    save.setType("text/plain");
                    save.putExtra(Intent.EXTRA_TITLE, "SuperMarioGalaxy-crash.txt");
                    activity.startActivityForResult(save, EXPORT);
                }).show();
        return true;
    }

    private static String tail(File file, int maxBytes) throws IOException {
        if (!file.isFile()) return "(non disponibile)\n";
        try (RandomAccessFile input = new RandomAccessFile(file, "r")) {
            int count = (int)Math.min(input.length(), maxBytes);
            input.seek(input.length() - count);
            byte[] bytes = new byte[count];
            input.readFully(bytes);
            return new String(bytes, StandardCharsets.UTF_8);
        }
    }

    static void export(Activity activity, Uri destination) {
        try {
            String version = activity.getPackageManager().getPackageInfo(activity.getPackageName(), 0).versionName;
            String text = "Super Mario Galaxy " + version + "\n"
                    + Build.MANUFACTURER + " " + Build.MODEL + "; Android " + Build.VERSION.RELEASE + "\n\n"
                    + tail(new File(activity.getFilesDir(), "galaxy_crash.log"), 4096)
                    + "\nUltime righe del gioco:\n"
                    + tail(new File(activity.getFilesDir(), "galaxy_log.txt"), 128 * 1024);
            try (OutputStream output = activity.getContentResolver().openOutputStream(destination, "wt")) {
                if (output == null) throw new IOException("Documento non disponibile");
                output.write(text.getBytes(StandardCharsets.UTF_8));
            }
            Toast.makeText(activity, R.string.crash_export_done, Toast.LENGTH_LONG).show();
        } catch (Exception error) {
            android.util.Log.e("GalaxyCrash", "Cannot export report", error);
            Toast.makeText(activity, R.string.crash_export_error, Toast.LENGTH_LONG).show();
        }
    }
}
