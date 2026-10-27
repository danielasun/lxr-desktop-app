/*
 * PunchFx.h
 *
 * Pocket Operator style punch-in effects on the master outputs.
 * Played live from the step buttons 9-16 in perf mode and recordable
 * into a 16 step punch track per pattern.
 *
 * The effects are split in 2 groups that can be stacked:
 * one time effect (repeat, reverse, tape stop) followed by one tone effect (crush, filters).
 * Step button 8 adds a master soft clip on top of both.
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */

#ifndef INC_SOUNDENGINE_PUNCHFX_H_
#define INC_SOUNDENGINE_PUNCHFX_H_

#include "Soundengine/globals.h"
#include "Soundengine/config.h"

//number of steps in the punch track of a pattern
#define PUNCH_STEPS 16

//order matches the perf mode step buttons 9-16
enum PunchFxTypeEnum
{
	PUNCH_OFF,
	PUNCH_LOOP16,		//repeat 1/16
	PUNCH_LOOP16T,		//repeat 1/16 triplet
	PUNCH_LOOP32,		//repeat 1/32
	PUNCH_CRUSH,		//bit crush + sample rate reduction
	PUNCH_REVERSE,		//play the last 1/8 backwards
	PUNCH_TAPESTOP,		//slow down to a halt
	PUNCH_LPF,			//low pass sweep down
	PUNCH_HPF,			//high pass sweep up

	PUNCH_NUM_TYPES
};

enum PunchFxGroupEnum
{
	PUNCH_GROUP_TIME,	//plays back from the history, one at a time
	PUNCH_GROUP_TONE,	//processes the output of the time group

	PUNCH_NUM_GROUPS
};

//a punch track step holds the time effect in the lower and the tone effect in the upper nibble
#define PUNCH_STEP_TIME(step)	((step) & 0x0f)
#define PUNCH_STEP_TONE(step)	((step) >> 4)

//SEQ_PUNCH_FX data2: bit 4 = group, bits 0-3 = type (PUNCH_OFF releases the group)
#define PUNCH_MSG(group, type)	(((group) << 4) | (type))

static inline uint8_t PunchFx_getGroup(uint8_t type)
{
	return (type == PUNCH_CRUSH || type == PUNCH_LPF || type == PUNCH_HPF) ? PUNCH_GROUP_TONE : PUNCH_GROUP_TIME;
}

void PunchFx_init();

/** set the effect of a group played from the front panel. PUNCH_OFF releases the group.
 * called from the UI side, every call with a type != PUNCH_OFF retriggers the effect */
void PunchFx_setLive(uint8_t group, uint8_t type);

/** 1: clear the punch track steps while the sequencer passes them */
void PunchFx_setErase(uint8_t active);

/** 1: master soft clip while performance button 8 is held */
void PunchFx_setDistort(uint8_t active);

/** process both interleaved stereo DAC buffers in place, after the mixer and FX bus */
void PunchFx_process(int16_t* out1, int16_t* out2, uint32_t frames);

#endif /* INC_SOUNDENGINE_PUNCHFX_H_ */
