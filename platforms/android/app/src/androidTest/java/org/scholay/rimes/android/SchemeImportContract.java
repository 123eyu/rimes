package org.scholay.rimes.android;

import android.app.Instrumentation;
import android.content.Context;
import android.provider.Settings;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import java.util.concurrent.TimeUnit;
import java.util.zip.*;
import org.json.JSONObject;
import org.scholay.rimes.core.RimeEngine;
import org.scholay.rimes.core.RimeSchemeArchive;

/** Real packaged JNI; public fixture only, no installed scheme or learned-word mutation. */
final class SchemeImportContract {
    static String run(Instrumentation instrumentation,String upstream) throws Exception {
        Context context=instrumentation.getTargetContext();
        String selected=Settings.Secure.getString(context.getContentResolver(),Settings.Secure.DEFAULT_INPUT_METHOD);
        if(selected!=null && selected.startsWith(context.getPackageName()+"/"))
            throw new IllegalStateException("Select another IME before the isolated import contract");
        File root=new File(context.getCacheDir(),"scheme-test-"+UUID.randomUUID());
        if(!root.mkdirs()) throw new IOException("Cannot create test directory");
        try {
            File archive=upstream==null?new File(root,"original.zip"):new File(upstream);
            String schema=upstream==null?"demo":"wanxiang";
            if(upstream==null) fixture(archive);
            String digest=RimeSchemeArchive.sha256(archive);
            return EngineWorker.QUEUE.submit(() -> {
                RimeSchemeArchive.Review review=RimeSchemeArchive.inspect(archive);
                RimeSchemeStore store=new RimeSchemeStore(new File(root,"library"));
                JSONObject installed=store.install(review,"Public input fixture",upstream==null?List.of(schema):List.of(schema,"wanxiang_t9"));
                check(store.active()==null,"installation must not activate");
                String id=installed.getString("id");store.activate(id,schema);
                RimeSchemeStore.Selection active=new RimeSchemeStore(store.root).active();
                check(active!=null && active.schemaID.equals(schema),"selection survives process-style reload");
                File user=new File(root,"learning");Files.createDirectories(user.toPath());
                NativeRimeEngine engine=new NativeRimeEngine();engine.initialize(active.resources.getAbsolutePath(),user.getAbsolutePath());
                type(engine,schema,"nihao","你好");
                if(upstream!=null) {
                    check(active.layouts!=null && active.layouts.usesNineKey("nineKey"),"paired imported T9 uses digit keys");
                    type(engine,active.layouts.schema("nineKey"),"64426","你好");
                }
                store.activate(null,null);check(store.active()==null,"restore built-in selection");
                // Restore the actual bundled engine environment before testing deletion.
                File builtin=EngineResources.prepare(context),builtinUser=new File(root,"builtin-user");Files.createDirectories(builtinUser.toPath());
                engine.initialize(builtin.getAbsolutePath(),builtinUser.getAbsolutePath());
                type(engine,"rimes_pinyin_private","nihao","你好");
                File marker=new File(user,"retain.txt");Files.writeString(marker.toPath(),"public fixture");
                store.remove(id);check(store.packages().isEmpty() && marker.isFile(),"delete resources without deleting learning data");
                check(digest.equals(RimeSchemeArchive.sha256(archive)),"original ZIP remains unchanged");
                return "PASS real JNI import, deploy, Lua module/userdb, candidate and commit, cold selection, built-in fallback, deletion; SHA256="+digest+"\n";
            }).get(10,TimeUnit.MINUTES);
        } finally {RimeSchemeArchive.remove(root);}
    }
    private static void type(NativeRimeEngine engine,String schema,String keys,String expected) {
        long session=engine.createSession();check(session!=0,"session created");
        try {
            check(engine.selectSchema(session,schema),"schema loaded: "+schema);
            RimeEngine.Snapshot snapshot=RimeEngine.Snapshot.EMPTY;
            for(char key:keys.toCharArray()) snapshot=engine.processKey(session,key);
            int index=snapshot.candidates.indexOf(expected);check(index>=0,"expected candidate: "+schema);
            check(expected.equals(engine.selectCandidate(session,snapshot.pageStart+index).commit),"exact Chinese commit");
        } finally {engine.destroySession(session);}
    }
    private static void check(boolean condition,String message) {if(!condition)throw new AssertionError(message);}
    private static void fixture(File file) throws Exception {
        Map<String,String> files=new LinkedHashMap<>();
        files.put("upstream/demo.schema.yaml","schema:\n  schema_id: demo\n  name: Public fixture\n  version: '1'\nengine:\n  processors: [lua_processor@*probe, speller, selector, navigator, express_editor]\n  segmentors: [abc_segmentor, fallback_segmentor]\n  translators: [table_translator]\nspeller:\n  alphabet: abcdefghijklmnopqrstuvwxyz\ntranslator:\n  dictionary: dicts/demo\n  enable_sentence: false\n");
        files.put("upstream/dicts/demo.dict.yaml","---\nname: demo\nversion: '1'\n...\n你好\tnihao\t100\n");
        files.put("upstream/lua/probe/init.lua","return {init=function(env) env.db=assert(UserDb('lua/sequence','userdb')); assert(env.db:open()); assert(env.db:update('public','fixture')) end, func=function(key,env) return 2 end, fini=function(env) env.db:close() end}\n");
        try(ZipOutputStream zip=new ZipOutputStream(new FileOutputStream(file))) {
            for(Map.Entry<String,String> entry:files.entrySet()) {zip.putNextEntry(new ZipEntry(entry.getKey()));zip.write(entry.getValue().getBytes(StandardCharsets.UTF_8));zip.closeEntry();}
        }
    }
}
