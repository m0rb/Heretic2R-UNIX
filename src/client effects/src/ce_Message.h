//
// ce_Message.h
//
// Copyright 1998 Raven Software
//

#pragma once

#include "SinglyLinkedList.h"

typedef enum CE_MsgID_e
{
	MSG_COLLISION = 0, // (parm1) trace_t* trace -- the trace matching the collision, valid only on the frame of collision.
	NUM_MESSAGES
} CE_MsgID_t;

typedef struct Message_s
{
	CE_MsgID_t ID;
	SinglyLinkedList_t parms;
} CE_Message_t;

typedef struct client_entity_s client_entity_t; //mxd. Forward typedef for consistency with .c definitions.
typedef void (*CE_MessageHandler_t)(client_entity_t* self, CE_Message_t* msg);
typedef void (*CE_MsgReceiver_t)(client_entity_t* self, CE_Message_t* msg);

extern void CE_InitMsgMngr(void);
extern void CE_ReleaseMsgMngr(void);

extern void CE_PostMessage(client_entity_t* to, CE_MsgID_t id, const char* format, ...);
extern int CE_ParseMsgParms(CE_Message_t* msg, const char* format, ...);
extern void CE_ProcessMessages(client_entity_t* self);
extern void CE_ClearMessageQueue(client_entity_t* self);