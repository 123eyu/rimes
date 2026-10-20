package org.scholay.rimes.android;

import android.content.Context;
import android.content.SharedPreferences;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;

/** Validated keyboard preferences. Schema/layout writes always use one atomic editor update. */
public final class KeyboardSettings {
    public static final String PREFERENCES_NAME="keyboard";
    public static final String KEY_SCHEMA="schema";
    public static final String KEY_LAYOUT="layout";
    public static final String KEY_THEME="theme";
    public static final String KEY_TRANSLATION_DIRECTION="translation_direction";
    public static final String KEY_LEARNING="learning";
    public static final String KEY_SYMBOL_GESTURE="symbol_gesture";
    public static final String KEY_NUMERIC_GRID="numeric_grid";
    public static final String KEY_WIDE_SPACE="wide_space";
    public static final String KEY_PORTRAIT_HEIGHT="portrait_height";
    public static final String KEY_LANDSCAPE_HEIGHT="landscape_height";
    public static final String KEY_INPUT_HABITS="input_habits_v1";
    public static final String KEY_AI_MOCK_ENABLED="ai_mock_enabled";
    public static final String DEFAULT_SCHEMA="rimes_pinyin";
    public static final String DEFAULT_LAYOUT="qwerty";
    public static final String DEFAULT_THEME="apple";
    public static final String DEFAULT_TRANSLATION_DIRECTION="auto";
    public static final boolean DEFAULT_LEARNING=true;
    public static final boolean DEFAULT_AI_MOCK_ENABLED=true;

    private static final List<String> SCHEMA_VALUES=values("rimes_pinyin","rimes_ziranma","rimes_wubi","lingxi_wubi");
    private static final List<String> LAYOUT_VALUES=values("qwerty","nineKey","orthogonal","splitOrthogonal");
    private static final List<String> DIRECTION_VALUES=values("auto","zh-en","en-zh");
    private static final List<String> THEME_VALUES=themeIDs();
    private final SharedPreferences preferences;

    public KeyboardSettings(Context context) {
        this(preferences(context));
    }
    public KeyboardSettings(SharedPreferences preferences) {
        if(preferences==null) throw new IllegalArgumentException("Keyboard preferences are required");
        this.preferences=preferences;
    }
    private static SharedPreferences preferences(Context context) {
        if(context==null) throw new IllegalArgumentException("Keyboard context is required");
        return context.getSharedPreferences(PREFERENCES_NAME,Context.MODE_PRIVATE);
    }
    private static List<String> values(String... values) {
        return Collections.unmodifiableList(Arrays.asList(values));
    }
    private static List<String> themeIDs() {
        List<String> values=new ArrayList<>(KeyboardTheme.ALL.length);
        for(KeyboardTheme theme:KeyboardTheme.ALL) values.add(theme.id);
        return Collections.unmodifiableList(values);
    }
    public static List<String> schemaValues() { return SCHEMA_VALUES; }
    public static List<String> layoutValues() { return LAYOUT_VALUES; }
    public static List<String> themeValues() { return THEME_VALUES; }
    public static List<String> translationDirectionValues() { return DIRECTION_VALUES; }
    private static String normalized(String value,List<String> values,String fallback) {
        return values.contains(value)?value:fallback;
    }
    private static String text(Map<String,?> stored,String key,List<String> values,String fallback) {
        Object value=stored.get(key);
        return normalized(value instanceof String?(String)value:null,values,fallback);
    }
    private static boolean flag(Map<String,?> stored,String key,boolean fallback) {
        Object value=stored.get(key);
        return value instanceof Boolean?(Boolean)value:fallback;
    }
    private static boolean chord(String layout) {
        return "orthogonal".equals(layout) || "splitOrthogonal".equals(layout);
    }

    /** One getAll snapshot avoids mixing separate schema/layout change notifications. No read writes. */
    public Snapshot snapshot() {
        Map<String,?> stored=preferences.getAll();
        String schema=text(stored,KEY_SCHEMA,SCHEMA_VALUES,DEFAULT_SCHEMA);
        String layout=text(stored,KEY_LAYOUT,LAYOUT_VALUES,DEFAULT_LAYOUT);
        // Repair a legacy/inconsistent pair in the projection, preserving a valid selected schema.
        if("nineKey".equals(layout) && !DEFAULT_SCHEMA.equals(schema)) layout=DEFAULT_LAYOUT;
        return new Snapshot(schema,layout,text(stored,KEY_THEME,THEME_VALUES,DEFAULT_THEME),
                text(stored,KEY_TRANSLATION_DIRECTION,DIRECTION_VALUES,DEFAULT_TRANSLATION_DIRECTION),
                flag(stored,KEY_LEARNING,DEFAULT_LEARNING),flag(stored,KEY_AI_MOCK_ENABLED,DEFAULT_AI_MOCK_ENABLED));
    }
    public String getSchema() { return snapshot().schema; }
    public String getLayout() { return snapshot().layout; }
    public String getTheme() { return snapshot().theme; }
    public String getTranslationDirection() { return snapshot().translationDirection; }
    public boolean isLearningEnabled() { return snapshot().learning; }
    public boolean isAiMockEnabled() { return snapshot().aiMockEnabled; }

    /** Selecting any schema leaves chord mode; non-Pinyin schemas also leave nine-key mode. */
    public void setSchema(String value) {
        String schema=normalized(value,SCHEMA_VALUES,DEFAULT_SCHEMA);
        synchronized(preferences) {
            String layout=snapshot().layout;
            if(chord(layout) || "nineKey".equals(layout) && !DEFAULT_SCHEMA.equals(schema)) layout=DEFAULT_LAYOUT;
            preferences.edit().putString(KEY_SCHEMA,schema).putString(KEY_LAYOUT,layout).apply();
        }
    }
    /** Nine-key selects Pinyin. Chord layout retains the ordinary schema for later restoration. */
    public void setLayout(String value) {
        String layout=normalized(value,LAYOUT_VALUES,DEFAULT_LAYOUT);
        synchronized(preferences) {
            String schema="nineKey".equals(layout)?DEFAULT_SCHEMA:snapshot().schema;
            preferences.edit().putString(KEY_SCHEMA,schema).putString(KEY_LAYOUT,layout).apply();
        }
    }
    public void setTheme(String value) {
        preferences.edit().putString(KEY_THEME,normalized(value,THEME_VALUES,DEFAULT_THEME)).apply();
    }
    public void setTranslationDirection(String value) {
        preferences.edit().putString(KEY_TRANSLATION_DIRECTION,normalized(value,DIRECTION_VALUES,DEFAULT_TRANSLATION_DIRECTION)).apply();
    }
    public void setLearningEnabled(boolean enabled) { preferences.edit().putBoolean(KEY_LEARNING,enabled).apply(); }
    public void setAiMockEnabled(boolean enabled) { preferences.edit().putBoolean(KEY_AI_MOCK_ENABLED,enabled).apply(); }

    // Android has always enabled hold/up; existing profiles retain that behavior.
    public String symbolGesture() { return text(preferences.getAll(),KEY_SYMBOL_GESTURE,values("off","hold","swipe"),"hold"); }
    public void setSymbolGesture(String mode) { preferences.edit().putString(KEY_SYMBOL_GESTURE,normalized(mode,values("off","hold","swipe"),"hold")).apply(); }
    public boolean numericGrid() { return flag(preferences.getAll(),KEY_NUMERIC_GRID,false); }
    public boolean wideSpace() { return flag(preferences.getAll(),KEY_WIDE_SPACE,false); }
    public void setNumericGrid(boolean value) { preferences.edit().putBoolean(KEY_NUMERIC_GRID,value).apply(); }
    public void setWideSpace(boolean value) { preferences.edit().putBoolean(KEY_WIDE_SPACE,value).apply(); }
    public int heightPercent(boolean landscape) {
        Object value=preferences.getAll().get(landscape?KEY_LANDSCAPE_HEIGHT:KEY_PORTRAIT_HEIGHT);
        return value instanceof Integer?Math.min(130,Math.max(100,(Integer)value)):100;
    }
    public void setHeightPercent(boolean landscape,int value) { preferences.edit().putInt(landscape?KEY_LANDSCAPE_HEIGHT:KEY_PORTRAIT_HEIGHT,Math.min(130,Math.max(100,value))).apply(); }

    public org.json.JSONObject inputOptions(String scope) {
        try {
            String raw=preferences.getString(KEY_INPUT_HABITS,"{}");
            if(raw.length()>262144) return new org.json.JSONObject();
            org.json.JSONObject values=new org.json.JSONObject(raw).optJSONObject(scope);
            return values==null?new org.json.JSONObject():values;
        } catch(Exception e) { return new org.json.JSONObject(); }
    }
    public void setInputOption(String scope,String option,Boolean value) throws org.json.JSONException {
        synchronized(preferences) {
            org.json.JSONObject all=new org.json.JSONObject(preferences.getString(KEY_INPUT_HABITS,"{}"));
            org.json.JSONObject values=all.optJSONObject(scope);
            if(values==null) values=new org.json.JSONObject();
            if(value==null) values.remove(option); else values.put(option,value);
            all.put(scope,values);
            if(all.toString().length()>262144) throw new org.json.JSONException("Input preferences are too large");
            preferences.edit().putString(KEY_INPUT_HABITS,all.toString()).apply();
        }
    }

    public static final class Snapshot {
        public final String schema,layout,theme,translationDirection;
        public final boolean learning,aiMockEnabled;
        private Snapshot(String schema,String layout,String theme,String direction,boolean learning,boolean aiMockEnabled) {
            this.schema=schema; this.layout=layout; this.theme=theme; this.translationDirection=direction;
            this.learning=learning; this.aiMockEnabled=aiMockEnabled;
        }
    }
}
