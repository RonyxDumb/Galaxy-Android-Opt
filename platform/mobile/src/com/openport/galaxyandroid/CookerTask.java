package com.openport.galaxyandroid;

import android.app.Activity;
import android.net.Uri;
import android.os.Handler;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicReference;

public final class CookerTask {
    private static final String TAG = "CookerTask";

    public interface CookerTaskListener {
        void onLog(String line);
        void onProgress(int percent, String message);
        void onCompleted(boolean success, String message);
    }

    private CookerTask() {}

    public static void start(Activity activity, Handler handler, String selectedPath, Uri selectedUri,
                             String selectedDisplayName, boolean includeMovies, CookerTaskListener listener) {
        new Thread(() -> {
            File cachedDisc = null;
            File extractedDisc = null;
            boolean success = false;
            String resultMessage = "Installazione non completata.";
            AtomicReference<String> lastError = new AtomicReference<>();
            CookerEngine.CookerCallback callback = new CookerEngine.CookerCallback() {
                @Override public void onLog(String line) {
                    String detail = line == null ? "" : line.trim();
                    if (detail.startsWith("[ERROR]") || detail.startsWith("[Error]")
                            || detail.startsWith("[Python Error]") || detail.startsWith("[Cook Warning]")
                            || detail.contains("Error:")) {
                        // Keep the first concrete failure; the JNI wrapper prints
                        // its generic return-code exception afterwards.
                        lastError.updateAndGet(previous -> previous == null || isGenericError(previous)
                                ? detail : previous);
                    }
                    if (listener != null) listener.onLog(line);
                }
                @Override public void onProgress(int percent, String message) {
                    if (listener != null) listener.onProgress(percent, message);
                }
                // The synchronous JNI methods return their result; they do not send this callback.
                @Override public void onFinished(boolean finished, String message) {}
            };
            try {
                File appStorage = activity.getExternalFilesDir(null);
                if (appStorage == null) appStorage = activity.getFilesDir();
                File outputDir = new File(appStorage, "GalaxyData");
                if (!outputDir.isDirectory() && !outputDir.mkdirs()) {
                    throw new IOException("Impossibile creare la cartella del gioco.");
                }
                File pending = new File(outputDir, ".installing");
                if (!pending.exists() && !pending.createNewFile()) {
                    throw new IOException("Impossibile iniziare l’installazione.");
                }
                new File(outputDir, ".installed_ok").delete();
                callback.onLog("[Storage] Target installation directory: " + outputDir.getAbsolutePath());

                String name = selectedDisplayName == null ? "" : selectedDisplayName;
                if (name.isEmpty()) name = selectedPath == null ? "" : selectedPath;
                String lower = name.toLowerCase(Locale.ROOT);
                if (!(lower.endsWith(".rvz") || lower.endsWith(".wbfs")
                        || lower.endsWith(".iso") || lower.endsWith(".gcm"))) {
                    throw new IOException("Seleziona una ROM .iso, .rvz, .wbfs o .gcm.");
                }

                File disc = selectedPath == null ? null : new File(selectedPath);
                if (disc == null || !disc.isFile()) {
                    if (selectedUri == null) throw new IOException("La ROM selezionata non è accessibile.");
                    String extension = lower.endsWith(".rvz") ? "rvz" : lower.endsWith(".wbfs") ? "wbfs" : "iso";
                    cachedDisc = new File(activity.getFilesDir(), "temp_disc." + extension);
                    callback.onProgress(-1, "Lettura della ROM…");
                    try (InputStream in = activity.getContentResolver().openInputStream(selectedUri);
                         FileOutputStream out = new FileOutputStream(cachedDisc)) {
                        if (in == null) throw new IOException("Impossibile leggere la ROM selezionata.");
                        byte[] buffer = new byte[1024 * 1024];
                        int count;
                        long total = 0, reported = 0;
                        while ((count = in.read(buffer)) != -1) {
                            out.write(buffer, 0, count);
                            total += count;
                            if (total - reported >= 50L * 1024 * 1024) {
                                reported = total;
                                callback.onProgress(-1, "ROM letta: " + total / (1024 * 1024) + " MB");
                            }
                        }
                    }
                    disc = cachedDisc;
                }

                extractedDisc = new File(activity.getFilesDir(), "temp_extracted");
                CookerEngine.deleteRecursive(extractedDisc);
                if (!extractedDisc.mkdirs()) throw new IOException("Impossibile preparare la cartella temporanea.");
                callback.onProgress(-1, "Estrazione della ROM…");
                boolean extracted = false;
                try {
                    extracted = CookerEngine.extractDisc(disc.getAbsolutePath(),
                            extractedDisc.getAbsolutePath(), includeMovies, callback) == 0;
                } catch (LinkageError e) {
                    callback.onLog("[Installer] Native extractor unavailable: " + e.getMessage());
                }
                if (!extracted) {
                    callback.onLog("[Installer] Trying fallback disc extractor...");
                    extracted = CookerEngine.extractWithNodtool(activity, disc.getAbsolutePath(),
                            extractedDisc.getAbsolutePath(), includeMovies, callback);
                }
                if (!extracted) throw new IOException(errorOr(lastError, "Estrazione della ROM non riuscita."));
                if (cachedDisc != null) {
                    cachedDisc.delete();
                    cachedDisc = null;
                }

                lastError.set(null);
                callback.onProgress(-1, "Preparazione del convertitore…");
                CookerEngine.prepareAssets(activity, callback);
                callback.onProgress(-1, "Preparazione dei dati del gioco…");
                File filesDir = activity.getFilesDir();
                int result = CookerEngine.runCooker(
                        new File(filesDir, "pythonhome").getAbsolutePath(),
                        new File(filesDir, "scripts").getAbsolutePath(),
                        extractedDisc.getAbsolutePath(), outputDir.getAbsolutePath(),
                        includeMovies, callback);
                if (result != 0) {
                    throw new IOException(errorOr(lastError, "Il convertitore ha restituito il codice " + result + "."));
                }
                if (!GameDataLocator.hasConvertedAssets(outputDir)) {
                    throw new IOException("I dati convertiti sono incompleti o non appartengono a Super Mario Galaxy.");
                }
                if (!pending.delete()) throw new IOException("Impossibile completare l’installazione.");
                success = true;
                resultMessage = "Gioco pronto.";
            } catch (Exception | LinkageError error) {
                Log.e(TAG, "Installation failed", error);
                resultMessage = error.getMessage() == null ? error.toString() : error.getMessage();
                callback.onLog("[ERROR] " + resultMessage);
            } finally {
                if (cachedDisc != null) CookerEngine.deleteRecursive(cachedDisc);
                if (extractedDisc != null) CookerEngine.deleteRecursive(extractedDisc);
                final boolean completed = success;
                final String message = resultMessage;
                handler.post(() -> {
                    if (listener != null) listener.onCompleted(completed, message);
                });
            }
        }, "GalaxyInstall").start();
    }

    private static String errorOr(AtomicReference<String> error, String fallback) {
        String detail = error.get();
        return detail == null || detail.isEmpty() ? fallback : detail;
    }

    private static boolean isGenericError(String error) {
        return error.contains("cook.convert returned non-zero code");
    }
}
