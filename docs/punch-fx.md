# Punch-in effects

Punch-in effects run on the master outputs, after the mixer and the FX bus. They are played from the step buttons in performance mode, and they can be recorded onto the 16 steps of the current pattern.

Enter performance mode, then hold a button. The effect plays for as long as the button is held and fades out when you release it. One time effect and one tone effect can be held together. The time effect runs first, and the tone effect processes that result. The soft clip sits on top of both.

Within one group, the last button you pressed is the one that plays. Releasing it falls back to another button of that group that is still held.

## Buttons

| Button | Name | Effect |
|---|---|---|
| 8 | Kit | Soft clip |
| 9 | Morph | Repeat, 1/16 |
| 10 | Song | Repeat, 1/16 triplet |
| 11 | Project | Repeat, 1/32 |
| 12 | Config | Crush |
| 13 | Reload | Reverse |
| 14 | Last | Tape stop |
| 15 | FX | Lowpass sweep |
| 16 | Pattern | Highpass sweep |

Buttons 9, 10, 11, 13 and 14 are the time group. Buttons 12, 15 and 16 are the tone group. Button 8 is the soft clip.

## Effects

**Repeat.** Loops a slice of what was just played. 1/16 is one step long, the triplet is two thirds of a step, and 1/32 is half a step. The loop lines up with the step grid. The first pass is the live signal, so the punch-in does not jump, and the seam of each repeat is faded. At a slow tempo a loop that would be longer than the history (about 186 ms) is halved until it fits.

**Reverse.** Plays the last two steps backwards, then keeps looping that capture.

**Tape stop.** Slows playback to a halt over four steps. The level fades during the last part of the stop.

**Crush.** Drops the sample rate to one fifth and keeps the top 6 bits.

**Lowpass.** Sweeps from 4 kHz down to 180 Hz over four steps.

**Highpass.** Sweeps from 400 Hz up to 2.5 kHz over four steps.

Both filters use a mild resonance. The sweep restarts each time the button is pressed.

## Parameters

Only the soft clip has a live parameter. The other effects use the fixed settings above.

Hold button 8 and turn the first parameter encoder. The screen shows **DRV** and the value until you release the button. The range is **1.0** to **4.0** in steps of 0.1. This is the gain into the clip: 1.0 is a light shave of the peaks, and 4.0 is already a strong slam. The value stays where you left it for the rest of the session, including when a recorded step plays the clip. It is not stored in the pattern, and a restart sets it back to 4.0.

The other three encoders do nothing while button 8 is held.

## Recording

Turn record on and start the sequencer. Hold an effect in performance mode and it is written onto the punch track.

A press in the first half of a step lands on that step. A press in the second half lands on the next step. A held time effect, a held tone effect, and the soft clip can all be stored on the same step. While a button is held it replaces whatever that step had recorded for that group.

To wipe the punch track as the pattern plays, hold shift and the copy button while record is on, the sequencer is running, and performance mode is showing. Each step is cleared as the sequencer reaches it.

The whole punch track of the pattern on screen can also be cleared from the clear menu.
