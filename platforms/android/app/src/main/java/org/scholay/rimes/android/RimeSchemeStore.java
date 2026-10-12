package org.scholay.rimes.android;

import android.content.Context;
import android.util.AtomicFile;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import org.json.*;
import org.scholay.rimes.core.RimeSchemeArchive;

/** Published source/compiled packages are immutable. Learning data has a separate lifetime. */
final class RimeSchemeStore {
    final File root;
    RimeSchemeStore(Context context) {this(new File(context.getNoBackupFilesDir(),"rime-schemes"));}
    RimeSchemeStore(File root) {this.root=root;}
    static final class Selection {
        final String packageID,schemaID,name;
        final File resources;
        Selection(String packageID,String schemaID,String name,File resources) {
            this.packageID=packageID;this.schemaID=schemaID;this.name=name;this.resources=resources;
        }
    }
    List<JSONObject> packages() {
        List<JSONObject> packages=new ArrayList<>();File[] paths=new File(root,"packages").listFiles();
        if(paths!=null) for(File path:paths) {
            if(!uuid(path.getName()) || Files.isSymbolicLink(path.toPath())) continue;
            try {JSONObject item=read(new File(path,"package.json"));if(item.getString("id").equals(path.getName())) packages.add(item);} catch(Exception ignored) {}
        }
        packages.sort(Comparator.comparing(x -> x.optString("importedAt")));return packages;
    }
    Selection active() {
        try {
            JSONObject selected=new JSONObject(new String(new AtomicFile(new File(root,"active.json")).readFully(),StandardCharsets.UTF_8));
            String id=selected.getString("package"),schema=selected.getString("schema");
            if(!uuid(id) || !RimeSchemeArchive.identifier(schema)) return null;
            File resources=new File(root,"packages/"+id);
            if(Files.isSymbolicLink(resources.toPath())) return null;
            JSONObject metadata=read(new File(resources,"package.json"));JSONArray schemas=metadata.getJSONArray("schemas");
            for(int i=0;i<schemas.length();i++) if(schema.equals(schemas.getJSONObject(i).getString("id")) && new File(resources,"build/"+schema+".schema.yaml").isFile())
                return new Selection(id,schema,schemas.getJSONObject(i).getString("name"),resources);
        } catch(Exception ignored) {}
        return null;
    }
    void activate(String packageID,String schemaID) throws Exception {
        JSONObject next=new JSONObject();
        if(packageID!=null) {
            if(!uuid(packageID) || !RimeSchemeArchive.identifier(schemaID)) throw new IOException("方案标识无效");
            File path=new File(root,"packages/"+packageID);
            JSONArray schemas=read(new File(path,"package.json")).getJSONArray("schemas");boolean present=false;
            for(int i=0;i<schemas.length();i++) present |= schemaID.equals(schemas.getJSONObject(i).getString("id"));
            if(!present || !new File(path,"build/"+schemaID+".schema.yaml").isFile()) throw new IOException("方案部署文件缺失，请重新导入");
            next.put("package",packageID).put("schema",schemaID);
        }
        Files.createDirectories(root.toPath());AtomicFile atomic=new AtomicFile(new File(root,"active.json"));FileOutputStream out=null;
        try {out=atomic.startWrite();out.write(next.toString().getBytes(StandardCharsets.UTF_8));atomic.finishWrite(out);}
        catch(Exception e) {atomic.failWrite(out);throw e;}
    }
    JSONObject install(RimeSchemeArchive.Review review,String title,List<String> selected) throws Exception {
        if(packages().size()>=32) throw new IOException("最多保留 32 个方案包，请先删除不再使用的方案");
        String id=UUID.randomUUID().toString();File staged=new File(root,".staging/"+id);
        boolean published=false;
        try {
            RimeSchemeArchive.stage(review,selected,staged);
            String failure=NativeRimeEngine.deployNative(staged.getAbsolutePath(),review.deploymentOrder(selected));
            if(failure!=null) throw new IOException("方案部署或加载失败："+failure);
            if(treeBytes(staged)>512L*1024*1024) throw new IOException("部署结果超过 512 MB");
            JSONObject metadata=new JSONObject().put("format",1).put("id",id).put("name",title)
                .put("sourceDigest",review.digest).put("importedAt",java.time.Instant.now().toString());
            JSONArray schemas=new JSONArray();for(RimeSchemeArchive.Scheme schema:review.schemes) if(selected.contains(schema.id))
                schemas.put(new JSONObject().put("id",schema.id).put("name",schema.name));
            metadata.put("schemas",schemas).put("warnings",new JSONArray(review.warnings));
            Files.write(new File(staged,"package.json").toPath(),metadata.toString().getBytes(StandardCharsets.UTF_8));
            File destination=new File(root,"packages/"+id);Files.createDirectories(destination.toPath().getParent());
            if(!staged.renameTo(destination)) throw new IOException("无法发布已编译的方案包");
            published=true;return metadata;
        } finally {if(!published) RimeSchemeArchive.remove(staged);}
    }
    void remove(String id) throws Exception {
        if(!uuid(id)) throw new IOException("方案标识无效");Selection current=active();
        if(current!=null && current.packageID.equals(id)) activate(null,null);
        RimeSchemeArchive.remove(new File(root,"packages/"+id));
    }
    private static JSONObject read(File file) throws Exception {
        if(!file.isFile() || file.length()>262144) throw new IOException("方案记录无效");
        return new JSONObject(new String(Files.readAllBytes(file.toPath()),StandardCharsets.UTF_8));
    }
    private static long treeBytes(File root) throws IOException {
        if(Files.isSymbolicLink(root.toPath())) throw new IOException("部署结果包含链接");
        long size=root.isFile()?root.length():0;File[] children=root.listFiles();
        if(children!=null) for(File child:children) size+=treeBytes(child);return size;
    }
    private static boolean uuid(String value) {try{return UUID.fromString(value).toString().equals(value);}catch(Exception e){return false;}}
}
