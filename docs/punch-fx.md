# Punch-in effects

Punch-in effects run on the master outputs, after the mixer and the FX bus. They are played from the step buttons in performance mode, and they can be recorded onto the 16 steps of the current pattern.

Enter performance mode, then hold a button. The effect plays for as long as the button is held and fades out when you release it. One time effect and one tone effect can be held together. The time effect runs first, and the tone effect processes that result. The soft clip sits on top of both.

Within one group, the last button you pressed is the one that plays. Releasing it falls back to another button of that group that is still held.

## Buttons

| Button | Name | Effect |
|---|---|---|
| 8 | Kit | Soft clip |
| 9 | Morph | Repeat, 1/16. Shift: repeat, 1/32. Hold and press 1-7 to change the rate |
| 10 | Song | Repeat, 1/16 triplet |
| 11 | Project | Octave up. Shift: octave down |
| 12 | Config | Crush |
| 13 | Reload | Reverse |
| 14 | Last | Tape stop |
| 15 | FX | DJ filter |
| 16 | Pattern | Highpass sweep |

Buttons 9, 10, 13 and 14 are the time group. Buttons 12, 15 and 16 are the tone group. Button 8 is the soft clip. Button 11 is the octave transpose.

## Effects

**Repeat.** Loops a slice of what was just played. Button 9 is one step long (1/16). Shift and button 9 is half a step (1/32). Button 10 is a 1/16 triplet, two thirds of a step. The loop lines up with the step grid. The first pass is the live signal, so the punch-in does not jump, and the seam of each repeat is faded. At a slow tempo a loop that would be longer than the history (about 186 ms) is halved until it fits. That means 1/8 only plays at its full length above about 165 BPM, and 1/12 above about 110 BPM.

**Repeat rate.** While button 9 is held, buttons 1-7 change its rate instead of starting a roll:

| Button | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| Rate | 1/8 | 1/12 | 1/16 | 1/24 | 1/32 | 1/48 | 1/64 |

The new rate stays until you release button 9; you do not need to keep holding the rate button. The screen shows **RPT** and the current rate while button 9 is held. Going to a shorter rate keeps repeating the audio that was already captured, like a roll. Going to a rate longer than the captured slice captures fresh audio. Each rate is recorded onto the punch track like the other effects.

**Octave.** Each press of button 11 raises the playing note of every track by one octave. Shift and button 11 lowers every track by one octave. Notes that are already sounding move with it, and the next notes do too. The change stays after you release the button, up to four octaves either way. It is not stored in the pattern, and a restart puts it back to zero. While the button is held the screen shows **OCT** and the current octave count.

**Reverse.** Plays the last two steps backwards, then keeps looping that capture.

**Tape stop.** Slows playback to a halt over four steps. The level fades during the last part of the stop.

**Crush.** Holds a sample for several frames, then keeps only the top bits of that sample. Both are set while the button is held.

**DJ filter.** A 2-pole filter on button 15. At the midpoint the signal is untouched. Above the midpoint it is a highpass, opening from a low cutoff up to a high one. Below the midpoint it is a lowpass, and moving further down cuts more of the top. Resonance is a separate control.

**Highpass.** Sweeps from 400 Hz up to 2.5 kHz over four steps, with a mild fixed resonance. The sweep restarts each time the button is pressed.

## Parameters

Hold a button that has parameters and the screen shows them until you release it. The values stay where you left them for the rest of the session, including when a recorded step plays that effect. They are not stored in the pattern, and a restart restores the defaults. If two of these are held, the button you pressed last owns the encoders. Releasing it gives the encoders back to the one still held.

**Soft clip, button 8.** The first encoder sets **DRV**, from **1.0** to **4.0** in steps of 0.1. This is the gain into the clip: 1.0 is a light shave of the peaks, and 4.0 is already a strong slam. It starts at 4.0. The other three encoders do nothing.

**Crush, button 12.** The first encoder sets **HLD**, from **1** to **16**. 1 captures every frame, so the sample-rate reduction is off. 16 holds each sample for 16 frames. It starts at 5. The second encoder sets **MSK**, from **1** to **16**, which is how many bits of the 16-bit sample are kept. 16 is full resolution. 1 keeps only the sign. It starts at 5, the previous crush. The other two encoders do nothing.

**DJ filter, button 15.** The first encoder sets **CUT**, from **0** to **100**. **50** is the midpoint and the filter does nothing. Above 50 the filter is a highpass and the cutoff rises toward the top of the knob. Below 50 it is a lowpass and the cutoff falls toward the bottom. The second encoder sets **RES**, from **0** to **100**. 0 is a flat Butterworth slope. 100 is a strong resonant peak. Both start at 50 and 0. The other two encoders do nothing.

## Recording

Turn record on and start the sequencer. Hold an effect in performance mode and it is written onto the punch track.

A press in the first half of a step lands on that step. A press in the second half lands on the next step. A held time effect, a held tone effect, and the soft clip can all be stored on the same step. While a button is held it replaces whatever that step had recorded for that group.

To wipe the punch track as the pattern plays, hold shift and the copy button while record is on, the sequencer is running, and performance mode is showing. Each step is cleared as the sequencer reaches it.

The whole punch track of the pattern on screen can also be cleared from the clear menu.
