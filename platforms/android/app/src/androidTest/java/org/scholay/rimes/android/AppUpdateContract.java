package org.scholay.rimes.android;
import org.json.JSONObject;
final class AppUpdateContract {
    static void run() throws Exception {
        JSONObject asset = new JSONObject().put("version", "1.2.3").put("url", "https://github.com/scholay/xsime/releases/download/v1.2.3/RIMES-Android-1.2.3.apk").put("sha256", "a".repeat(64)).put("size", 100);
        AppUpdateActivity.validateAsset(asset);
        for (String url : new String[]{"http://github.com/scholay/xsime/releases/download/v1.2.3/RIMES-Android-1.2.3.apk", "https://github.com/attacker/xsime/releases/download/v1.2.3/RIMES-Android-1.2.3.apk", "https://github.com/scholay/xsime/releases/download/v1.2.2/RIMES-Android-1.2.2.apk"}) {
            JSONObject wrong = new JSONObject(asset.toString()).put("url", url);
            boolean rejected = false; try { AppUpdateActivity.validateAsset(wrong); } catch (Exception expected) { rejected = true; }
            if (!rejected) throw new AssertionError("Untrusted update accepted");
        }
        for (long size : new long[]{0, -1, 1024L * 1024 * 1024}) {
            boolean rejected = false; try { AppUpdateActivity.validateAsset(new JSONObject(asset.toString()).put("size", size)); } catch (Exception expected) { rejected = true; }
            if (!rejected) throw new AssertionError("Unsafe package size accepted");
        }
    }
}
