package org.scholay.rimes.core;

/** Ordinary mobile Shift: one letter, double-tap/hold lock, then tap to unlock. */
public final class ShiftState {
    private enum Mode { OFF, ONCE, LOCKED }
    private Mode mode=Mode.OFF;
    private long lastTap;
    public void tap(long uptimeMillis) {
        switch(mode) {
            case OFF: mode=Mode.ONCE; break;
            case ONCE: mode=uptimeMillis-lastTap>=0 && uptimeMillis-lastTap<350?Mode.LOCKED:Mode.OFF; break;
            case LOCKED: mode=Mode.OFF; break;
        }
        lastTap=uptimeMillis;
    }
    public void hold() { mode=Mode.LOCKED; }
    public void reset() { mode=Mode.OFF; lastTap=0; }
    public boolean uppercase() { return mode!=Mode.OFF; }
    public boolean locked() { return mode==Mode.LOCKED; }
    public boolean consumeLetter(String text) {
        if(mode!=Mode.ONCE || !text.codePoints().anyMatch(Character::isLetter)) return false;
        reset(); return true;
    }
}
