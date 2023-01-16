/*
Copyright (C) 2023 Sergey Galushko

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// in_psp.c

#include <pspctrl.h>

#include "../client/client.h"

#define	IN_JOY_MAX_AXES 2
#define IN_JOY_AXIS_X   0
#define IN_JOY_AXIS_Y   1

enum AxisMode
{
	AxisNada = 0,
	AxisForward,
	AxisLook,
	AxisSide,
	AxisTurn,
	AxisUp
};

static byte     joyAxisMap[IN_JOY_MAX_AXES];
static float    joyCurveTable[IN_JOY_MAX_AXES][256];

cvar_t  *in_joystick;

cvar_t  *joy_axisx;	
cvar_t  *joy_axisx_dz_min;
cvar_t  *joy_axisx_dz_max;
cvar_t  *joy_axisx_cv_power;
cvar_t  *joy_axisx_cv_expo;

cvar_t  *joy_axisy;
cvar_t  *joy_axisy_dz_min;
cvar_t  *joy_axisy_dz_max;
cvar_t  *joy_axisy_cv_power;
cvar_t  *joy_axisy_cv_expo;

cvar_t  *joy_forwardsensitivity;
cvar_t  *joy_sidesensitivity;
cvar_t  *joy_upsensitivity;
cvar_t  *joy_pitchsensitivity;
cvar_t  *joy_yawsensitivity;


static struct keymap_s
{
	int srckey;
	int dstkey;
} keyMap[] =
{
#if 0
	{ PSP_CTRL_SELECT,   '~'          },
	{ PSP_CTRL_START,    K_ESCAPE     },
	{ PSP_CTRL_UP,       K_UPARROW    },
	{ PSP_CTRL_RIGHT,    K_RIGHTARROW },
	{ PSP_CTRL_DOWN,     K_DOWNARROW  },
	{ PSP_CTRL_LEFT,     K_LEFTARROW  },
	{ PSP_CTRL_LTRIGGER, K_JOY1       },
	{ PSP_CTRL_RTRIGGER, K_JOY2       },
	{ PSP_CTRL_TRIANGLE, K_SHIFT      },
	{ PSP_CTRL_CIRCLE,   K_SPACE      },
	{ PSP_CTRL_CROSS,    K_ENTER      },
	{ PSP_CTRL_SQUARE,   K_BACKSPACE  },
#else
	{ PSP_CTRL_SELECT  , K_MODE_BUTTON  },
	{ PSP_CTRL_START   , K_START_BUTTON },
	{ PSP_CTRL_UP      , K_UPARROW      },
	{ PSP_CTRL_RIGHT   , K_RIGHTARROW   },
	{ PSP_CTRL_DOWN    , K_DOWNARROW    },
	{ PSP_CTRL_LEFT    , K_LEFTARROW    },
	{ PSP_CTRL_LTRIGGER, K_L1_BUTTON    },
	{ PSP_CTRL_RTRIGGER, K_R1_BUTTON    },
	{ PSP_CTRL_TRIANGLE, K_Y_BUTTON     },
	{ PSP_CTRL_CIRCLE  , K_B_BUTTON     },
	{ PSP_CTRL_CROSS   , K_A_BUTTON     },
	{ PSP_CTRL_SQUARE  , K_X_BUTTON     },
#endif
#if 0
	PSP_CTRL_HOME,
	PSP_CTRL_HOLD,
	PSP_CTRL_NOTE,
	PSP_CTRL_SCREEN,
	PSP_CTRL_VOLUP,
	PSP_CTRL_VOLDOWN,
	PSP_CTRL_WLAN_UP,
	PSP_CTRL_REMOTE,
	PSP_CTRL_DISC,
	PSP_CTRL_MS,
#endif
};

#define IN_KEY_MAX	sizeof(keyMap) / sizeof(struct keymap_s)

static void IN_JoyUpdate(usercmd_t *cmd);

/*
===========
IN_JoyAxisCompute
===========
*/
static float IN_JoyAxisCompute(float axis, float deadzone_min, float deadzone_max, float power, float expo)
{
	float       abs_axis, fabs_axis, fcurve;
	float       scale, r_deadzone_max;
	qboolean    flip_axis = false;

	// (-127) - (0) - (+127)
	abs_axis = axis - 128.0f;
	if(abs_axis < 0.0f)
	{
		abs_axis = -abs_axis - 1.0f;
		flip_axis = true;
	}
	if(abs_axis <= deadzone_min) return 0.0f;
	r_deadzone_max = 127.0f - deadzone_max;
	if(abs_axis >= r_deadzone_max) return (flip_axis ? -1.0f :  1.0f);

	scale = 127.0f / (r_deadzone_max - deadzone_min);
	abs_axis -= deadzone_min;
	abs_axis *= scale;

	fabs_axis = abs_axis / 127.0f; // 0.0f - 1.0f

	if(expo)
	{
		// x * ( x^power * expo + x * ( 1.0 - expo ))
		fcurve = powf(fabs_axis, power) * expo + fabs_axis * (1.0f - expo);
		fabs_axis *= fcurve;
	}

	return (flip_axis ? -fabs_axis :  fabs_axis);
}

/*
===========
IN_Init
===========
*/
void IN_Init (void)
{
	int i;

	in_joystick         = Cvar_Get("in_joystick",           "1",    CVAR_ARCHIVE);

	joy_axisx           = Cvar_Get("joy_axisx",             "4",    CVAR_ARCHIVE);
	joy_axisx_dz_min    = Cvar_Get("joy_axisx_dz_min",      "15",   CVAR_ARCHIVE);
	joy_axisx_dz_max    = Cvar_Get("joy_axisx_dz_max",      "0",    CVAR_ARCHIVE);
	joy_axisx_cv_power  = Cvar_Get("joy_axisx_cv_power",    "2",    CVAR_ARCHIVE);
	joy_axisx_cv_expo   = Cvar_Get("joy_axisx_cv_expo",     "0.5",  CVAR_ARCHIVE);

	joy_axisy           = Cvar_Get("joy_axisy",             "2",    CVAR_ARCHIVE);
	joy_axisy_dz_min    = Cvar_Get("joy_axisy_dz_min",      "15",   CVAR_ARCHIVE);
	joy_axisy_dz_max    = Cvar_Get("joy_axisy_dz_max",      "0",    CVAR_ARCHIVE);
	joy_axisy_cv_power  = Cvar_Get("joy_axisy_cv_power",    "2",    CVAR_ARCHIVE);
	joy_axisy_cv_expo   = Cvar_Get("joy_axisy_cv_expo",     "0.5",  CVAR_ARCHIVE);

	joy_forwardsensitivity  = Cvar_Get("joy_forwardsensitivity",    "-1",   0);
	joy_sidesensitivity     = Cvar_Get("joy_sidesensitivity",       "-1",   0);
	joy_upsensitivity       = Cvar_Get("joy_upsensitivity",         "-1",   0);
	joy_pitchsensitivity    = Cvar_Get("joy_pitchsensitivity",      "1",    0);
	joy_yawsensitivity      = Cvar_Get("joy_yawsensitivity",        "-1",   0);

	// set up the controller.
	sceCtrlSetSamplingCycle( 0 );
	sceCtrlSetSamplingMode( PSP_CTRL_MODE_ANALOG );

	// building a joystick map
	for ( i = 0; i < 256; i++ )
	{
		joyCurveTable[IN_JOY_AXIS_X][i] = IN_JoyAxisCompute(i,
			joy_axisx_dz_min->value,
			joy_axisx_dz_max->value,
			joy_axisx_cv_power->value,
			joy_axisx_cv_expo->value);
		joyCurveTable[IN_JOY_AXIS_Y][i] = IN_JoyAxisCompute(i,
			joy_axisy_dz_min->value,
			joy_axisy_dz_max->value,
			joy_axisy_cv_power->value,
			joy_axisy_cv_expo->value);
	}

	joyAxisMap[IN_JOY_AXIS_X] = ((int)joy_axisx->value) & 0x0f;
	joyAxisMap[IN_JOY_AXIS_Y] = ((int)joy_axisy->value) & 0x0f;
}

/*
===========
IN_Shutdown
===========
*/
void IN_Shutdown (void)
{
}

/*
===========
IN_Commands
===========
*/
void IN_Commands (void)
{
}

/*
===========
IN_Frame
===========
*/
void IN_Frame (void)
{	
}

/*
===========
IN_Move
===========
*/
void IN_Move (usercmd_t *cmd)
{
	IN_JoyUpdate(cmd);
}

/*
===========
IN_Activate
===========
*/
void IN_Activate (qboolean active)
{
}

/*
===========
IN_ActivateMouse
===========
*/
void IN_ActivateMouse (void)
{
}

/*
===========
IN_DeactivateMouse
===========
*/
void IN_DeactivateMouse (void)
{
}

/*
===========
IN_KeyUpdate
===========
*/
void IN_KeyUpdate(void)
{
	int         i, time;
	SceCtrlData buf;
	static unsigned int last_buttons;

	time = Sys_Milliseconds();

	sceCtrlPeekBufferPositive( &buf, 1 );

	for( i = 0; i < IN_KEY_MAX; i++ )
	{
		if((last_buttons ^ buf.Buttons) & keyMap[i].srckey)
			Key_Event(keyMap[i].dstkey, buf.Buttons & keyMap[i].srckey, time);
	}
	last_buttons = buf.Buttons;
}

/*
===========
IN_JoyUpdate
===========
*/
static void IN_JoyUpdate(usercmd_t *cmd)
{
	float       speed, aspeed;
	float       axisValue[IN_JOY_MAX_AXES];
	float       fAxisValue;
	int         i;
	SceCtrlData buf;

	// verify joystick is available and that the user wants to use it
	if (!in_joystick->value)
		return; 

	// collect the joystick data, if possible
	sceCtrlPeekBufferPositive(&buf, 1);

	axisValue[IN_JOY_AXIS_X] = joyCurveTable[IN_JOY_AXIS_X][buf.Lx];
	axisValue[IN_JOY_AXIS_Y] = joyCurveTable[IN_JOY_AXIS_Y][buf.Ly];

	if ((in_speed.state & 1) ^ (int)cl_run->value)
		speed = 2;
	else
		speed = 1;

	aspeed = speed * cls.frametime;

	// loop through the axes
	for (i = 0; i < IN_JOY_MAX_AXES; i++)
	{
		// get the floating point zero-centered, potentially-inverted data for the current axis
		fAxisValue = axisValue[i];

		if(fAxisValue == 0.0f)
			continue;

		switch (joyAxisMap[i])
		{
		case AxisForward:
			// user wants forward control to be forward control
			cmd->forwardmove += (fAxisValue * joy_forwardsensitivity->value) * speed * cl_forwardspeed->value;
			break;
		case AxisSide:
			cmd->sidemove += (fAxisValue * joy_sidesensitivity->value) * speed * cl_sidespeed->value;
			break;
		case AxisUp:
			cmd->upmove += (fAxisValue * joy_upsensitivity->value) * speed * cl_upspeed->value;
			break;
		case AxisTurn:
			if ((in_strafe.state & 1) || lookstrafe->value)
			{
				// user wants turn control to become side control
				cmd->sidemove -= (fAxisValue * joy_sidesensitivity->value) * speed * cl_sidespeed->value;
			}
			else
			{
				// user wants turn control to be turn control
				cl.viewangles[YAW] += (fAxisValue * joy_yawsensitivity->value) * aspeed * cl_yawspeed->value;
			}
			break;
		case AxisLook:
			// pitch movement detected and pitch movement desired by user
			cl.viewangles[PITCH] += (fAxisValue * joy_pitchsensitivity->value) * aspeed * cl_pitchspeed->value;
			break;
		default:
			break;
		}
	}
}

