package org.scholay.rimes.core;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import org.yaml.snakeyaml.LoaderOptions;
import org.yaml.snakeyaml.Yaml;
import org.yaml.snakeyaml.constructor.SafeConstructor;

/** Infer native T9 from resolved deployed YAML, using the same contract as iOS. */
public final class ImportedKeyboardLayouts {
    public final String qwerty, nineKey;
    private ImportedKeyboardLayouts(String qwerty,String nineKey) {this.qwerty=qwerty;this.nineKey=nineKey;}
    public String schema(String layout) {return nineKey!=null && ("nineKey".equals(layout) || qwerty==null)?nineKey:qwerty;}
    public boolean usesNineKey(String layout) {return nineKey!=null && nineKey.equals(schema(layout));}
    public static ImportedKeyboardLayouts read(File resources,String selected) {
        Map<?,?> current=info(resources,selected);
        boolean currentNine=nine(current);
        String base=currentNine && selected.endsWith("_t9")?selected.substring(0,selected.length()-3):selected;
        String digits=currentNine?selected:selected+"_t9";
        Map<?,?> letters=info(resources,base),numbers=info(resources,digits);
        String dictionary=string(letters,"translator","dictionary");
        boolean paired=script(letters) && !nine(letters) && nine(numbers) && !dictionary.isEmpty()
                && dictionary.equals(string(numbers,"translator","dictionary"));
        return new ImportedKeyboardLayouts(currentNine?(paired?base:null):selected,currentNine?selected:(paired?digits:null));
    }
    private static Map<?,?> info(File resources,String id) {
        if(!RimeSchemeArchive.identifier(id)) return Collections.emptyMap();
        File build=new File(resources,"build"),file=new File(build,id+".schema.yaml");
        try {
            if(Files.isSymbolicLink(build.toPath()) || Files.isSymbolicLink(file.toPath()) || !file.isFile() || file.length()>1024*1024) return Collections.emptyMap();
            LoaderOptions options=new LoaderOptions();options.setCodePointLimit(1024*1024);options.setMaxAliasesForCollections(32);
            Object parsed=new Yaml(new SafeConstructor(options)).load(Files.readString(file.toPath(),StandardCharsets.UTF_8));
            if(parsed instanceof Map && id.equals(string((Map<?,?>)parsed,"schema","schema_id"))) return (Map<?,?>)parsed;
        } catch(Exception ignored) {}
        return Collections.emptyMap();
    }
    private static Object get(Map<?,?> root,String section,String key) {
        Object value=root.get(section);return value instanceof Map?((Map<?,?>)value).get(key):null;
    }
    private static String string(Map<?,?> root,String section,String key) {
        Object value=get(root,section,key);return value instanceof String?(String)value:"";
    }
    private static boolean script(Map<?,?> root) {
        Object translators=get(root,"engine","translators");
        return translators instanceof List && ((List<?>)translators).contains("script_translator");
    }
    private static boolean nine(Map<?,?> root) {
        Object algebra=get(root,"speller","algebra");
        if(!(algebra instanceof List) || ((List<?>)algebra).isEmpty()) return false;
        List<?> rules=(List<?>)algebra;Object last=rules.get(rules.size()-1);
        if(!(last instanceof String) || !((String)last).matches("xlit/(abcdefghijklmnopqrstuvwxyz|ABCDEFGHIJKLMNOPQRSTUVWXYZ)/22233344455566677778889999/")) return false;
        String alphabet=string(root,"speller","alphabet"),initials=string(root,"speller","initials");
        for(char key:"abcdefghijklmnopqrstuvwxyz23456789".toCharArray()) if(alphabet.indexOf(key)<0) return false;
        for(char key:"23456789".toCharArray()) if(!initials.isEmpty() && initials.indexOf(key)<0) return false;
        return script(root) && string(root,"speller","delimiter").contains("'");
    }
}
