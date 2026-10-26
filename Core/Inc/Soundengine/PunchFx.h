/*
 * PunchFx.h
 *
 * Pocket Operator style punch-in effects on the master outputs.
 * Played live from the step buttons 9-16 in perf mode and recordable
 * into a 16 step punch track per pattern.
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

void PunchFx_init();

/** set the effect played from the front panel. PUNCH_OFF releases it.
 * called from the UI side, every call with a type != PUNCH_OFF retriggers the effect */
void PunchFx_setLive(uint8_t type);

/** 1: clear the punch track steps while the sequencer passes them */
void PunchFx_setErase(uint8_t active);

/** process both interleaved stereo DAC buffers in place, after the mixer and FX bus */
void PunchFx_process(int16_t* out1, int16_t* out2, uint32_t frames);

#endif /* INC_SOUNDENGINE_PUNCHFX_H_ */
