package org.scholay.rimes.core;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.MessageDigest;
import java.text.Normalizer;
import java.util.*;
import java.util.zip.*;
import org.yaml.snakeyaml.LoaderOptions;
import org.yaml.snakeyaml.Yaml;
import org.yaml.snakeyaml.constructor.SafeConstructor;

/** Portable Rime source ZIP. Inspection/staging never executes Lua or installs scripts. */
public final class RimeSchemeArchive {
    public static final long MAX_ARCHIVE=256L<<20, MAX_FILE=128L<<20, MAX_EXPANDED=512L<<20;
    private static final int MAX_CONFIG=512<<10, MAX_ENTRIES=4096;
    private static final Set<String> GLOBAL=new HashSet<>(Arrays.asList("default.custom.yaml","installation.yaml","user.yaml",
        "hamster.yaml","hamster.custom.yaml","weasel.yaml","weasel.custom.yaml","squirrel.yaml","squirrel.custom.yaml",
        "trime.yaml","trime.custom.yaml","ibus_rime.yaml","ibus_rime.custom.yaml"));
    public static final class Scheme {
        public final String id,name;
        public final List<String> dependencies;
        public final boolean recommended;
        Scheme(String id,String name,List<String> dependencies,boolean recommended) {
            this.id=id;this.name=name;this.dependencies=Collections.unmodifiableList(dependencies);this.recommended=recommended;
        }
    }
    public static final class Review {
        public final String digest, prefix;
        private String archiveDigest;
        public final File archive;
        public final List<Scheme> schemes;
        public final List<String> warnings;
        public final long expandedBytes;
        private final Set<String> resources;
        private final Map<String,Object> defaults;
        Review(File archive,String digest,String prefix,List<Scheme> schemes,Set<String> resources,
               Map<String,Object> defaults,List<String> warnings,long expandedBytes) {
            this.archive=archive;this.digest=digest;this.archiveDigest=digest;this.prefix=prefix;this.schemes=Collections.unmodifiableList(schemes);
            this.resources=resources;this.defaults=defaults;this.warnings=Collections.unmodifiableList(warnings);this.expandedBytes=expandedBytes;
        }
        public String[] deploymentOrder(List<String> selected) throws IOException {
            if(selected.isEmpty() || selected.size()>32 || new HashSet<>(selected).size()!=selected.size()) throw bad("请选择不重复的输入方案");
            Map<String,Scheme> byID=new LinkedHashMap<>();for(Scheme s:schemes) byID.put(s.id,s);
            Set<String> seen=new HashSet<>();List<String> order=new ArrayList<>();
            for(String id:selected) visit(id,byID,seen,order);
            return order.toArray(new String[0]);
        }
        private void visit(String id,Map<String,Scheme> byID,Set<String> seen,List<String> order) throws IOException {
            if(!seen.add(id)) return; // reciprocal reverse-lookup schemas compile once
            Scheme scheme=byID.get(id);if(scheme==null) throw bad("缺少依赖方案 "+id+".schema.yaml");
            for(String dependency:scheme.dependencies) visit(dependency,byID,seen,order);
            order.add(id);
        }
    }
    private RimeSchemeArchive() {}

    /** Compile a coded UTF-8 table with a deterministic Rime wrapper; never guess readings. */
    public static Review inspectTextDictionary(File textFile,String title,File generatedZip) throws Exception {
        if(textFile.length()>128L*1024*1024) throw bad("文本词库不能超过 128 MB");
        String text=StandardCharsets.UTF_8.newDecoder().decode(java.nio.ByteBuffer.wrap(Files.readAllBytes(textFile.toPath()))).toString();
        if(text.startsWith("\ufeff")) text=text.substring(1);
        String digest=sha256(textFile),id="text_"+digest.substring(0,16);
        Set<Character> alphabet=new TreeSet<>();StringBuilder rows=new StringBuilder();int line=0;
        for(String raw:text.split("\\r\\n|\\n|\\r",-1)) {
            line++;if(raw.isEmpty() || raw.startsWith("#")) continue;
            String[] parts=raw.split("\t",-1);
            if(parts.length<2 || parts.length>3 || parts[0].isEmpty() || parts[0].getBytes(StandardCharsets.UTF_8).length>4096 ||
                parts[0].chars().anyMatch(Character::isISOControl) || parts[1].length()>256 ||
                !parts[1].matches("[a-z0-9;'.,/= -]+") || parts[1].trim().isEmpty() ||
                (parts.length==3 && (!parts[2].matches("[0-9]+") || parts[2].length()>10 || Long.parseLong(parts[2])>4294967295L)))
                throw bad("第 "+line+" 行格式不符：词条、编码和可选整数权重须以制表符分隔；纯汉字词表须补充编码");
            for(char c:parts[1].toCharArray()) if(c!=' ') alphabet.add(c);
            rows.append(raw).append('\n');
        }
        if(rows.length()==0) throw bad("文本词库没有带编码的有效词条");
        StringBuilder letters=new StringBuilder();for(char c:alphabet) letters.append(c);
        title=title.substring(0,Math.min(120,title.length()));
        String schema="schema:\n  schema_id: "+id+"\n  name: "+quote(title)+"\n  version: '1'\n"+
            "engine:\n  processors: [ascii_composer, recognizer, key_binder, speller, punctuator, selector, navigator, express_editor]\n"+
            "  segmentors: [ascii_segmentor, abc_segmentor, punct_segmentor, fallback_segmentor]\n  translators: [punct_translator, table_translator]\n"+
            "speller:\n  alphabet: "+quote(letters.toString())+"\n  delimiter: ' '\n"+
            "translator:\n  dictionary: "+id+"\n  enable_sentence: false\n  enable_user_dict: true\n  enable_completion: true\nmenu:\n  page_size: 9\n";
        String dictionary="---\nname: "+id+"\nversion: '1'\nsort: by_weight\n...\n"+rows;
        try(ZipOutputStream zip=new ZipOutputStream(new FileOutputStream(generatedZip))) {
            zip.setLevel(0); // generated text is not subject to false compression-ratio alarms
            for(String[] entry:new String[][]{{id+".schema.yaml",schema},{id+".dict.yaml",dictionary}}) {
                zip.putNextEntry(new ZipEntry(entry[0]));zip.write(entry[1].getBytes(StandardCharsets.UTF_8));zip.closeEntry();
            }
        }
        Review source=inspect(generatedZip);List<String> warnings=new ArrayList<>(source.warnings);
        warnings.add("已为带编码的文本词库生成独立码表方案；原文件未修改。");
        Review result=new Review(generatedZip,digest,source.prefix,source.schemes,source.resources,source.defaults,warnings,source.expandedBytes);
        result.archiveDigest=source.digest;return result;
    }
    private static String quote(String value) {return "'"+value.replace("'","''")+"'";}

    public static Review inspect(File archive) throws Exception {
        if(!archive.isFile() || archive.length()>MAX_ARCHIVE) throw bad("请选择不超过 256 MB 的 Rime ZIP");
        checkCentralDirectory(archive);
        Set<String> parents=new TreeSet<>(), names=new HashSet<>();
        long expanded=0;int count=0;
        try(ZipFile zip=new ZipFile(archive)) {
            for(Enumeration<? extends ZipEntry> all=zip.entries();all.hasMoreElements();) {
                ZipEntry e=all.nextElement();String path=e.getName();safePath(path,e.isDirectory());
                if(!e.isDirectory() && path.toLowerCase(Locale.ROOT).endsWith(".gram")) throw bad("这个 ZIP 包含 .gram 语言模型。五端统一导入暂不支持模型，请选择上游提供的基础方案包。");
                String folded=Normalizer.normalize(path.replaceAll("/$",""),Normalizer.Form.NFC).toLowerCase(Locale.ROOT);
                if(!names.add(folded)) throw bad("ZIP 资源路径重复："+path);
                long size=e.getSize(),compressed=e.getCompressedSize();
                if(++count>MAX_ENTRIES || size<0 || size>MAX_FILE || compressed<0 || size>Math.max(1,compressed)*100 || expanded>MAX_EXPANDED-size) throw bad("ZIP 超过文件大小、展开总量或压缩比限制");
                expanded+=size;
                if(!e.isDirectory() && path.endsWith(".schema.yaml") && !ignored(path)) parents.add(parent(path));
            }
            List<String> roots=new ArrayList<>();
            for(String p:parents) if(parents.stream().noneMatch(other -> !other.equals(p) && (other.isEmpty() || p.startsWith(other+"/")))) roots.add(p);
            if(roots.size()!=1) throw bad(roots.isEmpty()?"ZIP 中没有 Rime 源方案":"ZIP 包含多套独立方案目录，请选择单套方案包");
            String prefix=roots.get(0).isEmpty()?"":roots.get(0)+"/";
            Set<String> resources=new TreeSet<>();Map<String,Map<String,Object>> configs=new TreeMap<>();
            List<String> warnings=new ArrayList<>();
            for(Enumeration<? extends ZipEntry> all=zip.entries();all.hasMoreElements();) {
                ZipEntry e=all.nextElement();if(e.isDirectory() || !e.getName().startsWith(prefix)) continue;
                String path=e.getName().substring(prefix.length());if(!allowed(path)) continue;
                resources.add(path);
                if(path.endsWith(".yaml")) {
                    byte[] content=path.endsWith(".dict.yaml")?dictionaryHeader(zip,e):read(zip,e,MAX_CONFIG);
                    Map<String,Object> node=parse(content,path);
                    if(path.endsWith(".dict.yaml")) { if(!resourceID(text(node.get("name")))) throw bad("词典名称不合法："+path); }
                    else configs.put(path,node);
                    validate(node,"",0,new int[]{40000});
                } else if(path.endsWith(".lua")) {
                    StandardCharsets.UTF_8.newDecoder().decode(java.nio.ByteBuffer.wrap(read(zip,e,1<<20)));
                } else if(path.startsWith("opencc/") && path.endsWith(".json")) {
                    validate(parse(read(zip,e,MAX_CONFIG),path),"opencc",0,new int[]{40000});
                }
            }
            Map<String,Object> defaults=configs.getOrDefault("default.yaml",Collections.emptyMap());
            List<String> preferred=schemaList(defaults.get("schema_list"));
            ZipEntry custom=zip.getEntry(prefix+"default.custom.yaml");
            if(custom!=null) {
                Map<String,Object> patch=map(parse(read(zip,custom,MAX_CONFIG),"default.custom.yaml").get("patch"));
                if(patch.containsKey("schema_list")) preferred=schemaList(patch.get("schema_list"));
            }
            Map<String,Object> manifest=map(configs.getOrDefault("rimes-package.yaml",Collections.emptyMap()).get("rimes_package"));
            if(!manifest.isEmpty()) {
                if(!"1".equals(text(manifest.get("format")))) throw bad("方案包清单版本不受支持，请更新灵犀输入法");
                if(manifest.containsKey("recommended_schemes")) preferred=strings(manifest.get("recommended_schemes"));
            }
            List<Scheme> schemes=new ArrayList<>();
            for(Map.Entry<String,Map<String,Object>> e:configs.entrySet()) {
                String path=e.getKey();if(!path.endsWith(".schema.yaml") || path.contains("/")) continue;
                Map<String,Object> identity=map(e.getValue().get("schema"));String id=text(identity.get("schema_id"));
                if(!identifier(id) || !path.equals(id+".schema.yaml")) throw bad("方案 ID 与文件名不一致："+path);
                Map<String,Object> patch=map(configs.getOrDefault(id+".custom.yaml",Collections.emptyMap()).get("patch"));
                String name=text(patch.getOrDefault("schema/name",identity.getOrDefault("name",id)));
                if(name.isEmpty() || name.length()>120) throw bad("方案名称过长或为空："+id);
                if(patch.containsKey("schema/schema_id") && !id.equals(text(patch.get("schema/schema_id")))) throw bad("补丁不能改变方案 ID");
                schemes.add(new Scheme(id,name,strings(patch.getOrDefault("schema/dependencies",identity.get("dependencies"))),preferred.contains(id)));
            }
            if(schemes.isEmpty() || schemes.size()>32) throw bad("一套方案须包含 1–32 个方案文件");
            warnings.add("原 ZIP 不变；不导入全局偏好、其他输入法的键盘布局、编译缓存和用户数据库。");
            if(resources.stream().anyMatch(p -> p.endsWith(".lua"))) warnings.add("Lua 输入逻辑使用受限运行环境；不运行系统命令或原生扩展。");
            return new Review(archive,sha256(archive),prefix,schemes,resources,defaults,warnings,expanded);
        }
    }

    /** The caller owns this fresh destination and removes it on failure. */
    public static void stage(Review review,List<String> selected,File destination) throws Exception {
        review.deploymentOrder(selected);
        if(!sha256(review.archive).equals(review.archiveDigest)) throw bad("ZIP 在检查后发生改变，请重新选择");
        if(destination.exists() || !destination.mkdirs()) throw bad("无法创建独立的暂存目录");
        try(ZipFile zip=new ZipFile(review.archive)) {
            for(String path:review.resources) {
                if(path.equals("default.yaml")) continue;
                File target=new File(destination,path);Files.createDirectories(target.toPath().getParent());
                ZipEntry e=zip.getEntry(review.prefix+path);
                try(InputStream in=zip.getInputStream(e);OutputStream out=new FileOutputStream(target)) {
                    CRC32 crc=new CRC32();long size=0;byte[] buffer=new byte[32768];int n;
                    while((n=in.read(buffer))!=-1) {
                        if(Thread.currentThread().isInterrupted()) throw bad("导入已取消");
                        size+=n;if(size>e.getSize() || size>MAX_FILE) throw bad("展开大小超过限制");
                        crc.update(buffer,0,n);out.write(buffer,0,n);
                    }
                    if(size!=e.getSize() || crc.getValue()!=e.getCrc()) throw bad("资源校验失败："+path);
                }
            }
            Map<String,Object> defaults=new LinkedHashMap<>();defaults.put("config_version","1");
            List<Map<String,String>> list=new ArrayList<>();for(String id:selected) list.add(Collections.singletonMap("schema",id));defaults.put("schema_list",list);
            for(String key:Arrays.asList("key_binder","ascii_composer","recognizer","punctuator","menu","speller")) if(review.defaults.containsKey(key)) defaults.put(key,review.defaults.get(key));
            Files.write(new File(destination,"default.yaml").toPath(),new Yaml().dump(defaults).getBytes(StandardCharsets.UTF_8));
        } catch(Exception failure) { remove(destination);throw failure; }
    }
    private static boolean ignored(String path) {
        for(String p:path.toLowerCase(Locale.ROOT).split("/")) if(p.startsWith(".") || p.equals("__macosx") || p.equals("build") || p.equals("sync") || p.contains(".userdb")) return true;
        return false;
    }
    private static boolean allowed(String path) {
        String lower=path.toLowerCase(Locale.ROOT);if(ignored(path) || GLOBAL.contains(lower)) return false;
        if(!path.contains("/")) return lower.endsWith(".yaml") || lower.endsWith(".txt") || lower.equals("rime.lua");
        if(lower.startsWith("lua/")) return lower.endsWith(".lua") || lower.endsWith(".txt") || lower.endsWith(".json") || lower.endsWith(".reverse.bin");
        if(lower.startsWith("opencc/")) return Arrays.asList(".json",".txt",".ocd",".ocd2",".lua").stream().anyMatch(lower::endsWith);
        return (lower.endsWith(".dict.yaml") || lower.endsWith(".txt")) && resourceID(path);
    }
    public static boolean identifier(String name) {return name!=null && name.length()<=100 && name.matches("[A-Za-z0-9_.-]+") && !name.contains("..");}
    private static boolean resourceID(String name) {
        if(name==null || name.length()>256) return false;
        for(String part:name.split("/",-1)) if(part.equals(".") || !identifier(part)) return false;
        return true;
    }
    private static void safePath(String path,boolean directory) throws IOException {
        if(path.isEmpty() || path.length()>1024 || path.startsWith("/") || path.contains("\\") || path.contains(":")) throw bad("不安全的资源路径");
        String clean=directory && path.endsWith("/")?path.substring(0,path.length()-1):path;
        for(String part:clean.split("/",-1)) {
            if(part.isEmpty() || part.equals(".") || part.equals("..")) throw bad("资源路径不能越界");
            String base=part.split("\\.",-1)[0].toLowerCase(Locale.ROOT);
            if(part.getBytes(StandardCharsets.UTF_8).length>255 || part.endsWith(".") || part.endsWith(" ") ||
                Arrays.asList("con","aux","nul","prn").contains(base) || base.matches("(com|lpt)[1-9]")) throw bad("资源路径不能在五端通用："+part);
        }
        for(char ch:path.toCharArray()) if(Character.isISOControl(ch)) throw bad("资源路径包含控制字符");
    }
    private static String parent(String path) {int slash=path.lastIndexOf('/');return slash<0?"":path.substring(0,slash);}
    private static byte[] read(ZipFile zip,ZipEntry entry,int limit) throws IOException {
        if(entry.getSize()>limit) throw bad("配置或 Lua 文件超过读取限制："+entry.getName());
        try(InputStream in=zip.getInputStream(entry);ByteArrayOutputStream out=new ByteArrayOutputStream()) {
            byte[] block=new byte[16384];int n;CRC32 crc=new CRC32();
            while((n=in.read(block))!=-1) {if(out.size()>limit-n) throw bad("配置展开超过限制");out.write(block,0,n);crc.update(block,0,n);}
            if(out.size()!=entry.getSize() || crc.getValue()!=entry.getCrc()) throw bad("配置校验失败");
            return out.toByteArray();
        }
    }
    private static byte[] dictionaryHeader(ZipFile zip,ZipEntry entry) throws IOException {
        try(InputStream in=new BufferedInputStream(zip.getInputStream(entry))) {
            ByteArrayOutputStream header=new ByteArrayOutputStream(),line=new ByteArrayOutputStream();int b;
            while((b=in.read())!=-1) {
                if(header.size()+line.size()>=MAX_CONFIG) throw bad("词典头超过 512 KB："+entry.getName());
                if(b!='\n') {line.write(b);continue;}
                String value=new String(line.toByteArray(),StandardCharsets.UTF_8);
                if(value.trim().equals("...")) return header.toByteArray();
                if(!value.trim().isEmpty()) line.writeTo(header);
                header.write('\n');line.reset();
            }
            if(new String(line.toByteArray(),StandardCharsets.UTF_8).trim().equals("...")) return header.toByteArray();
            throw bad("词典缺少 YAML 结束标记："+entry.getName());
        }
    }
    private static Map<String,Object> parse(byte[] bytes,String path) throws IOException {
        LoaderOptions options=new LoaderOptions();options.setCodePointLimit(MAX_CONFIG);options.setNestingDepthLimit(64);
        options.setMaxAliasesForCollections(30);options.setAllowRecursiveKeys(false);options.setAllowDuplicateKeys(path.equals("symbols.yaml"));
        try {
            Object result=new Yaml(new SafeConstructor(options)).load(new ByteArrayInputStream(bytes));
            if(!(result instanceof Map)) throw bad("配置必须是映射："+path);
            return map(result);
        } catch(RuntimeException e) {throw bad("YAML 无法解析："+path+"（"+e.getMessage()+"）");}
    }
    private static void validate(Object node,String path,int depth,int[] budget) throws IOException {
        if(depth>64 || --budget[0]<0) throw bad("配置层级或引用展开超过限制");
        if(path.endsWith("/import_tables")) for(String table:strings(node)) if(!resourceID(table)) throw bad("词典依赖不能指向包外："+table);
        if(path.endsWith("/__include") || path.endsWith("/__patch")) {
            if(node instanceof String) validateReference((String)node);
            else if(node instanceof List) for(Object reference:(List<?>)node) if(reference instanceof String) validateReference((String)reference);
        }
        if(node instanceof Map) {
            for(Map.Entry<?,?> pair:((Map<?,?>)node).entrySet()) {
                if(!(pair.getKey() instanceof String)) throw bad("配置键名必须是文本");
                String key=(String)pair.getKey();Object value=pair.getValue();
                String field=key.endsWith("/+") || key.endsWith("/=")?key.substring(0,key.length()-2):key;
                String leaf=field.substring(field.lastIndexOf('/')+1);
                if(value instanceof String && !((String)value).isEmpty()) {
                    String v=(String)value;
                    if(Arrays.asList("dictionary","user_dict","prism","import_preset").contains(leaf) && !resourceID(v)) throw bad("不安全的词典/配置引用："+v);
                    if(leaf.equals("opencc_config") || path.startsWith("opencc") && leaf.equals("file")) safePath(v.startsWith("./")?v.substring(2):v,false);
                    if(leaf.equals("__include") || leaf.equals("__patch")) validateReference(v);
                }
                validate(value,path+"/"+key,depth+1,budget);
            }
        } else if(node instanceof List) {for(Object value:(List<?>)node) validate(value,path,depth+1,budget);}
    }
    private static void validateReference(String value) throws IOException {
        String reference=value.endsWith("?")?value.substring(0,value.length()-1):value;
        int colon=reference.indexOf(':');String file=colon<0?"":reference.substring(0,colon);
        if(!file.isEmpty() && !resourceID(file) || reference.contains("\\") || Arrays.asList(reference.split("/")).contains("..")) throw bad("不安全的配置引用："+value);
    }
    @SuppressWarnings("unchecked") private static Map<String,Object> map(Object value) {return value instanceof Map?(Map<String,Object>)value:Collections.emptyMap();}
    private static String text(Object value) {return value==null?"":String.valueOf(value);}
    private static List<String> strings(Object value) {List<String> out=new ArrayList<>();if(value instanceof List) for(Object v:(List<?>)value) out.add(text(v));return out;}
    private static List<String> schemaList(Object value) {List<String> out=new ArrayList<>();if(value instanceof List) for(Object v:(List<?>)value) out.add(text(map(v).get("schema")));return out;}
    public static String sha256(File file) throws Exception {
        MessageDigest digest=MessageDigest.getInstance("SHA-256");
        try(InputStream in=new FileInputStream(file)) {byte[] block=new byte[32768];int n;while((n=in.read(block))!=-1) digest.update(block,0,n);}
        StringBuilder result=new StringBuilder();for(byte b:digest.digest()) result.append(String.format(Locale.ROOT,"%02x",b&255));return result.toString();
    }
    public static void remove(File path) throws IOException {
        if(Files.isSymbolicLink(path.toPath())) {Files.delete(path.toPath());return;}
        File[] children=path.listFiles();if(children!=null) for(File child:children) remove(child);
        Files.deleteIfExists(path.toPath());
    }
    private static IOException bad(String message) {return new IOException(message);}

    private static int u16(byte[] b,int n) {return (b[n]&255)|((b[n+1]&255)<<8);}
    private static long u32(byte[] b,int n) {return (u16(b,n)&65535L)|((long)u16(b,n+2)<<16);}
    private static void checkCentralDirectory(File file) throws IOException {
        try(RandomAccessFile in=new RandomAccessFile(file,"r")) {
            long length=in.length();if(length<22) throw bad("ZIP 损坏");
            int tail=(int)Math.min(length,65557);byte[] bytes=new byte[tail];in.seek(length-tail);in.readFully(bytes);int end=-1;
            for(int i=tail-22;i>=0;i--) if(u32(bytes,i)==0x06054b50L && i+22+u16(bytes,i+20)==tail) {end=i;break;}
            if(end<0) throw bad("无法识别 ZIP 目录");
            int count=u16(bytes,end+10);long size=u32(bytes,end+12),offset=u32(bytes,end+16),endOffset=length-tail+end;
            if(u16(bytes,end+4)!=0 || u16(bytes,end+6)!=0 || u16(bytes,end+8)!=count || count>MAX_ENTRIES || offset+size!=endOffset) throw bad("不支持分卷、ZIP64 或异常 ZIP 目录");
            in.seek(offset);byte[] header=new byte[46];
            for(int i=0;i<count;i++) {
                if(in.getFilePointer()+46>endOffset) throw bad("ZIP 条目不完整");in.readFully(header);
                if(u32(header,0)!=0x02014b50L || (u16(header,8)&1)!=0 || (u32(header,38)>>16&0170000)==0120000) throw bad("不支持加密、链接或损坏的 ZIP 条目");
                long next=in.getFilePointer()+u16(header,28)+u16(header,30)+u16(header,32);
                if(next>endOffset) throw bad("ZIP 条目越界");in.seek(next);
            }
            if(in.getFilePointer()!=endOffset) throw bad("ZIP 目录计数不一致");
        }
    }
}
