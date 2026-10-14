package org.scholay.rimes.android;

import android.app.Instrumentation;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.provider.Settings;
import android.text.Spanned;
import android.text.style.RelativeSizeSpan;
import android.view.View;
import java.io.File;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import org.scholay.rimes.core.RimeEngine;
import org.scholay.rimes.core.RimeSchemeArchive;

/** Packaged JNI and native candidate rendering, with no real learning or settings changes. */
final class WubiContract {
    static String run(Instrumentation instrumentation) throws Exception {
        Context context=instrumentation.getTargetContext();
        String selected=Settings.Secure.getString(context.getContentResolver(),Settings.Secure.DEFAULT_INPUT_METHOD);
        check(selected==null || !selected.startsWith(context.getPackageName()+"/"),
                "Select another IME before the isolated Wubi contract");
        File data=EngineResources.prepare(context);
        File user=new File(context.getCacheDir(),"wubi-contract-"+UUID.randomUUID());
        check(user.mkdirs(),"isolated user directory");
        try {
            EngineWorker.QUEUE.submit(() -> {
                android.util.Log.i("RIMES-WubiContract","Initializing packaged engine");
                NativeRimeEngine engine=new NativeRimeEngine();
                engine.initialize(data.getAbsolutePath(),user.getAbsolutePath());
                for(String suffix:new String[]{"","_private"}) {
                    for(String[] query:new String[][]{
                        {"rimes_wubi","wq","你",""},
                        {"rimes_wubi","zni","你","wq"},
                        {"rimes_wubi","znihao","你好","wqvb"},
                        {"lingxi_wubi","wq","你",""},
                        {"lingxi_wubi","nihao","你好","wqvb"},
                        {"lingxi_wubi","zni","你","wq"}}) {
                        android.util.Log.i("RIMES-WubiContract","Query "+query[0]+suffix+"/"+query[1]);
                        long session=engine.createSession(); check(session!=0,"create session");
                        try {
                            check(engine.selectSchema(session,query[0]+suffix),"select "+query[0]+suffix);
                            RimeEngine.Snapshot state=RimeEngine.Snapshot.EMPTY;
                            for(char key:query[1].toCharArray()) state=engine.processKey(session,key);
                            int index=state.candidates.indexOf(query[2]);
                            check(index>=0,"candidate for "+query[0]+"/"+query[1]);
                            if("wq".equals(query[1])) check(index==0,"complete Wubi match stays first");
                            if(!query[3].isEmpty()) {
                                check(index<state.comments.size(),"candidate has a comment");
                                String comment=state.comments.get(index);
                                check(comment.startsWith("(") && comment.endsWith(")") && comment.contains(query[3]),
                                        "parenthesized Wubi code for "+query[1]+": "+comment);
                            }
                            check(query[2].equals(engine.selectCandidate(session,state.pageStart+index).commit),
                                    "commit contains only Chinese text");
                            engine.processKey(session,'z'); engine.processKey(session,'n'); engine.processKey(session,'i');
                            check("zn".equals(engine.processKey(session,0xff08).raw),"lookup backspace keeps prefix");
                        } finally { engine.destroySession(session); }
                    }
                }
                for(String base:new String[]{"rimes_wubi","lingxi_wubi"}) {
                    for(String suffix:new String[]{"","_private"}) {
                        long session=engine.createSession();
                        try {
                            check(engine.selectSchema(session,base+suffix),"auto commit schema");
                            for(String code:new String[]{"voni","wqiy","zfiao","fiao"}) {
                                if(code.equals("fiao") && base.equals("rimes_wubi")) continue;
                                android.util.Log.i("RIMES-WubiContract","Auto commit boundary "+base+suffix+"/"+code);
                                engine.clearComposition(session); int typed=0;
                                for(char key:code.toCharArray()) {
                                    String commit=engine.processKey(session,key).commit; ++typed;
                                    check(commit.equals(code.equals("voni") && typed==4?"灵犀":""),
                                            "unique fourth Wubi key only: "+base+suffix+"/"+code+"/"+typed);
                                }
                            }
                        } finally { engine.destroySession(session); }
                    }
                }
                return null;
            }).get(2,TimeUnit.MINUTES);
            android.util.Log.i("RIMES-WubiContract","Checking native candidate layout");
            AtomicReference<Throwable> uiFailure=new AtomicReference<>();
            instrumentation.runOnMainSync(() -> {
                // Main-looper failures do not reach Instrumentation.onStart's catch.
                // Return them to the test thread so CI reports the actual assertion.
                try {
                    KeyButton button=new KeyButton(context); button.plain(true);
                    button.setAutoSizeTextTypeWithDefaults(0); button.setTextSize(20);
                    int height=Math.round(40*context.getResources().getDisplayMetrics().density);
                    int unrestricted=View.MeasureSpec.makeMeasureSpec(0,View.MeasureSpec.UNSPECIFIED);
                    int fixedHeight=View.MeasureSpec.makeMeasureSpec(height,View.MeasureSpec.EXACTLY);
                    button.candidate("你好",""); button.measure(unrestricted,fixedHeight);
                    int plainWidth=button.getMeasuredWidth();
                    button.candidate("你好","(wqvb)"); button.measure(unrestricted,fixedHeight);
                    check(button.getMeasuredWidth()>plainWidth,"annotation participates in candidate width");
                    Spanned label=(Spanned)button.getText();
                    RelativeSizeSpan[] spans=label.getSpans(0,label.length(),RelativeSizeSpan.class);
                    check(spans.length==1 && spans[0].getSizeChange()<1,"annotation has a smaller font");
                    int width=button.getMeasuredWidth(); button.layout(0,0,width,height);
                    Bitmap bitmap=Bitmap.createBitmap(width,height,Bitmap.Config.ARGB_8888);
                    float size=button.getPaint().getTextSize(); button.draw(new Canvas(bitmap));
                    check(button.getPaint().getTextSize()==size,"drawing restores text metrics for reuse");
                    bitmap.recycle();
                    button.candidate("你好","");
                    check("你好".contentEquals(button.getText()),"reused plain candidate removes old code hint");
                } catch(Throwable failure) { uiFailure.set(failure); }
            });
            if(uiFailure.get()!=null) throw new AssertionError("Native candidate layout",uiFailure.get());
            return "PASS Wubi packaged JNI: z lookup, mixed input, code hints, exact commit, private mode, backspace, native candidate layout, four-key unique auto commit\n";
        } finally { RimeSchemeArchive.remove(user); }
    }
    private static void check(boolean condition,String message) { if(!condition) throw new AssertionError(message); }
}
