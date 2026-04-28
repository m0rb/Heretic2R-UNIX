//
// g_func_Button.c -- Originally part of g_func.c
//
// Copyright 1998 Raven Software
//

#include "g_Local.h"
#include "g_func_Button.h"
#include "g_DefaultMessageHandler.h"
#include "g_func_Utility.h"
#include "Vector.h"
#include "compat.h"

#pragma region ========================== func_button ==========================

#define SF_TOUCH	1 //mxd

void FuncButtonDone(edict_t* self) //mxd. Named 'button_done' in original logic.
{
	self->moveinfo.state = STATE_BOTTOM;
	self->s.frame = 0;
}

void FuncButtonReturn(edict_t* self) //mxd. Named 'button_return' in original logic.
{
	self->moveinfo.state = STATE_DOWN;
	MoveCalc(self, self->moveinfo.start_origin, FuncButtonDone);
	self->s.frame = 0;

	if (self->health > 0)
		self->takedamage = DAMAGE_YES;
}

void FuncButtonWait(edict_t* self) //mxd. Named 'button_wait' in original logic.
{
	self->moveinfo.state = STATE_TOP;
	G_UseTargets(self, self->activator);
	self->s.frame = 1;

	if (self->moveinfo.wait >= 0.0f)
	{
		self->nextthink = level.time + self->moveinfo.wait;
		self->think = FuncButtonReturn;
	}
}

static void FuncButtonMove(edict_t* self) //mxd. Named 'button_fire' in original logic.
{
	if (self->moveinfo.state == STATE_UP || self->moveinfo.state == STATE_TOP)
		return;

	if (self->moveinfo.sound_start > 0 && !(self->flags & FL_TEAMSLAVE))
		gi.sound(self, CHAN_NO_PHS_ADD + CHAN_VOICE, self->moveinfo.sound_start, 1.0f, ATTN_IDLE, 0.0f);

	self->moveinfo.state = STATE_UP;
	MoveCalc(self, self->moveinfo.end_origin, FuncButtonWait);
}

void FuncButtonUse(edict_t* self, edict_t* other, edict_t* activator) //mxd. Named 'button_use' in original logic.
{
	self->activator = activator;
	FuncButtonMove(self);
}

void FuncButtonTouch(edict_t* self, trace_t* trace) //mxd. Named 'button_touch' in original logic.
{
	edict_t* other = trace->ent;

	if (other->client != NULL && other->health > 0)
	{
		self->activator = other;
		FuncButtonMove(self);
	}
}

static void FuncButtonSetSounds(edict_t* self) //mxd. Named 'button_sounds' in original logic.
{
	switch (self->sounds)
	{
		case 1: self->moveinfo.sound_start = gi.soundindex("doors/basicbutton.wav"); break;
		case 2: self->moveinfo.sound_start = gi.soundindex("doors/clankybutton.wav"); break;
		case 3: self->moveinfo.sound_start = gi.soundindex("doors/steambutton.wav"); break;
		default: break;
	}
}

// QUAKED func_button (0 .5 .8) ? TOUCH
// When a button is touched, it moves some distance in the direction of it's angle, triggers all of it's targets,
// waits "wait" time, then returns to it's original position where it can be triggered again.

// Spawnflags:
// TOUCH - Player can touch button to set it off.

// Variables:
// angle	- Determines the opening direction.
// target	- All entities with a matching targetname will be used.
// speed	- Override the default 40 speed.
// wait		- Override the default 1 second wait (-1 = never return).
// lip		- Override the default 4 pixel lip remaining at end of move.
// health	- If set, the button must be killed instead of touched.
// sounds:
//		0) Silent.
//		1) Basic Button.
//		2) Clanky Button.
//		3) Steam Button.
void SP_func_button(edict_t* self)
{
	G_SetMovedir(self->s.angles, self->movedir);
	self->movetype = PHYSICSTYPE_STOP;
	self->solid = SOLID_BSP;
	self->takedamage = DAMAGE_NO;

	gi.setmodel(self, self->model);
	gi.linkentity(self);

	FuncButtonSetSounds(self);

	if (self->speed == 0.0f)
		self->speed = 40.0f;

	if (self->accel == 0.0f)
		self->accel = self->speed;

	if (self->decel == 0.0f)
		self->decel = self->speed;

	if (self->wait == 0.0f)
		self->wait = 3.0f;

	if (st.lip == 0)
		st.lip = 4;

	VectorCopy(self->s.origin, self->pos1);

	vec3_t abs_movedir;
	VectorAbs(self->movedir, abs_movedir);

	const float dist = DotProduct(abs_movedir, self->size) - (float)st.lip;
	VectorMA(self->pos1, dist, self->movedir, self->pos2);

	self->use = FuncButtonUse;

	if (self->health > 0)
	{
		self->max_health = self->health;
		self->takedamage = DAMAGE_YES;
	}

	if (self->targetname == NULL || (self->spawnflags & SF_TOUCH))
		self->isBlocking = FuncButtonTouch;

	self->moveinfo.state = STATE_BOTTOM;
	self->moveinfo.speed = self->speed;
	self->moveinfo.accel = self->accel;
	self->moveinfo.decel = self->decel;
	self->moveinfo.wait = self->wait;

	VectorCopy(self->pos1, self->moveinfo.start_origin);
	VectorCopy(self->s.angles, self->moveinfo.start_angles);
	VectorCopy(self->pos2, self->moveinfo.end_origin);
	VectorCopy(self->s.angles, self->moveinfo.end_angles);

	self->msgHandler = DefaultMsgHandler;
}

#pragma endregion

#pragma region ========================== func_button support logic ==========================

static void FuncButtonOnDeathMessage(edict_t* self, G_Message_t* msg) //mxd. Named 'button_killed2' in original logic.
{
	self->activator = self->enemy;
	FuncButtonMove(self);
	self->health = self->max_health;
}

void FuncButtonStaticsInit(void)
{
	classStatics[CID_BUTTON].msgReceivers[MSG_DEATH] = FuncButtonOnDeathMessage;
}

#pragma endregion