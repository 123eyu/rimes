package org.scholay.rimes.core;

import java.io.File;
import java.nio.file.Files;
import org.junit.Test;
import static org.junit.Assert.*;

public final class ImportedKeyboardLayoutsTest {
    @Test public void missingOptionalVariantFallsBackInBothDirections() {
        for(String original:new String[]{"demo","demo_t9"}) {
            String preferred=original.equals("demo")?"demo_t9":"demo";
            java.util.List<String> attempts=new java.util.ArrayList<>();
            assertEquals(original,ImportedKeyboardLayouts.selectSchema(preferred,original,id -> {
                attempts.add(id);return original.equals(id);
            }));
            assertEquals(java.util.Arrays.asList(preferred,original),attempts);
        }
    }
    @Test public void availableVariantDoesNotLoadOriginalAgain() {
        java.util.List<String> attempts=new java.util.ArrayList<>();
        assertEquals("demo_t9",ImportedKeyboardLayouts.selectSchema("demo_t9","demo",id -> {attempts.add(id);return true;}));
        assertEquals(java.util.List.of("demo_t9"),attempts);
    }
    @Test public void unavailableOriginalStillReportsFailureWithoutRetryLoop() {
        for(String preferred:new String[]{"demo","demo_t9"}) {
            java.util.List<String> attempts=new java.util.ArrayList<>();
            try {
                ImportedKeyboardLayouts.selectSchema(preferred,"demo",id -> {attempts.add(id);return false;});
                fail("unusable schema must fail");
            } catch(IllegalStateException expected) {}
            assertEquals(preferred.equals("demo")?1:2,attempts.size());
        }
    }
    private static final String T9="xlit/abcdefghijklmnopqrstuvwxyz/22233344455566677778889999/";
    private void schema(File root,String id,String dictionary,boolean nine,String last) throws Exception {
        File build=new File(root,"build");Files.createDirectories(build.toPath());
        String yaml="schema:\n  schema_id: "+id+"\nengine:\n  translators: [script_translator]\ntranslator:\n  dictionary: "+dictionary+"\nspeller:\n  alphabet: abcdefghijklmnopqrstuvwxyz23456789\n  initials: abcdefghijklmnopqrstuvwxyz23456789\n  delimiter: \"'\"\n  algebra:\n    - erase/^xx$/\n";
        if(nine) yaml+="    - "+T9+"\n";
        if(last!=null) yaml+="    - "+last+"\n";
        Files.writeString(new File(build,id+".schema.yaml").toPath(),yaml);
    }
    @Test public void deployedPairFollowsLayoutPreferenceInBothDirections() throws Exception {
        File root=Files.createTempDirectory("rime-layouts").toFile();
        try {
            schema(root,"demo","words",false,null);schema(root,"demo_t9","words",true,null);
            for(String selected:new String[]{"demo","demo_t9"}) {
                ImportedKeyboardLayouts layouts=ImportedKeyboardLayouts.read(root,selected);
                assertEquals("demo",layouts.schema("qwerty"));assertEquals("demo_t9",layouts.schema("nineKey"));
                assertTrue(layouts.usesNineKey("nineKey"));assertFalse(layouts.usesNineKey("qwerty"));
            }
        } finally {RimeSchemeArchive.remove(root);}
    }
    @Test public void standaloneNineKeyCannotFallBackToLetterKeys() throws Exception {
        File root=Files.createTempDirectory("rime-layouts").toFile();
        try {
            schema(root,"demo_t9","words",true,null);
            ImportedKeyboardLayouts layouts=ImportedKeyboardLayouts.read(root,"demo_t9");
            assertEquals("demo_t9",layouts.schema("qwerty"));assertTrue(layouts.usesNineKey("qwerty"));
        } finally {RimeSchemeArchive.remove(root);}
    }
    @Test public void unrelatedOrUncompiledSiblingDoesNotEnableNineKey() throws Exception {
        File root=Files.createTempDirectory("rime-layouts").toFile();
        try {
            schema(root,"demo","words",false,null);
            assertFalse(ImportedKeyboardLayouts.read(root,"demo").usesNineKey("nineKey"));
            schema(root,"demo_t9","different",true,null);
            assertFalse(ImportedKeyboardLayouts.read(root,"demo").usesNineKey("nineKey"));
        } finally {RimeSchemeArchive.remove(root);}
    }
    @Test public void laterAlgebraAndMissingDigitInitialsRejectAutomaticPairing() throws Exception {
        File root=Files.createTempDirectory("rime-layouts").toFile();
        try {
            schema(root,"demo","words",false,null);schema(root,"demo_t9","words",true,"erase/^[2-9]+$/");
            assertFalse(ImportedKeyboardLayouts.read(root,"demo").usesNineKey("nineKey"));
            schema(root,"demo_t9","words",true,null);
            File file=new File(root,"build/demo_t9.schema.yaml");
            Files.writeString(file.toPath(),Files.readString(file.toPath()).replace("initials: abcdefghijklmnopqrstuvwxyz23456789","initials: abcdefghijklmnopqrstuvwxyz"));
            assertFalse(ImportedKeyboardLayouts.read(root,"demo").usesNineKey("nineKey"));
        } finally {RimeSchemeArchive.remove(root);}
    }
    @Test public void sourceOnlyAndWrongSchemaIdentityDoNotBecomeCompiledLayouts() throws Exception {
        File root=Files.createTempDirectory("rime-layouts").toFile();
        try {
            schema(root,"demo","words",false,null);schema(root,"demo_t9","words",true,null);
            File file=new File(root,"build/demo_t9.schema.yaml");
            Files.writeString(file.toPath(),Files.readString(file.toPath()).replace("schema_id: demo_t9","schema_id: unrelated"));
            assertEquals("demo",ImportedKeyboardLayouts.read(root,"demo").schema("nineKey"));
        } finally {RimeSchemeArchive.remove(root);}
    }
    @Test public void malformedUtf8DoesNotEnableAnOptionalLayout() throws Exception {
        File root=Files.createTempDirectory("rime-layouts").toFile();
        try {
            schema(root,"demo","words",false,null);schema(root,"demo_t9","words",true,null);
            File file=new File(root,"build/demo_t9.schema.yaml");
            Files.write(file.toPath(),new byte[]{(byte)0xc3,0x28},java.nio.file.StandardOpenOption.APPEND);
            assertEquals("demo",ImportedKeyboardLayouts.read(root,"demo").schema("nineKey"));
        } finally {RimeSchemeArchive.remove(root);}
    }
}
