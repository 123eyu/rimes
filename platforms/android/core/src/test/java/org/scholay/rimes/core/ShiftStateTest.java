package org.scholay.rimes.core;

import org.junit.Test;
import static org.junit.Assert.*;

public class ShiftStateTest {
    @Test public void onceSurvivesDigitsAndPunctuationButEndsAfterALetter() {
        ShiftState shift=new ShiftState(); shift.tap(1000);
        assertTrue(shift.uppercase()); assertFalse(shift.locked());
        for(String text:new String[]{"1"," ","。","😀"}) assertFalse(shift.consumeLetter(text));
        assertTrue(shift.consumeLetter("a")); assertFalse(shift.uppercase());
    }
    @Test public void doubleTapAndHoldLockUntilTappedOrReset() {
        ShiftState shift=new ShiftState(); shift.tap(1000); shift.tap(1200);
        assertTrue(shift.locked()); assertFalse(shift.consumeLetter("a")); assertFalse(shift.consumeLetter("b"));
        shift.tap(1400); assertFalse(shift.uppercase());
        shift.hold(); assertTrue(shift.locked()); shift.reset(); assertFalse(shift.uppercase());
    }
    @Test public void slowSecondTapTurnsShiftOffAndConsumedShiftDoesNotBecomeALock() {
        ShiftState shift=new ShiftState(); shift.tap(1000); shift.tap(1350); assertFalse(shift.uppercase());
        shift.tap(1500); shift.consumeLetter("a"); shift.tap(1550);
        assertTrue(shift.uppercase()); assertFalse(shift.locked());
    }
}
