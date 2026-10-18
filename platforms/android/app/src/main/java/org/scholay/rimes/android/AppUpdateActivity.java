package org.scholay.rimes.android;

import android.app.Activity;
import android.app.ActivityOptions;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.net.Uri;
import android.os.Bundle;
import android.provider.Settings;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.ScrollView;
import android.view.WindowInsets;
import org.json.JSONObject;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** User-initiated updates. Android owns authorization and APK replacement. */
public final class AppUpdateActivity extends Activity {
    private static final String FEED = "https://github.com/scholay/xsime/releases/latest/download/update-manifest.json";
    private static final String PREFS = "application-update";
    private static final String RESULT = "org.scholay.rimes.android.UPDATE_RESULT";
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private TextView status;
    private ProgressBar progress;
    private Button check, download, install;
    private JSONObject candidate;
    private boolean busy;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        setTitle("检查更新 / Software update");
        LinearLayout body = new LinearLayout(this);
        body.setOrientation(LinearLayout.VERTICAL);
        int padding = (int) (24 * getResources().getDisplayMetrics().density);
        body.setPadding(padding, padding, padding, padding);
        status = new TextView(this); status.setTextSize(16); body.addView(status);
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(100); body.addView(progress);
        check = button(body, "检查更新 / Check for updates", this::check);
        download = button(body, "下载更新 / Download", this::download);
        install = button(body, "安装更新 / Install", this::install);
        ScrollView scroll = new ScrollView(this); scroll.addView(body);
        scroll.setOnApplyWindowInsetsListener((view, insets) -> {
            if (android.os.Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets safe = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                view.setPadding(safe.left, safe.top, safe.right, safe.bottom);
            } else view.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(), insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            return insets;
        });
        setContentView(scroll);
        show("当前版本 / Current: " + BuildConfig.VERSION_NAME);
        restore(); handleResult(getIntent());
    }
    private Button button(LinearLayout body, String title, Runnable action) {
        Button button = new Button(this); button.setText(title);
        button.setOnClickListener(v -> action.run()); body.addView(button); return button;
    }
    private File packageFile() { return new File(getFilesDir(), "updates/release.apk"); }
    private void show(String message) {
        status.setText(message); check.setEnabled(!busy);
        download.setEnabled(!busy && candidate != null);
        install.setEnabled(!busy && candidate != null && packageFile().isFile());
    }
    private void ui(Runnable action) { runOnUiThread(() -> { if (!isDestroyed() && !isFinishing()) action.run(); }); }
    private void run(String message, Task task) {
        if (busy) return;
        busy = true; progress.setProgress(0); show(message);
        worker.execute(() -> {
            try { task.run(); }
            catch (Exception error) { ui(() -> { busy = false; show("更新失败，可重试 / Update failed: " + error.getMessage()); }); }
        });
    }
    private interface Task { void run() throws Exception; }
    private void restore() {
        try {
            String saved = getSharedPreferences(PREFS, MODE_PRIVATE).getString("candidate", "");
            if (!saved.isEmpty()) candidate = new JSONObject(saved);
            long pending = getSharedPreferences(PREFS, MODE_PRIVATE).getLong("pendingCode", 0);
            if (pending > 0 && BuildConfig.VERSION_CODE >= pending) {
                getSharedPreferences(PREFS, MODE_PRIVATE).edit().clear().apply();
                candidate = null;
                // Only this private staged APK is removed; user settings and schemes are untouched.
                if (packageFile().exists()) packageFile().delete();
                show("更新完成 / Updated to " + BuildConfig.VERSION_NAME);
            } else if (candidate != null) {
                show("可更新至 / Available: " + candidate.getString("version") +
                        (pending > 0 ? "\n等待系统安装完成；取消后可重试 / Waiting for installation; retry if cancelled." : ""));
            }
        } catch (Exception error) { candidate = null; show("请重新检查更新 / Check for updates again."); }
    }
    // A completed APK replacement starts a new process and is verified in
    // onCreate. Restoring on every resume would overwrite a cancellation
    // result with the older persisted "waiting for installation" message.
    @Override protected void onNewIntent(Intent intent) { super.onNewIntent(intent); setIntent(intent); handleResult(intent); }
    @Override protected void onDestroy() { worker.shutdownNow(); super.onDestroy(); }
    private void handleResult(Intent intent) {
        if (!RESULT.equals(intent.getAction())) return;
        int code = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
        if (code == PackageInstaller.STATUS_PENDING_USER_ACTION) {
            Intent confirmation = android.os.Build.VERSION.SDK_INT >= 33 ? intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent.class) : intent.getParcelableExtra(Intent.EXTRA_INTENT);
            if (confirmation != null) startActivity(confirmation);
        } else if (code == PackageInstaller.STATUS_SUCCESS) {
            restore(); // A result alone is insufficient: the running build must match.
        } else {
            busy = false; show("安装未完成，可重试 / Installation did not finish (" + code + ").");
        }
        intent.setAction(null);
    }
    private void check() {
        run("正在检查 / Checking…", () -> {
            byte[] data = fetch(FEED, 1024 * 1024);
            JSONObject manifest = new JSONObject(new String(data, StandardCharsets.UTF_8));
            if (manifest.getInt("schemaVersion") != 1) throw new Exception("Unsupported update format");
            String version = manifest.getString("version");
            if (!version.matches("(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)")) throw new Exception("Invalid version");
            JSONObject asset = manifest.getJSONObject("platforms").getJSONObject("android");
            asset.put("version", version);
            validateAsset(asset);
            boolean newer = asset.getLong("build") > BuildConfig.VERSION_CODE;
            if (asset.getInt("minimumOS") > android.os.Build.VERSION.SDK_INT) throw new Exception("This update requires a newer Android version");
            ui(() -> {
                busy = false; candidate = newer ? asset : null;
                getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString("candidate", newer ? asset.toString() : "").apply();
                show(newer ? "可更新至 / Available: " + version : "已是最新版本 / Up to date: " + BuildConfig.VERSION_NAME);
            });
        });
    }
    static void validateAsset(JSONObject asset) throws Exception {
        String version = asset.getString("version");
        if (!version.matches("(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)")) throw new Exception("Invalid version");
        String expected = "https://github.com/scholay/xsime/releases/download/v" + version + "/RIMES-Android-" + version + ".apk";
        if (!expected.equals(asset.getString("url")) || !asset.getString("sha256").matches("[a-f0-9]{64}") ||
                asset.getLong("size") <= 0 || asset.getLong("size") > 512L * 1024 * 1024) throw new Exception("Invalid package metadata");
    }
    private void download() {
        final JSONObject asset = candidate;
        run("正在下载 / Downloading…", () -> {
            validateAsset(asset);
            File file = packageFile(), temporary = new File(file.getPath() + ".partial");
            if (!file.getParentFile().isDirectory() && !file.getParentFile().mkdirs()) throw new Exception("Cannot create update directory");
            HttpURLConnection connection = connect(asset.getString("url"));
            long expected = asset.getLong("size"), total = 0;
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            try (InputStream input = connection.getInputStream(); OutputStream output = new FileOutputStream(temporary)) {
                byte[] buffer = new byte[64 * 1024]; int count, last = -1;
                while ((count = input.read(buffer)) != -1) {
                    if (Thread.currentThread().isInterrupted()) throw new Exception("Cancelled");
                    total += count; if (total > expected) throw new Exception("Package exceeds expected size");
                    output.write(buffer, 0, count); digest.update(buffer, 0, count);
                    int percent = (int) (100 * total / expected);
                    if (percent != last) { last = percent; ui(() -> progress.setProgress(percent)); }
                }
                if (total != expected || !hex(digest.digest()).equals(asset.getString("sha256"))) throw new Exception("Package integrity check failed");
            } catch (Exception error) {
                if (temporary.exists()) temporary.delete();
                throw error;
            } finally { connection.disconnect(); }
            try {
                verifyPackage(temporary, asset);
                if (!temporary.renameTo(file)) throw new Exception("Cannot stage package");
            } finally { if (temporary.exists()) temporary.delete(); }
            ui(() -> { busy = false; show("下载并验证完成，点击安装 / Download verified. Ready to install."); });
        });
    }
    private void verifyPackage(File file, JSONObject asset) throws Exception {
        PackageManager pm = getPackageManager();
        int flags = android.os.Build.VERSION.SDK_INT >= 28 ? PackageManager.GET_SIGNING_CERTIFICATES : PackageManager.GET_SIGNATURES;
        PackageInfo next = pm.getPackageArchiveInfo(file.getPath(), flags);
        PackageInfo current = pm.getPackageInfo(getPackageName(), flags);
        if (next == null || !getPackageName().equals(next.packageName) || versionCode(next) != asset.getLong("build") ||
                versionCode(next) <= versionCode(current) || !asset.getString("version").equals(next.versionName)) throw new Exception("Package identity/version mismatch");
        Signature[] expected = signatures(current), actual = signatures(next);
        if (expected == null || actual == null) throw new Exception("Missing signing identity");
        if (expected.length != actual.length || !Arrays.asList(expected).containsAll(Arrays.asList(actual))) throw new Exception("Package signing key mismatch");
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream input = new FileInputStream(file)) { byte[] buffer = new byte[65536]; int n; while ((n = input.read(buffer)) != -1) digest.update(buffer, 0, n); }
        if (file.length() != asset.getLong("size") || !hex(digest.digest()).equals(asset.getString("sha256"))) throw new Exception("Package changed; download again");
    }
    @SuppressWarnings("deprecation")
    private static long versionCode(PackageInfo info) { return android.os.Build.VERSION.SDK_INT >= 28 ? info.getLongVersionCode() : info.versionCode; }
    @SuppressWarnings("deprecation")
    private static Signature[] signatures(PackageInfo info) {
        return android.os.Build.VERSION.SDK_INT >= 28 ? (info.signingInfo == null ? null : info.signingInfo.getApkContentsSigners()) : info.signatures;
    }
    static PendingIntent installationCallback(Context context, int sessionId) {
        Intent result = new Intent(context, AppUpdateActivity.class).setAction(RESULT).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        ActivityOptions options = ActivityOptions.makeBasic();
        if (android.os.Build.VERSION.SDK_INT >= 35) {
            // Delegate only to Android's installer, and on newer systems only
            // while our update page is visible. Reopening verifies completion.
            options.setPendingIntentCreatorBackgroundActivityStartMode(android.os.Build.VERSION.SDK_INT >= 36
                    ? ActivityOptions.MODE_BACKGROUND_ACTIVITY_START_ALLOW_IF_VISIBLE
                    : ActivityOptions.MODE_BACKGROUND_ACTIVITY_START_ALLOWED);
        }
        return PendingIntent.getActivity(context, sessionId, result,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_MUTABLE, options.toBundle());
    }
    private void install() {
        if (!getPackageManager().canRequestPackageInstalls()) {
            show("请允许此应用安装更新，然后返回点击安装 / Allow updates from this app, then return and tap Install.");
            startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + getPackageName()))); return;
        }
        final JSONObject asset = candidate;
        run("正在验证并准备安装 / Preparing installation…", () -> {
            verifyPackage(packageFile(), asset);
            PackageInstaller installer = getPackageManager().getPackageInstaller();
            PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
            params.setAppPackageName(getPackageName()); params.setSize(packageFile().length());
            if (android.os.Build.VERSION.SDK_INT >= 31) params.setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_REQUIRED);
            int id = installer.createSession(params);
            try (PackageInstaller.Session session = installer.openSession(id); InputStream input = new FileInputStream(packageFile())) {
                try (OutputStream output = session.openWrite("base.apk", 0, packageFile().length())) {
                    byte[] buffer = new byte[65536]; int n; while ((n = input.read(buffer)) != -1) output.write(buffer, 0, n); session.fsync(output);
                }
                getSharedPreferences(PREFS, MODE_PRIVATE).edit().putLong("pendingCode", asset.getLong("build")).commit();
                session.commit(installationCallback(this, id).getIntentSender());
            } catch (Exception error) { installer.abandonSession(id); throw error; }
            ui(() -> { busy = false; show("请在系统安装器确认。更新完成后重新打开应用 / Confirm in Android, then reopen the app."); });
        });
    }
    private static String hex(byte[] bytes) { StringBuilder text = new StringBuilder(); for (byte b : bytes) text.append(String.format(java.util.Locale.ROOT, "%02x", b & 255)); return text.toString(); }
    private static HttpURLConnection connect(String address) throws Exception {
        for (int redirects = 0; redirects < 6; redirects++) {
            URL url = new URL(address); String host = url.getHost();
            if (!"https".equals(url.getProtocol()) || url.getUserInfo() != null || (url.getPort() != -1 && url.getPort() != 443) ||
                    !(host.equals("github.com") || host.endsWith(".githubusercontent.com"))) throw new Exception("Untrusted update URL");
            HttpURLConnection connection = (HttpURLConnection) url.openConnection();
            connection.setConnectTimeout(20000); connection.setReadTimeout(30000); connection.setInstanceFollowRedirects(false);
            connection.setRequestProperty("User-Agent", "XSIME-updater/1");
            int status = connection.getResponseCode();
            if (status >= 300 && status < 400) { String location = connection.getHeaderField("Location"); connection.disconnect(); address = new URL(url, location).toString(); continue; }
            if (status != 200) { connection.disconnect(); throw new Exception("HTTP " + status); }
            return connection;
        }
        throw new Exception("Too many redirects");
    }
    private static byte[] fetch(String url, int limit) throws Exception {
        HttpURLConnection connection = connect(url);
        try (InputStream input = connection.getInputStream(); ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192]; int n;
            while ((n = input.read(buffer)) != -1) { if (output.size() + n > limit) throw new Exception("Response too large"); output.write(buffer, 0, n); }
            return output.toByteArray();
        } finally { connection.disconnect(); }
    }
}
