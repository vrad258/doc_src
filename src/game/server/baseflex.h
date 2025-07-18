//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BASEFLEX_H
#define BASEFLEX_H
#ifdef _WIN32
#pragma once
#endif


#include "BaseAnimatingOverlay.h"
#include "utlvector.h"
#include "utlrbtree.h"
#include "sceneentity_shared.h"

struct flexsettinghdr_t;
struct flexsetting_t;
#ifndef NEW_RESPONSE_SYSTEM
class AI_Response;
#endif

//-----------------------------------------------------------------------------
// Purpose:  A .vfe referenced by a scene during .vcd playback
//-----------------------------------------------------------------------------
class CFlexSceneFile
{
public:
	enum
	{
		MAX_FLEX_FILENAME = 128,
	};

	char			filename[ MAX_FLEX_FILENAME ];
	void			*buffer;
};

//-----------------------------------------------------------------------------
// Purpose: Animated characters who have vertex flex capability (e.g., facial expressions)
//-----------------------------------------------------------------------------
class CBaseFlex : public CBaseAnimatingOverlay
{
	DECLARE_CLASS( CBaseFlex, CBaseAnimatingOverlay );
public:
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	DECLARE_PREDICTABLE();
	DECLARE_ENT_SCRIPTDESC();

	// Construction
						CBaseFlex( void );
						~CBaseFlex( void );

	virtual void		SetModel( const char *szModelName );

	void Blink( );

	virtual	void		SetViewtarget( const Vector &viewtarget );
	const Vector		&GetViewtarget( void ) const;

	void				SetFlexWeight( char *szName, float value );
	void				SetFlexWeight( LocalFlexController_t index, float value );
	float				GetFlexWeight( char *szName );
	float				GetFlexWeight( LocalFlexController_t index );

	// Look up flex controller index by global name
	LocalFlexController_t	FindFlexController( const char *szName );
	void				EnsureTranslations( const flexsettinghdr_t *pSettinghdr );

	// Keep track of what scenes are being played
	void				StartChoreoScene( CChoreoScene *scene );
	void				RemoveChoreoScene( CChoreoScene *scene, bool canceled = false );

	// Start the specifics of an scene event
#ifdef MAPBASE
	virtual bool		StartSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event, CChoreoActor *actor, CBaseEntity *pTarget, CSceneEntity *pSceneEnt );
	virtual bool		StartSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event, CChoreoActor *actor, CBaseEntity *pTarget ) { return StartSceneEvent( info, scene, event, actor, pTarget, NULL ); }
#else
	virtual bool		StartSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event, CChoreoActor *actor, CBaseEntity *pTarget );
#endif

	// Manipulation of events for the object
	// Should be called by think function to process all scene events
	// The default implementation resets m_flexWeight array and calls
	//  AddSceneEvents
	virtual void		ProcessSceneEvents( void );

	// Assumes m_flexWeight array has been set up, this adds the actual currently playing
	//  expressions to the flex weights and adds other scene events as needed
	virtual	bool		ProcessSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event );

	// Remove all playing events
	void				ClearSceneEvents( CChoreoScene *scene, bool canceled );

	// Stop specifics of event
	virtual	bool		ClearSceneEvent( CSceneEventInfo *info, bool fastKill, bool canceled );

	// Add the event to the queue for this actor
#ifdef MAPBASE
	void				AddSceneEvent( CChoreoScene *scene, CChoreoEvent *event, CBaseEntity *pTarget = NULL, CSceneEntity *pSceneEnt = NULL );
#else
	void				AddSceneEvent( CChoreoScene *scene, CChoreoEvent *event, CBaseEntity *pTarget = NULL );
#endif

	// Remove the event from the queue for this actor
	void				RemoveSceneEvent( CChoreoScene *scene, CChoreoEvent *event, bool fastKill );

	// Checks to see if the event should be considered "completed"
	bool				CheckSceneEvent( float currenttime, CChoreoScene *scene, CChoreoEvent *event );

	// Checks to see if a event should be considered "completed"
	virtual bool		CheckSceneEventCompletion( CSceneEventInfo *info, float currenttime, CChoreoScene *scene, CChoreoEvent *event );

	// Finds the layer priority of the current scene
	int					GetScenePriority( CChoreoScene *scene );

	// Returns true if the actor is not currently in a scene OR if the actor
	//  is in a scene, but a PERMIT_RESPONSES event is active and the permit time
	//  period has enough time remaining to handle the response in full.
	bool				PermitResponse( float response_length );

	// Set response end time (0 to clear response blocking)
	void				SetPermitResponse( float endtime );

	void				SentenceStop( void ) { EmitSound( "AI_BaseNPC.SentenceStop" ); }

	virtual float		PlayScene( const char *pszScene, float flDelay = 0.0f, AI_Response *response = NULL, IRecipientFilter *filter = NULL );
#ifdef MAPBASE
	virtual float		PlayChoreoSentenceScene( const char *pszSentenceName, float flDelay = 0.0f, AI_Response *response = NULL, IRecipientFilter *filter = NULL );
	virtual float		PlayAutoGeneratedSoundScene( const char *soundname, float flDelay = 0.0f, AI_Response *response = NULL, IRecipientFilter *filter = NULL );
#else
	virtual float		PlayAutoGeneratedSoundScene( const char *soundname );
#endif

	// Returns the script instance of the scene entity associated with our oldest ("top level") scene event
	virtual HSCRIPT		ScriptGetOldestScene( void );
	virtual HSCRIPT		ScriptGetSceneByIndex( int index );

	virtual int			GetSpecialDSP( void ) { return 0; }

#ifdef MAPBASE
	bool				DispatchGetGameTextSpeechParams( hudtextparms_t &params );
	virtual bool		GetGameTextSpeechParams( hudtextparms_t &params ) { return true; }
#endif

protected:
	// For handling .vfe files
	// Search list, or add if not in list
	const void			*FindSceneFile( const char *filename );

	// Find setting by name
	const flexsetting_t *FindNamedSetting( const flexsettinghdr_t *pSettinghdr, const char *expr );

	// Called at the lowest level to actually apply an expression
	void				AddFlexSetting( const char *expr, float scale, const flexsettinghdr_t *pSettinghdr, bool newexpression );

	// Called at the lowest level to actually apply a flex animation
	void				AddFlexAnimation( CSceneEventInfo *info );

	bool				HasSceneEvents() const;
	bool				IsRunningSceneMoveToEvent();

	LocalFlexController_t	FlexControllerLocalToGlobal( const flexsettinghdr_t *pSettinghdr, int key );

public:
#if 0
	// Returns the script instance of the scene entity associated with our oldest ("top level") scene event
	HSCRIPT		ScriptGetOldestScene( void );
	HSCRIPT		ScriptGetSceneByIndex( int index );
#endif
	float		ScriptPlayScene( const char *pszScene, float flDelay = 0.0f );

private:
	// Starting various expression types 

	bool RequestStartSequenceSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event, CChoreoActor *actor, CBaseEntity *pTarget );
	bool RequestStartGestureSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event, CChoreoActor *actor, CBaseEntity *pTarget );

	bool HandleStartSequenceSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event, CChoreoActor *actor );
	bool HandleStartGestureSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event, CChoreoActor *actor );
	bool StartFacingSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event, CChoreoActor *actor, CBaseEntity *pTarget );
	bool StartMoveToSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event, CChoreoActor *actor, CBaseEntity *pTarget );

	// Processing various expression types
	bool ProcessFlexAnimationSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event );
	bool ProcessFlexSettingSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event );
	bool ProcessSequenceSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event );
	bool ProcessGestureSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event );
	bool ProcessFacingSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event );
	bool ProcessMoveToSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event );
	bool ProcessLookAtSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event );

	// Set playing the scene sequence
public:
	bool EnterSceneSequence( CChoreoScene *scene, CChoreoEvent *event, bool bRestart = false );
private:
	bool ExitSceneSequence( void );

private:
	CNetworkArray( float, m_flexWeight, MAXSTUDIOFLEXCTRL );	// indexed by model local flexcontroller

	// Vector from actor to eye target
	CNetworkVector( m_viewtarget );

	// Blink state
	CNetworkVar( int, m_blinktoggle );

	// Array of active SceneEvents, in order oldest to newest
	CUtlVector < CSceneEventInfo >		m_SceneEvents;

	// Mapping for each loaded scene file used by this actor
	struct FS_LocalToGlobal_t
	{
		explicit FS_LocalToGlobal_t() :
			m_Key( nullptr ),
			m_nCount( 0 ),
			m_Mapping( nullptr )
		{
		}

		explicit FS_LocalToGlobal_t( const flexsettinghdr_t *key ) :
			m_Key( key ),
			m_nCount( 0 ),
			m_Mapping( nullptr )
		{
		}		

		void SetCount( int count )
		{
			Assert( !m_Mapping );
			Assert( count > 0 );
			m_nCount = count;
			m_Mapping = new LocalFlexController_t[ m_nCount ];
			Q_memset( m_Mapping, 0, m_nCount * sizeof( int ) );
		}

		FS_LocalToGlobal_t( const FS_LocalToGlobal_t& src )
		{
			m_Key = src.m_Key;
			m_Mapping = new LocalFlexController_t[ src.m_nCount ];
			Q_memcpy( m_Mapping, src.m_Mapping, src.m_nCount * sizeof( int ) );

			m_nCount = src.m_nCount;
		}

		~FS_LocalToGlobal_t()
		{
			delete m_Mapping;
			m_nCount = 0;
			m_Mapping = 0;
		}

		const flexsettinghdr_t	*m_Key = nullptr;
		int						m_nCount = 0;
		LocalFlexController_t	*m_Mapping = nullptr;
	};

	static bool FlexSettingLessFunc( const FS_LocalToGlobal_t& lhs, const FS_LocalToGlobal_t& rhs );

	CUtlRBTree< FS_LocalToGlobal_t, unsigned short > m_LocalToGlobal;

	// The NPC is in a scene, but another .vcd (such as a short wave to say in response to the player doing something )
	//  can be layered on top of this actor (assuming duration matches, etc.
	float				m_flAllowResponsesEndTime;

	// List of actively playing scenes
	CUtlVector < CChoreoScene * >		m_ActiveChoreoScenes;
	bool				m_bUpdateLayerPriorities;

public:
	bool				IsSuppressedFlexAnimation( CSceneEventInfo *info );

private:
	// last time a foreground flex animation was played
	float				m_flLastFlexAnimationTime;


public:
	void DoBodyLean( void );

	virtual void Teleport( const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity );


#ifdef HL2_DLL
	Vector m_vecPrevOrigin;
	Vector m_vecPrevVelocity;
	CNetworkVector( m_vecLean );
	CNetworkVector( m_vecShift );
#endif
};


//-----------------------------------------------------------------------------
// For toggling blinking
//-----------------------------------------------------------------------------
inline void CBaseFlex::Blink()
{
	m_blinktoggle = !m_blinktoggle;
}

//-----------------------------------------------------------------------------
// Do we have active expressions?
//-----------------------------------------------------------------------------
inline bool CBaseFlex::HasSceneEvents() const
{
	return m_SceneEvents.Count() != 0;
}


//-----------------------------------------------------------------------------
// Other inlines
//-----------------------------------------------------------------------------
inline const Vector &CBaseFlex::GetViewtarget( ) const
{
	return m_viewtarget.Get();	// bah
}

inline void CBaseFlex::SetFlexWeight( char *szName, float value )
{
	SetFlexWeight( FindFlexController( szName ), value );
}

inline float CBaseFlex::GetFlexWeight( char *szName )
{
	return GetFlexWeight( FindFlexController( szName ) );
}


EXTERN_SEND_TABLE(DT_BaseFlex);


#endif // BASEFLEX_H
