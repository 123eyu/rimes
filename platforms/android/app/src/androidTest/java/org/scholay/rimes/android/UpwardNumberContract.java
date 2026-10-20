package org.scholay.rimes.android;

import android.app.Activity;
import android.app.Instrumentation;
import android.os.Looper;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.LinearLayout;
import java.util.ArrayList;
import java.util.List;
import org.scholay.rimes.core.KeyboardLayout;

/** Attached native Button taps, hold/up alternates and whole-stream cancellation. */
final class UpwardNumberContract {
    private final Instrumentation instrumentation;
    private final Activity activity;
    private KeyboardRoot root;
    private KeyboardSurface surface;
    private KeyButton key;
    private KeyboardLayout.Mode mode=KeyboardLayout.Mode.QWERTY;
    private boolean chinese;
    private KeyboardTheme theme=KeyboardTheme.ALL[0];
    private final List<String> letters=new ArrayList<>(),numbers=new ArrayList<>();
    private int checks,target;
    private final List<Integer> numberTargets=new ArrayList<>();
    private float density,startX,startY;
    private long down;
    private UpwardNumberContract(Instrumentation instrumentation,Activity activity) { this.instrumentation=instrumentation; this.activity=activity; }
    static int run(Instrumentation instrumentation) {
        if(Looper.myLooper()==Looper.getMainLooper()) throw new IllegalStateException("UpwardNumberContract must run off main");
        Instrumentation.ActivityMonitor monitor=instrumentation.addMonitor(SetupActivity.class.getName(),null,false);
        Activity activity=null;
        String component=instrumentation.getTargetContext().getPackageName()+"/"+SetupActivity.class.getName();
        try(android.os.ParcelFileDescriptor launch=instrumentation.getUiAutomation().executeShellCommand("am start -W -f 0x10008000 -n "+component)) {
            activity=instrumentation.waitForMonitorWithTimeout(monitor,15000);
            if(activity==null) throw new AssertionError("Upward number fixture did not start within 15 seconds");
            UpwardNumberContract test=new UpwardNumberContract(instrumentation,activity); test.run(); return test.checks;
        } catch(java.io.IOException error) { throw new AssertionError("Upward number fixture shell launch failed",error); }
        finally {
            instrumentation.removeMonitor(monitor);
            if(activity!=null) { Activity fixture=activity; instrumentation.runOnMainSync(fixture::finish); instrumentation.waitForIdleSync(); }
        }
    }
    private void onMain(Runnable action) {
        java.util.concurrent.atomic.AtomicReference<Throwable> error=new java.util.concurrent.atomic.AtomicReference<>();
        instrumentation.runOnMainSync(() -> { try { action.run(); } catch(Throwable failure) { error.set(failure); } });
        if(error.get()!=null) throw new AssertionError("Upward number main-thread phase failed",error.get());
    }
    private void idle() { instrumentation.waitForIdleSync(); }
    private void check(boolean condition,String label) { checks++; if(!condition) throw new AssertionError("Upward number: "+label); }
    private void render() { surface.render(mode,chinese,theme); }
    private void findKey(String text) {
        onMain(() -> {
            key=null;
            for(int i=0;i<surface.getChildCount();i++) {
                KeyButton button=(KeyButton)surface.getChildAt(i);
                if(text.contentEquals(button.getContentDescription())) { key=button; break; }
            }
            check(key!=null,"native key exists: "+text);
        });
        boolean[] ready={false};
        for(int i=0;i<80 && !ready[0];i++) {
            onMain(() -> ready[0]=key.isAttachedToWindow() && key.getWidth()>0 && key.getHeight()>0);
            if(!ready[0]) SystemClock.sleep(25);
        }
        onMain(() -> check(ready[0],"native key has real window geometry"));
    }
    private void press() {
        SystemClock.sleep(2);
        onMain(() -> {
            down=SystemClock.uptimeMillis(); startX=surface.getLeft()+key.getLeft()+key.getWidth()/2f;
            startY=surface.getTop()+key.getTop()+key.getHeight()/2f;
            emit(MotionEvent.ACTION_DOWN,0,0);
        });
    }
    private void emit(int action,float dx,float dy) {
        MotionEvent event=MotionEvent.obtain(down,SystemClock.uptimeMillis(),action,startX+dx*density,startY+dy*density,0);
        try { root.dispatchTouchEvent(event); } finally { event.recycle(); }
    }
    private void up(float dx,float dy) { onMain(() -> emit(MotionEvent.ACTION_UP,dx,dy)); idle(); }
    private void hold() { SystemClock.sleep(UpwardNumberTouch.HOLD_MILLIS+70); idle(); }
    private int[] counts() { int[] result={0,0}; onMain(() -> {result[0]=letters.size();result[1]=numbers.size();}); return result; }
    private void unchanged(int[] before,String label) {
        SystemClock.sleep(UpwardNumberTouch.HOLD_MILLIS+30); idle();
        onMain(() -> check(letters.size()==before[0] && numbers.size()==before[1],label));
    }
    private int alternateAccessibilityAction(String value) {
        AccessibilityNodeInfo info=key.createAccessibilityNodeInfo();
        try {
            String name="输入"+(value.length()==1 && value.charAt(0)>='0' && value.charAt(0)<='9'?"数字 ":"符号 ")+value;
            for(AccessibilityNodeInfo.AccessibilityAction item:info.getActionList())
                if(item.getLabel()!=null && name.contentEquals(item.getLabel())) return item.getId();
            return 0;
        } finally { info.recycle(); }
    }
    private int numberAccessibilityAction() { return alternateAccessibilityAction("1"); }
    private void standardAccessibilityClicks() {
        String caps="qwertyuiopasdfghjklzxcvbnm";
        String[] digits={"1","2","3","4","5","6","7","8","9","0"};
        String[] english={",",".","?","!",":",";","/","(",")","\"","'","<",">","-","…","@"};
        String[] han={"，","。","？","！","：","；","、","（","）","“","”","《","》","—","…","·"};
        for(boolean chineseMode:new boolean[]{false,true}) {
            onMain(() -> {chinese=chineseMode;render();});idle();
            for(int i=0;i<caps.length();i++) {
                String letter=caps.substring(i,i+1),alternate=i<10?digits[i]:(chineseMode?han:english)[i-10];
                findKey(letter);int[] before=counts();
                onMain(() -> check(key.performAccessibilityAction(AccessibilityNodeInfo.ACTION_CLICK,null),"standard AX click remains native: "+letter));idle();
                onMain(() -> {
                    check(letters.size()==before[0]+1 && letter.equals(letters.get(letters.size()-1)) && numbers.size()==before[1],
                            "standard AX click emits only its original letter in "+(chineseMode?"Chinese":"English")+": "+letter);
                    int action=alternateAccessibilityAction(alternate);
                    check(action==R.id.key_alternate_action && action>>>24==0x7f,
                            "named alternate uses an application resource ID outside standard action IDs");
                    check(key.performAccessibilityAction(action,null),"only the named alternate selects literal "+alternate);
                });idle();
                onMain(() -> check(letters.size()==before[0]+1 && numbers.size()==before[1]+1 && alternate.equals(numbers.get(numbers.size()-1)),
                        "alternate AX action does not emit or replace a native letter: "+letter));
            }
        }
        onMain(() -> {chinese=false;render();});idle();findKey("q");
    }
    private void expectHint(String value) {
        onMain(() -> {
            AccessibilityNodeInfo info=key.createAccessibilityNodeInfo();
            try {check(info.getHintText()!=null && info.getHintText().toString().endsWith(value) && alternateAccessibilityAction(value)!=0,
                    "current literal alternate is discoverable by hint and named action: "+value);}
            finally {info.recycle();}
        });
    }
    private void multiPointer(int action,float firstDy) {
        multiPointer(action,firstDy,0);
    }
    private void multiPointer(int action,float firstDy,float secondDy) {
        MotionEvent.PointerProperties first=new MotionEvent.PointerProperties(),second=new MotionEvent.PointerProperties();
        first.id=0;second.id=1;first.toolType=second.toolType=MotionEvent.TOOL_TYPE_FINGER;
        MotionEvent.PointerCoords a=new MotionEvent.PointerCoords(),b=new MotionEvent.PointerCoords();
        a.x=startX;a.y=startY+firstDy*density;a.pressure=b.pressure=1;
        b.x=startX+key.getWidth();b.y=startY+secondDy*density;
        MotionEvent event=MotionEvent.obtain(down,SystemClock.uptimeMillis(),action,2,
                new MotionEvent.PointerProperties[]{first,second},new MotionEvent.PointerCoords[]{a,b},0,0,1,1,0,0,
                android.view.InputDevice.SOURCE_TOUCHSCREEN,0);
        try {root.dispatchTouchEvent(event);} finally {event.recycle();}
    }
    private void secondPointerUp() {
        MotionEvent.PointerProperties property=new MotionEvent.PointerProperties();property.id=1;property.toolType=MotionEvent.TOOL_TYPE_FINGER;
        MotionEvent.PointerCoords point=new MotionEvent.PointerCoords();point.x=startX+key.getWidth();point.y=startY;point.pressure=1;
        MotionEvent event=MotionEvent.obtain(down,SystemClock.uptimeMillis(),MotionEvent.ACTION_UP,1,
                new MotionEvent.PointerProperties[]{property},new MotionEvent.PointerCoords[]{point},0,0,1,1,0,0,
                android.view.InputDevice.SOURCE_TOUCHSCREEN,0);
        try {root.dispatchTouchEvent(event);} finally {event.recycle();}
    }
    private void overlappingNativeTaps() {
        findKey("q");
        for(boolean firstLiftsFirst:new boolean[]{false,true}) {
            int[] before=counts();press();
            onMain(() -> {
                multiPointer(MotionEvent.ACTION_POINTER_DOWN|(1<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),0);
                if(firstLiftsFirst) {multiPointer(MotionEvent.ACTION_POINTER_UP,0);secondPointerUp();}
                else {multiPointer(MotionEvent.ACTION_POINTER_UP|(1<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),0);emit(MotionEvent.ACTION_UP,0,0);}
            });idle();
            onMain(() -> check(numbers.size()==before[1] && letters.size()==before[0]+2
                    && letters.subList(before[0],letters.size()).equals(firstLiftsFirst?List.of("q","w"):List.of("w","q")),
                    "overlapping native two-thumb taps both click once in their release order"));
        }
        int[] suppressed=counts();press();
        onMain(() -> multiPointer(MotionEvent.ACTION_POINTER_DOWN|(1<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),0));
        hold();onMain(() -> {
            multiPointer(MotionEvent.ACTION_MOVE,-24,-24);
            multiPointer(MotionEvent.ACTION_POINTER_UP|(1<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),-24,-24);
        });up(0,-24);
        onMain(() -> check(numbers.size()==suppressed[1],"neither split pointer can rearm an alternate in the same multitouch stream"));
    }
    private void punctuationMappings() {
        String caps="asdfghjklzxcvbnm";
        String[] english={",",".","?","!",":",";","/","(",")","\"","'","<",">","-","…","@"};
        String[] han={"，","。","？","！","：","；","、","（","）","“","”","《","》","—","…","·"};
        for(boolean chineseMode:new boolean[]{false,true}) {
            onMain(() -> {chinese=chineseMode;render();});idle();
            for(int i=0;i<caps.length();i++) {
                String expected=(chineseMode?han:english)[i];findKey(caps.substring(i,i+1));expectHint(expected);
                int[] before=counts();press();hold();onMain(() -> emit(MotionEvent.ACTION_MOVE,0,-24));up(0,-24);
                onMain(() -> check(letters.size()==before[0] && numbers.size()==before[1]+1 && expected.equals(numbers.get(numbers.size()-1)),
                        "iOS punctuation mapping in "+(chineseMode?"Chinese":"English")+": "+expected));
            }
        }
        onMain(() -> {mode=KeyboardLayout.Mode.NINE_KEY;render();});idle();
        for(boolean chineseMode:new boolean[]{false,true}) {
            onMain(() -> {chinese=chineseMode;render();});idle();
            String[] values=chineseMode?new String[]{"，","。","？","！","：","；","、","…"}:new String[]{",",".","?","!",":",";","/","…"};
            for(int i=0;i<8;i++) {
                String expected=values[i];findKey(String.valueOf(i+2));expectHint(expected);
                int[] before=counts();press();hold();up(0,-24);
                onMain(() -> check(letters.size()==before[0] && numbers.size()==before[1]+1 && expected.equals(numbers.get(numbers.size()-1)),"nine-key literal mark "+expected));
            }
        }
        onMain(() -> {mode=KeyboardLayout.Mode.QWERTY;chinese=true;render();});idle();findKey("a");
        int[] multi=counts();press();hold();onMain(() -> {
            emit(MotionEvent.ACTION_MOVE,0,-24);
            multiPointer(MotionEvent.ACTION_POINTER_DOWN|(1<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),-24);
            check(!key.isPressed(),"second raw pointer cancels held key before child event splitting");
            multiPointer(MotionEvent.ACTION_POINTER_UP|(1<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),-24);
        });up(0,-24);unchanged(multi,"two-pointer stream cannot submit either a symbol or a letter");
        int[] retired=counts();press();hold();
        KeyButton oldKey=key;int[] oldAction={0};
        onMain(() -> {oldAction[0]=alternateAccessibilityAction("，");chinese=false;render();});up(0,-24);
        unchanged(retired,"language change retires an armed Chinese mark rather than retargeting it to English");
        onMain(() -> check(!oldKey.performAccessibilityAction(oldAction[0],null),"detached Chinese AX alternate cannot use the new mode"));
        findKey("a");expectHint(",");int[] ax=counts();
        onMain(() -> check(key.performAccessibilityAction(alternateAccessibilityAction(","),null),"current English mark AX action is accepted"));idle();
        onMain(() -> check(numbers.size()==ax[1]+1 && ",".equals(numbers.get(numbers.size()-1)),"named punctuation AX action inserts once"));
        int[] shortHold=counts();press();SystemClock.sleep(UpwardNumberTouch.HOLD_MILLIS-130);up(0,-18);
        onMain(() -> check(numbers.size()==shortHold[1],"upward release before the hold deadline never selects punctuation"));
        for(KeyboardLayout.Mode excluded:new KeyboardLayout.Mode[]{KeyboardLayout.Mode.NUMERIC,KeyboardLayout.Mode.SYMBOLS,KeyboardLayout.Mode.EMOJI}) {
            onMain(() -> {mode=excluded;render();});idle();findKey(excluded==KeyboardLayout.Mode.NUMERIC?"1":excluded==KeyboardLayout.Mode.SYMBOLS?"[":"😀");
            onMain(() -> {
                AccessibilityNodeInfo info=key.createAccessibilityNodeInfo();
                try {check(info.getHintText()==null,"number/symbol/emoji pages never inherit letter alternates: "+excluded);}
                finally {info.recycle();}
            });
        }
        onMain(() -> {mode=KeyboardLayout.Mode.QWERTY;render();});idle();findKey("q");
    }
    private void run() {
        onMain(() -> {
            activity.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);
            density=activity.getResources().getDisplayMetrics().density;
            root=new KeyboardRoot(activity); root.setOrientation(LinearLayout.VERTICAL);
            View margin=new View(activity); root.addView(margin,new LinearLayout.LayoutParams(1,Math.round(60*density)));
            surface=new KeyboardSurface(activity,new KeyboardSurface.Handler() {
                @Override public String label(KeyboardLayout.Key key) { return key.text; }
                @Override public String description(KeyboardLayout.Key key) { return key.text; }
                @Override public boolean enabled(KeyboardLayout.Key key) { return true; }
                @Override public boolean selected(KeyboardLayout.Key key) { return false; }
                @Override public void press(KeyboardLayout.Key key) { if(key.action==KeyboardLayout.Action.TEXT) letters.add(key.text); }
                @Override public void onAlternate(String text) { numbers.add(text); numberTargets.add(target); render(); }
            });
            root.addView(surface,new LinearLayout.LayoutParams(Math.round(320*density),LinearLayout.LayoutParams.WRAP_CONTENT));
            render(); activity.setContentView(root);
        }); idle(); findKey("q");
        press(); up(0,0); onMain(() -> check(letters.equals(List.of("q")) && numbers.isEmpty(),"native tap remains one letter"));
        press(); hold(); up(0,0); onMain(() -> check(letters.size()==2 && numbers.isEmpty(),"holding without sliding still types one letter"));
        int[] rapid=counts(); press();
        onMain(() -> {
            emit(MotionEvent.ACTION_UP,0,0);
            check(key.performAccessibilityAction(numberAccessibilityAction(),null),"AX alternate accepted after a completed pending tap");
        }); idle();
        onMain(() -> check(letters.size()==rapid[0]+1 && numbers.size()==rapid[1]+1,"independent AX alternate preserves the prior native tap click"));
        int[] held=counts();
        press(); hold();
        onMain(() -> { emit(MotionEvent.ACTION_MOVE,0,-36); check(key.isPressed(),"armed upward slide preserves cap feedback outside the native cell"); });
        up(0,-36); onMain(() -> check(numbers.size()==held[1]+1 && numbers.get(numbers.size()-1).equals("1") && letters.size()==held[0],"hold/up inserts one literal number instead of its letter"));
        int[] once=counts(); onMain(() -> emit(MotionEvent.ACTION_UP,0,-36)); unchanged(once,"duplicate release cannot repeat an alternate");

        int[] quick=counts(); press();
        onMain(() -> emit(MotionEvent.ACTION_MOVE,0,-36)); hold(); up(0,-36);
        unchanged(quick,"a quick slide cannot become an alternate after waiting outside the key");
        press(); hold(); onMain(() -> {emit(MotionEvent.ACTION_MOVE,0,-36);emit(MotionEvent.ACTION_MOVE,0,0);}); up(0,0);
        onMain(() -> check(numbers.size()==quick[1] && letters.size()==quick[0]+1,"returning to the cap cancels alternate selection"));
        int[] sideways=counts(); press(); hold(); onMain(() -> emit(MotionEvent.ACTION_MOVE,50,-36)); up(50,-36);
        unchanged(sideways,"sideways release outside the cell cannot type either a number or a letter");

        int[] retired=counts(); press(); hold(); onMain(() -> {emit(MotionEvent.ACTION_MOVE,0,-36);root.cancelPendingInputEvents();target++;});
        onMain(() -> emit(MotionEvent.ACTION_MOVE,0,-36)); up(0,-36);
        unchanged(retired,"target retirement removes an armed alternate and its old release");
        onMain(() -> check(!numberTargets.contains(target),"old alternate does not adopt the new target"));
        int[] pendingHold=counts(); press(); onMain(() -> root.cancelPendingInputEvents()); hold(); up(0,-36);
        unchanged(pendingHold,"retirement before the arm deadline removes the pending hold callback");
        press(); hold(); onMain(() -> emit(MotionEvent.ACTION_MOVE,0,-24)); up(0,-24);
        onMain(() -> check(numbers.size()==retired[1]+1 && numberTargets.get(numbers.size()-1)==target,"a fresh target gesture works once"));

        int[] themed=counts(); press(); hold(); onMain(() -> {emit(MotionEvent.ACTION_MOVE,0,-36);theme=KeyboardTheme.ALL[1];render();}); up(0,-36);
        unchanged(themed,"theme replacement retires armed selection");
        int[] canceled=counts(); press(); hold(); onMain(() -> {emit(MotionEvent.ACTION_MOVE,0,-36);emit(MotionEvent.ACTION_CANCEL,0,-36);}); up(0,-36);
        unchanged(canceled,"ACTION_CANCEL makes an old alternate inert");
        int[] hidden=counts(); press(); hold(); onMain(() -> {emit(MotionEvent.ACTION_MOVE,0,-36);surface.setVisibility(View.GONE);}); up(0,-36);
        unchanged(hidden,"hidden keyboard cannot commit its old alternate");
        onMain(() -> surface.setVisibility(View.VISIBLE)); idle(); findKey("q");

        int[] switched=counts(); press(); hold(); onMain(() -> {emit(MotionEvent.ACTION_MOVE,0,-36);mode=KeyboardLayout.Mode.NUMERIC;render();}); up(0,-36);
        unchanged(switched,"new layout cannot receive the old alternate release");
        onMain(() -> {mode=KeyboardLayout.Mode.QWERTY;render();}); idle(); findKey("q");
        int[] accessibility=counts();
        onMain(() -> {
            check(key.performAccessibilityAction(AccessibilityNodeInfo.ACTION_CLICK,null),"native accessibility click stays supported");
            int action=numberAccessibilityAction();
            check(action!=0 && key.performAccessibilityAction(action,null),"number has a named accessibility action");
        }); idle(); onMain(() -> check(letters.size()==accessibility[0]+1 && numbers.size()==accessibility[1]+1,"accessibility inserts exactly one selected action"));
        for(int i=0;i<10;i++) {
            String letter="qwertyuiop".substring(i,i+1),number="1234567890".substring(i,i+1); findKey(letter);
            int[] before=counts(); press(); hold(); onMain(() -> emit(MotionEvent.ACTION_MOVE,0,-24)); up(0,-24);
            onMain(() -> check(letters.size()==before[0] && numbers.size()==before[1]+1 && numbers.get(numbers.size()-1).equals(number),"top-row mapping "+letter+" → "+number));
        }
        overlappingNativeTaps();standardAccessibilityClicks();punctuationMappings();
        android.content.SharedPreferences fixture=activity.getSharedPreferences("input-options-contract",android.content.Context.MODE_PRIVATE);
        fixture.edit().clear().commit();
        KeyboardSettings settings=new KeyboardSettings(fixture);
        onMain(() -> {
            check(settings.symbolGesture().equals("hold") && settings.heightPercent(false)==100 && !settings.numericGrid(),"legacy mobile defaults preserved");
            settings.setSymbolGesture("swipe"); settings.setHeightPercent(false,125); settings.setHeightPercent(true,110);
            settings.setNumericGrid(true); settings.setWideSpace(true);
            check(new KeyboardSettings(fixture).heightPercent(false)==125 && settings.heightPercent(true)==110,"height saved per orientation");
            mode=KeyboardLayout.Mode.QWERTY; surface.applyPreferences(settings); render();
        }); idle(); findKey("q");
        int[] direct=counts(); press(); onMain(() -> emit(MotionEvent.ACTION_MOVE,0,-24)); up(0,-24);
        onMain(() -> check(letters.size()==direct[0] && numbers.size()==direct[1]+1,"direct swipe emits once without hold delay"));
        int[] directCanceled=counts(); press(); onMain(() -> {emit(MotionEvent.ACTION_MOVE,0,-24);root.cancelPendingInputEvents();}); up(0,-24);
        unchanged(directCanceled,"retirement also cancels direct swipe");
        onMain(() -> { settings.setSymbolGesture("off"); surface.applyPreferences(settings); render(); }); idle(); findKey("q");
        int[] disabled=counts(); press(); up(0,0);
        onMain(() -> check(letters.size()==disabled[0]+1 && numbers.size()==disabled[1],"disabled gesture preserves ordinary tap"));
        onMain(() -> { settings.setSymbolGesture("hold"); surface.applyPreferences(settings); render(); }); idle(); findKey("q");
        fixture.edit().clear().commit();
        int[] detached=counts(); press(); hold(); onMain(() -> {emit(MotionEvent.ACTION_MOVE,0,-24);activity.setContentView(new LinearLayout(activity));});
        unchanged(detached,"detaching an armed keyboard cannot leave an insertion callback");
    }
}
