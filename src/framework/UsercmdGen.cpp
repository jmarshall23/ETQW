// Copyright (C) 2007 Id Software, Inc.
//

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "../sys/sys_local.h"

extern volatile int com_ticNumber;
extern idCVar com_timescale;

/*
================
usercmd_t::ByteSwap
================
*/
void usercmd_t::ByteSwap() {
	gameFrame = LittleLong( gameFrame );
	gameTime = LittleLong( gameTime );
	duplicateCount = LittleLong( duplicateCount );
	buttons.btnValue = LittleShort( buttons.btnValue );
	angles[ 0 ] = LittleShort( angles[ 0 ] );
	angles[ 1 ] = LittleShort( angles[ 1 ] );
	angles[ 2 ] = LittleShort( angles[ 2 ] );
}

/*
================
usercmd_t::operator==
================
*/
bool usercmd_t::operator==( const usercmd_t& rhs ) const {
	return gameFrame == rhs.gameFrame
		&& gameTime == rhs.gameTime
		&& duplicateCount == rhs.duplicateCount
		&& buttons.btnValue == rhs.buttons.btnValue
		&& forwardmove == rhs.forwardmove
		&& rightmove == rhs.rightmove
		&& upmove == rhs.upmove
		&& angles[ 0 ] == rhs.angles[ 0 ]
		&& angles[ 1 ] == rhs.angles[ 1 ]
		&& angles[ 2 ] == rhs.angles[ 2 ]
		&& impulse == rhs.impulse
		&& flags == rhs.flags
		&& *reinterpret_cast< const byte* >( &clientButtons )
			== *reinterpret_cast< const byte* >( &rhs.clientButtons );
}

const int KEY_MOVESPEED = 127;

/*
===============================================================================

	sdButtonState

===============================================================================
*/
class sdButtonState {
public:
							sdButtonState() { Clear(); }

	void					Clear() {
								on = false;
								held = false;
							}
	void					SetKeyState( bool keyState, bool toggle ) {
								if ( !toggle ) {
									held = false;
									on = keyState;
								} else if ( !keyState ) {
									held = false;
								} else if ( !held ) {
									held = true;
									on = !on;
								}
							}
	bool					On() const { return on; }
	void					SetOn( bool value ) { on = value; }
	bool					Held() const { return held; }

private:
	bool					on;
	bool					held;
};

/*
===============================================================================

	idUsercmdGenLocal

===============================================================================
*/
class idUsercmdGenLocal : public idUsercmdGen {
public:
							idUsercmdGenLocal();

	virtual void			Init();
	virtual void			InitForNewMap();
	virtual void			Shutdown();
	virtual void			Clear();
	virtual void			ClearAngles();
	virtual usercmd_t		TicCmd( int ticNumber );
	virtual void			UsercmdInterrupt();
	virtual usercmd_t		GetDirectUsercmd( bool doGameCallback = true );
	virtual void			HandleCommand( const sdKeyCommand* command, bool down );
	virtual bool			ProcessEvent( const sdSysEvent& event );

private:
	sdKeyCommand*			Translate( const idKey& key );
	void					MakeCurrent( bool doGameCallback );
	void					InitCurrent();
	void					HandleKey( idKey& key, bool down );
	bool					ButtonState( usercmdButton_t button ) const;
	void					AdjustAngles();
	void					KeyMove();
	void					JoystickMove();
	void					MouseMove( bool doGameCallback );
	void					ControllerMove( bool doGameCallback );
	void					CmdButtons();
	static void				Mouse( bool postEvents );
	static void				Keyboard( bool postEvents );
	static void				Joystick();
	static void				Controllers( bool postEvents );

	idVec3					viewangles;
	int						flags;
	int						impulse;
	sdButtonState			toggledRun;
	sdButtonState			toggledSprint;
	int						buttonState[ UB_MAX_BUTTONS ];
	int						lastCommandTime;
	bool					initialized;
	usercmd_t				cmd;
	usercmd_t				buffered[ MAX_BUFFERED_USERCMD ];
	int						mouseDx;
	int						mouseDy;
	int						scheduledImpulse;

	static idCVar			in_yawSpeed;
	static idCVar			in_pitchSpeed;
	static idCVar			in_angleSpeedKey;
	static idCVar			in_freeLook;
	static idCVar			in_toggleRun;
	static idCVar			in_toggleSprint;
	static idCVar			sensitivity;
	static idCVar			m_pitch;
	static idCVar			m_yaw;
	static idCVar			m_strafeScale;
	static idCVar			m_smooth;
	static idCVar			m_strafeSmooth;
	static idCVar			m_showMouseRate;
};

static_assert( sizeof( sdButtonState ) == 0x2, "sdButtonState must match the ETQW PDB layout" );
#if defined( _M_IX86 )
static_assert( sizeof( idUsercmdGenLocal ) == 0x7B0, "idUsercmdGenLocal must match the ETQW PDB layout" );
#endif

idCVar idUsercmdGenLocal::in_yawSpeed( "in_yawspeed", "140", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "yaw change speed when holding down _left or _right" );
idCVar idUsercmdGenLocal::in_pitchSpeed( "in_pitchspeed", "140", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "pitch change speed when holding down _lookUp or _lookDown" );
idCVar idUsercmdGenLocal::in_angleSpeedKey( "in_anglespeedkey", "1.5", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "angle change scale while walking" );
idCVar idUsercmdGenLocal::in_freeLook( "in_freeLook", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "look around with the mouse" );
idCVar idUsercmdGenLocal::in_toggleRun( "in_toggleRun", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "pressing _speed toggles run" );
idCVar idUsercmdGenLocal::in_toggleSprint( "in_toggleSprint", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "pressing _sprint toggles sprint" );
idCVar idUsercmdGenLocal::sensitivity( "sensitivity", "5", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "mouse view sensitivity" );
idCVar idUsercmdGenLocal::m_pitch( "m_pitch", "0.022", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "mouse pitch scale" );
idCVar idUsercmdGenLocal::m_yaw( "m_yaw", "0.022", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "mouse yaw scale" );
idCVar idUsercmdGenLocal::m_strafeScale( "m_strafeScale", "6.25", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "mouse strafe movement scale" );
idCVar idUsercmdGenLocal::m_smooth( "m_smooth", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "number of samples blended for mouse viewing", 1, 8, idCmdSystem::ArgCompletion_Integer< 1, 8 > );
idCVar idUsercmdGenLocal::m_strafeSmooth( "m_strafeSmooth", "4", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "number of samples blended for mouse movement", 1, 8, idCmdSystem::ArgCompletion_Integer< 1, 8 > );
idCVar idUsercmdGenLocal::m_showMouseRate( "m_showMouseRate", "0", CVAR_SYSTEM | CVAR_BOOL, "show mouse movement" );

static idUsercmdGenLocal localUsercmdGen;
idUsercmdGen* usercmdGen = &localUsercmdGen;

idUsercmdGenLocal::idUsercmdGenLocal() :
	flags( 0 ),
	impulse( 0 ),
	lastCommandTime( 0 ),
	initialized( false ),
	mouseDx( 0 ),
	mouseDy( 0 ),
	scheduledImpulse( -1 ) {
	toggledRun.SetOn( true );
	ClearAngles();
	Clear();
	memset( &cmd, 0, sizeof( cmd ) );
	memset( buffered, 0, sizeof( buffered ) );
}

void idUsercmdGenLocal::Init() {
	initialized = true;
	Clear();
}

void idUsercmdGenLocal::InitForNewMap() {
	flags = 0;
	impulse = 0;
	toggledRun.Clear();
	toggledRun.SetOn( true );
	toggledSprint.Clear();
	Clear();
	ClearAngles();
}

void idUsercmdGenLocal::Shutdown() {
	initialized = false;
}

void idUsercmdGenLocal::Clear() {
	memset( buttonState, 0, sizeof( buttonState ) );
	mouseDx = 0;
	mouseDy = 0;
	scheduledImpulse = -1;
}

void idUsercmdGenLocal::ClearAngles() {
	viewangles.Zero();
}

usercmd_t idUsercmdGenLocal::TicCmd( int ticNumber ) {
	if ( ticNumber > com_ticNumber + 1 ) {
		common->Error( "idUsercmdGenLocal::TicCmd ticNumber > com_ticNumber" );
	}
	return buffered[ ticNumber & ( MAX_BUFFERED_USERCMD - 1 ) ];
}

bool idUsercmdGenLocal::ButtonState( usercmdButton_t button ) const {
	return button >= 0 && button < UB_MAX_BUTTONS && buttonState[ button ] > 0;
}

void idUsercmdGenLocal::AdjustAngles() {
	float timescale = com_timescale.GetFloat();
	int msec = USERCMD_MSEC;
	if ( timescale != 1.0f ) {
		msec = idMath::Ftoi( USERCMD_MSEC / timescale );
		if ( msec < 1 ) {
			msec = 1;
		}
	}

	float speed = msec * 0.001f;
	if ( !toggledRun.On() ) {
		speed *= in_angleSpeedKey.GetFloat();
	}

	if ( !ButtonState( UB_STRAFE ) ) {
		viewangles[ YAW ] -= in_yawSpeed.GetFloat() * speed * ButtonState( UB_RIGHT );
		viewangles[ YAW ] += in_yawSpeed.GetFloat() * speed * ButtonState( UB_LEFT );
	}
	viewangles[ PITCH ] -= in_pitchSpeed.GetFloat() * speed * ButtonState( UB_LOOKUP );
	viewangles[ PITCH ] += in_pitchSpeed.GetFloat() * speed * ButtonState( UB_LOOKDOWN );
}

void idUsercmdGenLocal::KeyMove() {
	int side = 0;
	if ( ButtonState( UB_STRAFE ) ) {
		side += KEY_MOVESPEED * ButtonState( UB_RIGHT );
		side -= KEY_MOVESPEED * ButtonState( UB_LEFT );
	}
	side += KEY_MOVESPEED * ButtonState( UB_MOVERIGHT );
	side -= KEY_MOVESPEED * ButtonState( UB_MOVELEFT );

	const int up = KEY_MOVESPEED * ( ButtonState( UB_UP ) - ButtonState( UB_DOWN ) );
	const int forward = KEY_MOVESPEED * ( ButtonState( UB_FORWARD ) - ButtonState( UB_BACK ) );

	const char clampedForward = idMath::ClampChar( forward );
	const char clampedSide = idMath::ClampChar( side );
	const char clampedUp = idMath::ClampChar( up );
	if ( game == NULL || !game->KeyMove( clampedForward, clampedSide, clampedUp, cmd ) ) {
		cmd.forwardmove = clampedForward;
		cmd.rightmove = clampedSide;
		cmd.upmove = clampedUp;
	}
}

void idUsercmdGenLocal::MouseMove( bool doGameCallback ) {
	static int history[ 8 ][ 2 ];
	static int historyCounter;

	history[ historyCounter & 7 ][ 0 ] = mouseDx;
	history[ historyCounter & 7 ][ 1 ] = mouseDy;

	int smooth = idMath::ClampInt( 1, 8, m_smooth.GetInteger() );
	float mx = 0.0f;
	float my = 0.0f;
	for ( int i = 0; i < smooth; i++ ) {
		mx += history[ ( historyCounter - i ) & 7 ][ 0 ];
		my += history[ ( historyCounter - i ) & 7 ][ 1 ];
	}
	mx /= smooth;
	my /= smooth;

	smooth = idMath::ClampInt( 1, 8, m_strafeSmooth.GetInteger() );
	float strafeMx = 0.0f;
	float strafeMy = 0.0f;
	for ( int i = 0; i < smooth; i++ ) {
		strafeMx += history[ ( historyCounter - i ) & 7 ][ 0 ];
		strafeMy += history[ ( historyCounter - i ) & 7 ][ 1 ];
	}
	strafeMx /= smooth;
	strafeMy /= smooth;
	historyCounter++;

	mx *= sensitivity.GetFloat();
	my *= sensitivity.GetFloat();

	if ( m_showMouseRate.GetBool() ) {
		Sys_DebugPrintf( "[%3i %3i = %5.1f %5.1f = %5.1f %5.1f] ", mouseDx, mouseDy, mx, my, strafeMx, strafeMy );
	}

	mouseDx = 0;
	mouseDy = 0;
	if ( mx == 0.0f && my == 0.0f && strafeMx == 0.0f && strafeMy == 0.0f ) {
		return;
	}

	if ( ButtonState( UB_STRAFE ) || cmd.buttons.btn.mLookOff ) {
		strafeMx *= m_strafeScale.GetFloat();
		strafeMy *= m_strafeScale.GetFloat();
		const float length = idMath::Sqrt( strafeMx * strafeMx + strafeMy * strafeMy );
		if ( length > KEY_MOVESPEED ) {
			strafeMx *= KEY_MOVESPEED / length;
			strafeMy *= KEY_MOVESPEED / length;
		}
	}

	float yawScale = m_yaw.GetFloat();
	float pitchScale = m_pitch.GetFloat();
	if ( doGameCallback && game != NULL ) {
		game->GetSensitivity( yawScale, pitchScale );
	}

	idVec3 angleDelta = vec3_origin;
	if ( !ButtonState( UB_STRAFE ) ) {
		angleDelta[ YAW ] = -yawScale * mx;
	} else {
		cmd.rightmove = idMath::ClampChar( idMath::Ftoi( cmd.rightmove + strafeMx ) );
	}

	if ( ButtonState( UB_STRAFE ) || cmd.buttons.btn.mLookOff ) {
		cmd.forwardmove = idMath::ClampChar( idMath::Ftoi( cmd.forwardmove - strafeMy ) );
	} else {
		angleDelta[ PITCH ] = pitchScale * my;
	}

	if ( doGameCallback && game != NULL ) {
		game->MouseMove( viewangles, angleDelta );
	}
	viewangles += angleDelta;
}

void idUsercmdGenLocal::ControllerMove( bool doGameCallback ) {
	if ( game == NULL ) {
		return;
	}

	int controllerNumbers[ 4 ];
	const float* controllerAxes[ 4 ];
	int activeControllers = 0;
	sdControllerManager& manager = sys->GetControllerManager();
	for ( int slot = 1; slot <= 4; slot++ ) {
		sdController* controller = manager.GetControllerByJoySlot( slot );
		if ( controller != NULL && controller->GetState() == sdController::CS_OK ) {
			controllerNumbers[ activeControllers ] = slot;
			controllerAxes[ activeControllers ] = controller->GetAxisArray();
			activeControllers++;
		}
	}

	game->ControllerMove( doGameCallback, activeControllers, controllerNumbers, controllerAxes, viewangles, cmd );
}

void idUsercmdGenLocal::JoystickMove() {
}

void idUsercmdGenLocal::CmdButtons() {
	cmd.buttons.btnValue = 0;
	cmd.buttons.btn.attack = ButtonState( UB_ATTACK );
	cmd.buttons.btn.run = !toggledRun.On();
	cmd.buttons.btn.modeSwitch = ButtonState( UB_MODESWITCH );
	cmd.buttons.btn.mLookOff = ButtonState( UB_MLOOK ) == in_freeLook.GetBool();
	cmd.buttons.btn.sprint = toggledSprint.On();
	cmd.buttons.btn.activate = ButtonState( UB_ACTIVATE );
	cmd.buttons.btn.altAttack = ButtonState( UB_ALTATTACK );
	cmd.buttons.btn.leanLeft = ButtonState( UB_LEANLEFT );
	cmd.buttons.btn.leanRight = ButtonState( UB_LEANRIGHT );
	cmd.buttons.btn.tophat = ButtonState( UB_TOPHAT );

	*reinterpret_cast< byte* >( &cmd.clientButtons ) = 0;
	cmd.clientButtons.showScores = ButtonState( UB_SHOWSCORES );
	cmd.clientButtons.voice = ButtonState( UB_VOICE );
	cmd.clientButtons.teamVoice = ButtonState( UB_TEAMVOICE );
	cmd.clientButtons.fireteamVoice = ButtonState( UB_FIRETEAMVOICE );
}

void idUsercmdGenLocal::InitCurrent() {
	memset( &cmd, 0, sizeof( cmd ) );
	cmd.flags = static_cast< byte >( flags );
	cmd.impulse = static_cast< signed char >( impulse );
	if ( scheduledImpulse != -1 ) {
		cmd.flags ^= UCF_IMPULSE_SEQUENCE;
		cmd.impulse = static_cast< signed char >( scheduledImpulse );
	}
	cmd.buttons.btn.run = true;
	cmd.buttons.btn.mLookOff = !in_freeLook.GetBool();
}

void idUsercmdGenLocal::MakeCurrent( bool doGameCallback ) {
	const float oldPitch = viewangles[ PITCH ];

	toggledRun.SetKeyState( ButtonState( UB_SPEED ), in_toggleRun.GetBool() );
	toggledSprint.SetKeyState( ButtonState( UB_SPRINT ), in_toggleSprint.GetBool() );

	AdjustAngles();
	CmdButtons();
	KeyMove();
	MouseMove( doGameCallback );
	ControllerMove( doGameCallback );

	if ( viewangles[ PITCH ] - oldPitch > 90.0f ) {
		viewangles[ PITCH ] = oldPitch + 90.0f;
	} else if ( oldPitch - viewangles[ PITCH ] > 90.0f ) {
		viewangles[ PITCH ] = oldPitch - 90.0f;
	}

	for ( int i = 0; i < 3; i++ ) {
		cmd.angles[ i ] = ANGLE2SHORT( viewangles[ i ] );
	}

	flags = cmd.flags;
	impulse = cmd.impulse;
	scheduledImpulse = -1;

	if ( doGameCallback && game != NULL ) {
		game->UsercommandCallback( cmd );
		for ( int i = 0; i < 3; i++ ) {
			viewangles[ i ] = SHORT2ANGLE( cmd.angles[ i ] );
		}
	}
}

void idUsercmdGenLocal::Mouse( bool postEvents ) {
	idMouse& mouse = sys->Mouse();
	mouse.PollInputEvents( postEvents );
	mouse.EndInputEvents();
}

void idUsercmdGenLocal::Keyboard( bool postEvents ) {
	idKeyboard& keyboard = sys->Keyboard();
	keyboard.PollInputEvents( postEvents );
	keyboard.EndInputEvents();
}

void idUsercmdGenLocal::Joystick() {
}

void idUsercmdGenLocal::Controllers( bool postEvents ) {
	sdControllerManager& manager = sys->GetControllerManager();
	for ( int i = 0; i < manager.GetMaxControllers(); i++ ) {
		sdController& controller = manager.GetController( i );
		const int eventCount = controller.PollInputEvents();
		if ( postEvents ) {
			for ( int eventIndex = 0; eventIndex < eventCount; eventIndex++ ) {
				int action;
				int value;
				if ( !controller.ReturnInputEvent( eventIndex, action, value ) ) {
					continue;
				}
				if ( action >= C_BUTTON1 && action <= C_BUTTON_MAX ) {
					sys->QueEvent( SE_CONTROLLER_BUTTON, action, ( controller.GetHash() << 1 ) | ( value > 0 ), 0, NULL );
				} else if ( action >= C_AXIS1 && action <= C_AXIS_MAX ) {
					controller.SetAxis( action - C_AXIS1, value / 32767.0f );
				}
			}
		}
		controller.EndInputEvents();
	}
}

void idUsercmdGenLocal::UsercmdInterrupt() {
	if ( !initialized ) {
		return;
	}

#ifdef _WIN32
	sdScopedLock< true > inputLock( IN_GetPollLock() );
#endif

	// Retained for the public interface. Platform polling is performed by the
	// input worker; usercmd state must only be assembled on the main thread.
	InitCurrent();
	MakeCurrent( true );
	buffered[ ( com_ticNumber + 1 ) & ( MAX_BUFFERED_USERCMD - 1 ) ] = cmd;
}

usercmd_t idUsercmdGenLocal::GetDirectUsercmd( bool doGameCallback ) {
#ifdef _WIN32
	sdScopedLock< true > inputLock( IN_GetPollLock() );
#endif

	InitCurrent();
	MakeCurrent( doGameCallback );
	cmd.duplicateCount = 0;
	return cmd;
}

sdKeyCommand* idUsercmdGenLocal::Translate( const idKey& key ) {
	return game != NULL ? game->Translate( key ) : NULL;
}

void idUsercmdGenLocal::HandleKey( idKey& key, bool down ) {
	if ( key.IsDown() == down || !down ) {
		return;
	}

	sdKeyCommand* command = Translate( key );
	if ( command != NULL ) {
		key.SetActiveCommand( command );
		HandleCommand( command, true );
	}
}

void idUsercmdGenLocal::HandleCommand( const sdKeyCommand* command, bool down ) {
	if ( command == NULL ) {
		return;
	}

	const int action = command->GetAction();
	switch ( command->GetType() ) {
		case B_BUTTON:
			if ( action < 0 || action >= UB_MAX_BUTTONS ) {
				return;
			}
			if ( down ) {
				buttonState[ action ]++;
			} else if ( --buttonState[ action ] < 0 ) {
				buttonState[ action ] = 0;
			}
			break;

		case B_IMPULSE:
			if ( down ) {
				scheduledImpulse = action;
			}
			break;

		case B_LOCAL_IMPULSE:
			if ( game != NULL ) {
				game->HandleLocalImpulse( action, down );
			}
			break;

		case B_COMMAND:
			if ( down ) {
				idKeyInput::ExecKeyBinding( command );
			}
			break;
	}
}

bool idUsercmdGenLocal::ProcessEvent( const sdSysEvent& event ) {
	if ( event.IsMouseEvent() ) {
		mouseDx += idMath::Ftoi( event.GetXCoord() );
		mouseDy += idMath::Ftoi( event.GetYCoord() );
		return true;
	}

	bool down = false;
	idKey* key = keyInputManager->GetKeyForEvent( event, down );
	if ( key == NULL ) {
		return false;
	}

	HandleKey( *key, down );
	return true;
}
