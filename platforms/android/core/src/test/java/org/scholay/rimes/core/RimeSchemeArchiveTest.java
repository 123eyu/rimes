package org.scholay.rimes.core;

import org.junit.Test;
import org.junit.Rule;
import org.junit.rules.TemporaryFolder;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import java.util.zip.*;
import static org.junit.Assert.*;

public final class RimeSchemeArchiveTest {
    @Rule public TemporaryFolder temporary=new TemporaryFolder();
    private static final String SCHEMA="schema:\n  schema_id: demo\n  name: 示例\ntranslator:\n  dictionary: dicts/demo\n";
    private static final String DICT="---\nname: demo\nversion: '1'\n...\n你好\tnihao\t100\n";
    @Test public void gigabyteArchiveStagesWithBoundedBuffers() throws Exception {
        File archive=temporary.newFile("large.zip");
        long size=(1L<<30)+4096;
        byte[] block=new byte[65536];Arrays.fill(block,(byte)' ');
        byte[] header=DICT.getBytes(StandardCharsets.UTF_8);
        try(ZipOutputStream zip=new ZipOutputStream(new BufferedOutputStream(new FileOutputStream(archive)))) {
            zip.setLevel(0);
            zip.putNextEntry(new ZipEntry("demo.schema.yaml"));zip.write(SCHEMA.getBytes(StandardCharsets.UTF_8));zip.closeEntry();
            zip.putNextEntry(new ZipEntry("dicts/demo.dict.yaml"));zip.write(header);
            for(long written=header.length;written<size;) {
                int count=(int)Math.min(block.length,size-written);zip.write(block,0,count);written+=count;
            }
            zip.closeEntry();
        }
        assertTrue(archive.length()>(1L<<30));
        RimeSchemeArchive.Review review=RimeSchemeArchive.inspect(archive);
        File destination=new File(temporary.getRoot(),"large-stage");
        RimeSchemeArchive.stage(review,List.of("demo"),destination);
        assertEquals(size,new File(destination,"dicts/demo.dict.yaml").length());
        assertEquals(review.digest,RimeSchemeArchive.sha256(archive));
    }
    @Test public void oversizedArchivesAndInsufficientSpaceFailBeforeExtraction() throws Exception {
        File archive=temporary.newFile("oversized.zip");
        try(RandomAccessFile file=new RandomAccessFile(archive,"rw")) {file.setLength(RimeSchemeArchive.MAX_ARCHIVE+1);}
        try {RimeSchemeArchive.inspect(archive);fail("accepted oversized ZIP");}
        catch(IOException expected) {assertTrue(expected.getMessage().contains("2 GiB"));}
        try {RimeSchemeArchive.requireSpace(temporary.getRoot(),Long.MAX_VALUE-(1L<<20));fail("accepted insufficient capacity");}
        catch(IOException expected) {assertTrue(expected.getMessage().contains("空间不足"));}
    }
    private LinkedHashMap<String,String> source(String prefix) {
        LinkedHashMap<String,String> files=new LinkedHashMap<>();
        files.put(prefix+"demo.schema.yaml",SCHEMA);files.put(prefix+"dicts/demo.dict.yaml",DICT);return files;
    }
    private File zip(Map<String,String> files) throws Exception {
        File file=temporary.newFile();
        try(ZipOutputStream zip=new ZipOutputStream(new FileOutputStream(file))) {
            for(Map.Entry<String,String> item:files.entrySet()) {
                zip.putNextEntry(new ZipEntry(item.getKey()));zip.write(item.getValue().getBytes(StandardCharsets.UTF_8));zip.closeEntry();
            }
        }
        return file;
    }
    private void rejected(Map<String,String> files,String diagnostic) throws Exception {
        try {RimeSchemeArchive.inspect(zip(files));fail("accepted invalid archive");}
        catch(IOException failure) {assertTrue(failure.getMessage(),failure.getMessage().contains(diagnostic));}
    }
    @Test public void portablePathsAndUnavailableModelsAreDiagnosed() throws Exception {
        for(String path:List.of("CON.txt","dir/aux.yaml","word . ")) {
            Map<String,String> files=source("");files.put(path,"fixture");rejected(files,"路径");
        }
        Map<String,String> files=source("");files.put("model.gram","model");rejected(files,"模型");
    }
    @Test public void originalUpstreamArchiveWhenProvided() throws Exception {
        String path=System.getenv("RIMES_TEST_SCHEME_ARCHIVE");
        org.junit.Assume.assumeTrue("Original ZIP supplied by the compatibility run", path!=null && !path.isEmpty());
        File archive=new File(path);
        assertEquals("abd00fd2b4135453e53b81d8ac893301e808fdb52290ea72977714dfc67400a2",RimeSchemeArchive.sha256(archive));
        RimeSchemeArchive.Review review=RimeSchemeArchive.inspect(archive);
        assertTrue(review.schemes.stream().anyMatch(s -> s.id.equals("wanxiang_t9")));
        File destination=new File(temporary.getRoot(),"original-staged");
        RimeSchemeArchive.stage(review,List.of("wanxiang"),destination);
        assertTrue(new File(destination,"wanxiang_t9.schema.yaml").isFile());
        assertEquals(review.digest,RimeSchemeArchive.sha256(archive));
    }
    @Test public void upstreamLayoutStagesUnchangedAndLeavesOriginalIntact() throws Exception {
        LinkedHashMap<String,String> files=source("repository-main/rime/");
        files.put("__MACOSX/._demo.schema.yaml","metadata");
        files.put("repository-main/rime/build/cached.schema.yaml","discard");
        files.put("repository-main/rime/default.custom.yaml","patch:\n  schema_list:\n    - schema: demo\n");
        files.put("repository-main/rime/lua/module/init.lua","return {}\n");
        File archive=zip(files);String digest=RimeSchemeArchive.sha256(archive);
        RimeSchemeArchive.Review review=RimeSchemeArchive.inspect(archive);
        assertEquals(1,review.schemes.size());assertTrue(review.schemes.get(0).recommended);
        File destination=new File(temporary.getRoot(),"staged");RimeSchemeArchive.stage(review,List.of("demo"),destination);
        assertEquals(DICT,Files.readString(new File(destination,"dicts/demo.dict.yaml").toPath()));
        assertEquals(SCHEMA,Files.readString(new File(destination,"demo.schema.yaml").toPath()));
        assertTrue(new File(destination,"lua/module/init.lua").isFile());
        assertFalse(new File(destination,"default.custom.yaml").exists());assertFalse(new File(destination,"build").exists());
        assertEquals(digest,RimeSchemeArchive.sha256(archive));
    }
    @Test public void nativeLocalOptionalAndYamlReferencesAreAccepted() throws Exception {
        Map<String,String> files=source("");
        files.put("demo.schema.yaml",SCHEMA+"punctuator:\n  __include: symbols.yaml:/punctuator\nrecognizer:\n  __include: :/other\nother:\n  __patch: ['/empty?', 'missing:/optional?']\n");
        files.put("symbols.yaml","punctuator: {}\n");
        assertEquals("demo",RimeSchemeArchive.inspect(zip(files)).schemes.get(0).id);
    }
    @Test public void reciprocalSchemaDependenciesCompileExactlyOnce() throws Exception {
        Map<String,String> files=source("");files.put("demo.schema.yaml",SCHEMA.replace("name: 示例","name: 示例\n  dependencies: [reverse]"));
        files.put("reverse.schema.yaml","schema:\n  schema_id: reverse\n  name: 反查\n  dependencies: [demo]\ntranslator:\n  dictionary: dicts/demo\n");
        assertArrayEquals(new String[]{"reverse","demo"},RimeSchemeArchive.inspect(zip(files)).deploymentOrder(List.of("demo")));
    }
    @Test public void missingSchemaDependencyFailsBeforeStaging() throws Exception {
        Map<String,String> files=source("");files.put("demo.schema.yaml",SCHEMA.replace("name: 示例","name: 示例\n  dependencies: [absent]"));
        RimeSchemeArchive.Review review=RimeSchemeArchive.inspect(zip(files));
        try {review.deploymentOrder(List.of("demo"));fail();}catch(IOException expected){assertTrue(expected.getMessage().contains("absent"));}
    }
    @Test public void pathsAndAmbiguousRootsFail() throws Exception {
        Map<String,String> files=source("");files.put("../escape.txt","x");rejected(files,"路径");
        files=source("");files.put("DEMO.SCHEMA.YAML",SCHEMA);rejected(files,"重复");
        files=source("one/");files.putAll(source("two/"));rejected(files,"独立");
    }
    @Test public void futureManifestAndUnsafeConfigFail() throws Exception {
        Map<String,String> files=source("");files.put("rimes-package.yaml","rimes_package:\n  format: 999\n");rejected(files,"版本");
        files=source("");files.put("demo.schema.yaml",SCHEMA.replace("dicts/demo","../../outside"));rejected(files,"不安全");
        files=source("");files.put("symbols.yaml","x: !!java.net.URL https://example.com\n");
        try{RimeSchemeArchive.inspect(zip(files));fail("unsafe YAML tag");}catch(Exception expected){assertNotNull(expected.getMessage());}
    }
    @Test public void changedArchiveCannotBeInstalledAndExistingDestinationIsPreserved() throws Exception {
        File archive=zip(source(""));RimeSchemeArchive.Review review=RimeSchemeArchive.inspect(archive);
        File destination=temporary.newFolder("existing");File marker=new File(destination,"marker");Files.writeString(marker.toPath(),"keep");
        try{RimeSchemeArchive.stage(review,List.of("demo"),destination);fail();}catch(IOException expected){}
        assertEquals("keep",Files.readString(marker.toPath()));
        Files.writeString(archive.toPath(),"changed");
        File absent=new File(temporary.getRoot(),"absent");
        try{RimeSchemeArchive.stage(review,List.of("demo"),absent);fail();}catch(IOException expected){assertTrue(expected.getMessage().contains("改变"));}
        assertFalse(absent.exists());
    }
    @Test public void oversizedDictionaryHeaderIsBounded() throws Exception {
        Map<String,String> files=source("");StringBuilder random=new StringBuilder("# ");
        Random r=new Random(42);for(int i=0;i<530000;i++) random.append((char)('a'+r.nextInt(26)));
        files.put("dicts/demo.dict.yaml",random+"\n"+DICT);rejected(files,"512");
    }
    @Test public void codedTextDictionaryPreservesOriginalHashAndStages() throws Exception {
        File source=temporary.newFile("words.tsv"),archive=temporary.newFile("generated.zip");
        Files.writeString(source.toPath(),"\ufeff# fixture\n你好\tnihao\t100\n世界\tshijie\n");
        RimeSchemeArchive.Review review=RimeSchemeArchive.inspectTextDictionary(source,"自编词库",archive);
        assertEquals(RimeSchemeArchive.sha256(source),review.digest);
        String id=review.schemes.get(0).id;File destination=new File(temporary.getRoot(),"text-stage");
        RimeSchemeArchive.stage(review,List.of(id),destination);
        assertTrue(Files.readString(new File(destination,id+".dict.yaml").toPath()).contains("你好\tnihao\t100"));
        for(String bad:List.of("纯汉字","字\tabc\t-1","字\tABC","字\t拼音")) {
            Files.writeString(source.toPath(),bad);
            try{RimeSchemeArchive.inspectTextDictionary(source,"invalid",archive);fail();}catch(IOException expected){}
        }
    }
}
