package org.scholay.rimes.android;

import android.app.*;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.view.View;
import android.widget.*;
import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import org.json.*;
import org.scholay.rimes.core.RimeSchemeArchive;

/** Runs in :scheme_import so compiling a large dictionary cannot retire live IME sessions. */
public final class RimeSchemesActivity extends Activity {
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private LinearLayout content;
    private RimeSchemeStore store;
    private boolean busy,destroyed;
    private String message="";
    @Override public void onCreate(Bundle state) {super.onCreate(state);store=new RimeSchemeStore(this);render();}
    @Override protected void onDestroy() {destroyed=true;worker.shutdown();super.onDestroy();}
    private void render() {
        if(destroyed) return;
        ScrollView scroll=new ScrollView(this);content=new LinearLayout(this);content.setOrientation(LinearLayout.VERTICAL);
        int pad=(int)(20*getResources().getDisplayMetrics().density);content.setPadding(pad,pad,pad,pad);scroll.addView(content);setContentView(scroll);
        label("Rime 方案包",24);label("支持上游 ZIP，以及每行“词条⇥编码⇥可选权重”的 UTF-8 TXT / TSV。编译成功后再选择启用。",15);
        button("返回设置",() -> finish());
        RimeSchemeStore.Selection active=store.active();label("当前："+(active==null?"内置方案":active.name),17);
        if(active!=null) button("停用导入方案，恢复内置",() -> action(() -> store.activate(null,null),"已恢复内置方案，请重新唤起键盘。"));
        button(busy?"正在处理，请保持页面打开…":"导入方案或文本词库",() -> {
            Intent picker=new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE);
            picker.putExtra(Intent.EXTRA_MIME_TYPES,new String[]{"application/zip","application/x-zip-compressed","application/octet-stream","text/plain","text/tab-separated-values"});startActivityForResult(picker,1);
        });
        if(!message.isEmpty()) label(message,15);
        for(JSONObject item:store.packages()) {
            String id=item.optString("id");label(item.optString("name"),20);JSONArray schemas=item.optJSONArray("schemas");
            if(schemas!=null) for(int i=0;i<schemas.length();i++) {
                JSONObject schema=schemas.optJSONObject(i);if(schema==null) continue;
                String schemaID=schema.optString("id"),name=schema.optString("name");
                button((active!=null && active.packageID.equals(id) && active.schemaID.equals(schemaID)?"✓ ":"启用 ")+name,
                    () -> action(() -> store.activate(id,schemaID),"已启用“"+name+"”，下次唤起键盘生效。"));
            }
            button("删除方案包",() -> new AlertDialog.Builder(this).setTitle("删除“"+item.optString("name")+"”？")
                .setMessage("移除源文件和编译缓存，保留已学习的用户词库；正在启用的方案会恢复为内置方案。")
                .setNegativeButton("取消",null).setPositiveButton("删除",(dialog,which) -> action(() -> store.remove(id),"已删除方案包")).show());
        }
    }
    private void label(String text,int size) {TextView view=new TextView(this);view.setText(text);view.setTextSize(size);view.setPadding(0,12,0,12);content.addView(view);}
    private void button(String title,Runnable action) {Button button=new Button(this);button.setText(title);button.setEnabled(!busy);button.setOnClickListener(v -> action.run());content.addView(button);}
    private interface Operation {void run() throws Exception;}
    private void action(Operation operation,String success) {try{operation.run();message=success;render();}catch(Exception e){error(e);}}
    private void error(Throwable failure) {message=failure.getMessage()==null?"操作未完成":failure.getMessage();render();}
    @Override protected void onActivityResult(int request,int result,Intent data) {
        super.onActivityResult(request,result,data);if(request!=1 || result!=RESULT_OK || data==null || data.getData()==null) return;
        Uri uri=data.getData();busy=true;message="正在检查 ZIP…";render();
        worker.execute(() -> {
            File archive=null;
            try {
                String title="导入方案";
                try(android.database.Cursor cursor=getContentResolver().query(uri,new String[]{OpenableColumns.DISPLAY_NAME},null,null,null)) {
                    if(cursor!=null && cursor.moveToFirst()) title=cursor.getString(0);
                }
                if(title==null) title="导入方案";title=title.substring(0,Math.min(120,title.length()));
                archive=File.createTempFile("scheme-",".zip",getCacheDir());
                try(InputStream in=getContentResolver().openInputStream(uri);OutputStream out=new FileOutputStream(archive)) {
                    if(in==null) throw new IOException("无法打开所选 ZIP");long size=0;byte[] block=new byte[32768];int n;
                    while((n=in.read(block))!=-1) {size+=n;if(size>RimeSchemeArchive.MAX_ARCHIVE) throw new IOException("ZIP 超过 256 MB");out.write(block,0,n);}
                }
                RimeSchemeArchive.Review review;
                if(title.toLowerCase(java.util.Locale.ROOT).endsWith(".txt") || title.toLowerCase(java.util.Locale.ROOT).endsWith(".tsv")) {
                    File generated=File.createTempFile("text-scheme-",".zip",getCacheDir());
                    try {review=RimeSchemeArchive.inspectTextDictionary(archive,title,generated);}
                    catch(Exception error) {generated.delete();throw error;}
                    archive.delete();archive=generated;
                } else review=RimeSchemeArchive.inspect(archive);
                String name=title;File stagedArchive=archive;
                runOnUiThread(() -> {busy=false;if(destroyed){stagedArchive.delete();return;}review(review,name);});
            } catch(Exception | LinkageError e) {if(archive!=null) archive.delete();runOnUiThread(() -> {busy=false;error(e);});}
        });
    }
    private void review(RimeSchemeArchive.Review review,String title) {
        render();boolean[] chosen=new boolean[review.schemes.size()];String[] titles=new String[chosen.length];
        for(String warning:review.warnings) label(warning,14);
        for(int i=0;i<chosen.length;i++) {RimeSchemeArchive.Scheme s=review.schemes.get(i);titles[i]=s.name+" · "+s.id;chosen[i]=s.recommended;}
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle("选择需要的输入方案")
            .setMultiChoiceItems(titles,chosen,(d,which,value) -> chosen[which]=value).setNegativeButton("取消",(d,w) -> review.archive.delete())
            .setPositiveButton("编译并安装",null).create();
        dialog.setOnCancelListener(d -> review.archive.delete());dialog.setOnShowListener(d -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
            List<String> selected=new ArrayList<>();for(int i=0;i<chosen.length;i++) if(chosen[i]) selected.add(review.schemes.get(i).id);
            try {review.deploymentOrder(selected);}catch(Exception e){Toast.makeText(this,e.getMessage(),Toast.LENGTH_LONG).show();return;}
            dialog.dismiss();busy=true;message="正在编译词典与输入方案，大词库首次部署可能需要几分钟…";render();
            getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            worker.execute(() -> {
                try {store.install(review,title,selected);runOnUiThread(() -> {message="安装完成。选择上方的方案启用；当前输入方式尚未改变。";done();});}
                catch(Exception | LinkageError e) {runOnUiThread(() -> {busy=false;error(e);done();});}
                finally {review.archive.delete();}
            });
        }));dialog.show();
    }
    private void done() {busy=false;getWindow().clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);render();}
}
