//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"

#include "ai_speech.h"

#include "game.h"
#include "engine/IEngineSound.h"
#include "KeyValues.h"
#include "ai_basenpc.h"
#include "AI_Criteria.h"
#include "isaverestore.h"
#include "sceneentity.h"
#ifdef MAPBASE
#include "ai_squad.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>

#define DEBUG_AISPEECH 1
#ifdef DEBUG_AISPEECH
ConVar ai_debug_speech( "ai_debug_speech", "0" );
#define DebuggingSpeech() ai_debug_speech.GetBool()
#else
inline void SpeechMsg( ... ) {}
#define DebuggingSpeech() (false)
#endif

extern ConVar rr_debugresponses;

//-----------------------------------------------------------------------------

CAI_TimedSemaphore g_AIFriendliesTalkSemaphore;
CAI_TimedSemaphore g_AIFoesTalkSemaphore;

ConceptHistory_t::~ConceptHistory_t()
{
	delete response;
	response = NULL;
}

ConceptHistory_t::ConceptHistory_t( const ConceptHistory_t& src )
{
	timeSpoken = src.timeSpoken;
	response = NULL;
	if ( src.response )
	{
		response = new AI_Response( *src.response );
	}
}

ConceptHistory_t& ConceptHistory_t::operator =( const ConceptHistory_t& src )
{
	if ( this != &src )
	{
		timeSpoken = src.timeSpoken;

		delete response;
		response = NULL;
		if ( src.response )
		{
			response = new AI_Response( *src.response );
		}
	}

	return *this;
}

BEGIN_SIMPLE_DATADESC( ConceptHistory_t )
	DEFINE_FIELD( timeSpoken,	FIELD_TIME ),  // Relative to server time
	// DEFINE_EMBEDDED( response,	FIELD_??? ),	// This is manually saved/restored by the ConceptHistory saverestore ops below
END_DATADESC()

class CConceptHistoriesDataOps : public CDefSaveRestoreOps
{
public:
	virtual void Save( const SaveRestoreFieldInfo_t &fieldInfo, ISave *pSave )
	{
		CUtlDict< ConceptHistory_t, int > *ch = ((CUtlDict< ConceptHistory_t, int > *)fieldInfo.pField);
		int count = ch->Count();
		pSave->WriteInt( &count );
		for ( int i = 0 ; i < count; i++ )
		{
			ConceptHistory_t *pHistory = &(*ch)[ i ];

			pSave->StartBlock();
			{
				// Write element name
				pSave->WriteString( ch->GetElementName( i ) );

				// Write data
				pSave->WriteAll( pHistory );

				// Write response blob
				bool hasresponse = !!pHistory->response;
				pSave->WriteBool( &hasresponse );
				if ( hasresponse )
				{
					pSave->WriteAll( pHistory->response );
				}
				// TODO: Could blat out pHistory->criteria pointer here, if it's needed
			}
			pSave->EndBlock();
		}
	}
	
	virtual void Restore( const SaveRestoreFieldInfo_t &fieldInfo, IRestore *pRestore )
	{
		CUtlDict< ConceptHistory_t, int > *ch = ((CUtlDict< ConceptHistory_t, int > *)fieldInfo.pField);

		int count = pRestore->ReadInt();
		Assert( count >= 0 );
		for ( int i = 0 ; i < count; i++ )
		{
			char conceptname[ 512 ];
			conceptname[ 0 ] = 0;

			ConceptHistory_t history;

			pRestore->StartBlock();
			{
				pRestore->ReadString( conceptname, sizeof( conceptname ), 0 );

				pRestore->ReadAll( &history );

				bool hasresponse = false;
				pRestore->ReadBool( &hasresponse );
				if ( hasresponse )
				{
					history.response = new AI_Response();
					pRestore->ReadAll( history.response );
				}
			}

			pRestore->EndBlock();

			// TODO: Could restore pHistory->criteria pointer here, if it's needed

			// Add to utldict
			if ( conceptname[0] != 0 )
			{
				ch->Insert( conceptname, history );
			}
			else
			{
				Assert( !"Error restoring ConceptHistory_t, discarding!" );
			}
		}
	}

	virtual void MakeEmpty( const SaveRestoreFieldInfo_t &fieldInfo )
	{
	}

	virtual bool IsEmpty( const SaveRestoreFieldInfo_t &fieldInfo )
	{
		CUtlDict< ConceptHistory_t, int > *ch = ((CUtlDict< ConceptHistory_t, int > *)fieldInfo.pField);
		return ch->Count() == 0 ? true : false;
	}
};

CConceptHistoriesDataOps g_ConceptHistoriesSaveDataOps;

//-----------------------------------------------------------------------------
//
// CLASS: CAI_Expresser
//

BEGIN_SIMPLE_DATADESC( CAI_Expresser )
	//									m_pSink		(reconnected on load)
//	DEFINE_FIELD( m_pOuter, CHandle < CBaseFlex > ),
	DEFINE_CUSTOM_FIELD( m_ConceptHistories,	&g_ConceptHistoriesSaveDataOps ),
	DEFINE_FIELD(		m_flStopTalkTime,		FIELD_TIME		),
	DEFINE_FIELD(		m_flStopTalkTimeWithoutDelay, FIELD_TIME		),
	DEFINE_FIELD(		m_flBlockedTalkTime, 	FIELD_TIME		),
	DEFINE_FIELD(		m_voicePitch,			FIELD_INTEGER	),
	DEFINE_FIELD(		m_flLastTimeAcceptedSpeak, 	FIELD_TIME		),
END_DATADESC()

#ifdef MAPBASE_VSCRIPT
BEGIN_SCRIPTDESC_ROOT( CAI_Expresser, "Expresser class for complex speech." )

	DEFINE_SCRIPTFUNC( IsSpeaking, "Check if the actor is speaking." )
	DEFINE_SCRIPTFUNC( CanSpeak, "Check if the actor can speak." )
	DEFINE_SCRIPTFUNC( BlockSpeechUntil, "Block speech for a certain amount of time. This is stored in curtime." )
	DEFINE_SCRIPTFUNC( ForceNotSpeaking, "If the actor is speaking, force the system to recognize them as not speaking." )

	DEFINE_SCRIPTFUNC( GetVoicePitch, "Get the actor's voice pitch. Used in sentences." )
	DEFINE_SCRIPTFUNC( SetVoicePitch, "Set the actor's voice pitch. Used in sentences." )

	DEFINE_SCRIPTFUNC_NAMED( ScriptSpeakRawScene, "SpeakRawScene", "Speak a raw, instanced VCD scene as though it were played through the Response System. Return whether the scene successfully plays." )
	DEFINE_SCRIPTFUNC_NAMED( ScriptSpeakAutoGeneratedScene, "SpeakAutoGeneratedScene", "Speak an automatically generated, instanced VCD scene for this sound as though it were played through the Response System. Return whether the scene successfully plays." )
	DEFINE_SCRIPTFUNC_NAMED( ScriptSpeakRawSentence, "SpeakRawSentence", "Speak a raw sentence as though it were played through the Response System. Return the sentence's index; -1 if not successfully played." )
	DEFINE_SCRIPTFUNC_NAMED( ScriptSpeak, "Speak", "Speak a response concept with the specified modifiers." )

END_SCRIPTDESC();
#endif

//-------------------------------------

bool CAI_Expresser::SemaphoreIsAvailable( CBaseEntity *pTalker )
{
	if ( !GetSink()->UseSemaphore() )
		return true;

	CAI_TimedSemaphore *pSemaphore = GetMySpeechSemaphore( pTalker->MyNPCPointer() );
	return (pSemaphore ? pSemaphore->IsAvailable( pTalker ) : true);
}

//-------------------------------------

float CAI_Expresser::GetSemaphoreAvailableTime( CBaseEntity *pTalker )
{
	CAI_TimedSemaphore *pSemaphore = GetMySpeechSemaphore( pTalker->MyNPCPointer() );
	return (pSemaphore ? pSemaphore->GetReleaseTime() : 0);
}

//-------------------------------------

int CAI_Expresser::GetVoicePitch() const
{
	return m_voicePitch + random->RandomInt(0,3);
}

#ifdef DEBUG
static int g_nExpressers;
#endif

CAI_Expresser::CAI_Expresser( CBaseFlex *pOuter )
 :	m_pOuter( pOuter ),
	m_pSink( NULL ),
	m_flStopTalkTime( 0 ),
	m_flLastTimeAcceptedSpeak( 0 ),
	m_flBlockedTalkTime( 0 ),
	m_flStopTalkTimeWithoutDelay( 0 ),
	m_voicePitch( 100 )
{
#ifdef DEBUG
	g_nExpressers++;
#endif
}

CAI_Expresser::~CAI_Expresser()
{
	m_ConceptHistories.Purge();

	CAI_TimedSemaphore *pSemaphore = GetMySpeechSemaphore( GetOuter() );
	if ( pSemaphore )
	{
		if ( pSemaphore->GetOwner() == GetOuter() )
			pSemaphore->Release();

#ifdef DEBUG
		g_nExpressers--;
		if ( g_nExpressers == 0 && pSemaphore->GetOwner() )
			DevMsg( 2, "Speech semaphore being held by non-talker entity\n" );
#endif
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CAI_Expresser::TestAllResponses()
{
	IResponseSystem *pResponseSystem = GetOuter()->GetResponseSystem();
	if ( pResponseSystem )
	{
		CUtlVector<AI_Response *> responses;

		pResponseSystem->GetAllResponses( &responses );
		for ( int i = 0; i < responses.Count(); i++ )
		{
			const char *szResponse = responses[i]->GetResponsePtr();

			Msg( "Response: %s\n", szResponse );
			SpeakDispatchResponse( "", *responses[i] );
		}
	}
}

//-----------------------------------------------------------------------------

static const int LEN_SPECIFIC_SCENE_MODIFIER = strlen( AI_SPECIFIC_SCENE_MODIFIER );

//-----------------------------------------------------------------------------
// Purpose: Searches for a possible response
// Input  : concept - 
//			NULL - 
// Output : AI_Response
//-----------------------------------------------------------------------------
bool CAI_Expresser::SpeakFindResponse( AI_Response &outResponse, AIConcept_t concept, const char *modifiers /*= NULL*/ )
{
#ifdef MAPBASE
	AI_CriteriaSet set;

	if (modifiers)
	{
		MergeModifiers(set, modifiers);
	}

	// Now return the code in the new function.
	return SpeakFindResponse(concept, set);
#else
	IResponseSystem *rs = GetOuter()->GetResponseSystem();
	if ( !rs )
	{
		Assert( !"No response system installed for CAI_Expresser::GetOuter()!!!" );
		return false;
	}

	AI_CriteriaSet set;
	// Always include the concept name
	set.AppendCriteria( "concept", concept, CONCEPT_WEIGHT );

	// Always include any optional modifiers
	if ( modifiers )
	{
		char copy_modifiers[ 255 ];
		const char *pCopy;
		char key[ 128 ] = { 0 };
		char value[ 128 ] = { 0 };

		Q_strncpy( copy_modifiers, modifiers, sizeof( copy_modifiers ) );
		pCopy = copy_modifiers;

		while( pCopy )
		{
			pCopy = SplitContext( pCopy, key, sizeof( key ), value, sizeof( value ), NULL );

			if( *key && *value )
			{
				set.AppendCriteria( key, value, CONCEPT_WEIGHT );
			}
		}
	}

	// Let our outer fill in most match criteria
	GetOuter()->ModifyOrAppendCriteria( set );

	// Append local player criteria to set, but not if this is a player doing the talking
	if ( !GetOuter()->IsPlayer() )
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex( 1 );
		if( pPlayer )
			pPlayer->ModifyOrAppendPlayerCriteria( set );
	}

#ifdef MAPBASE
	GetOuter()->ReAppendContextCriteria( set );
#endif

	// Now that we have a criteria set, ask for a suitable response
	bool found = rs->FindBestResponse( set, outResponse, this );

	if ( rr_debugresponses.GetInt() == 3 )
	{
		if ( ( GetOuter()->MyNPCPointer() && GetOuter()->m_debugOverlays & OVERLAY_NPC_SELECTED_BIT ) || GetOuter()->IsPlayer() )
		{
			const char *pszName = GetOuter()->IsPlayer() ?
									((CBasePlayer*)GetOuter())->GetPlayerName() : GetOuter()->GetDebugName();

			if ( found )
			{
				const char *szReponse = outResponse.GetResponsePtr();
				Warning( "RESPONSERULES: %s spoke '%s'. Found response '%s'.\n", pszName, concept, szReponse );
			}
			else
			{
				Warning( "RESPONSERULES: %s spoke '%s'. Found no matching response.\n", pszName, concept );
			}
		}
	}

	if ( !found )
		return false;

	const char *szReponse = outResponse.GetResponsePtr();
	if ( !szReponse[0] )
		return false;

	if ( ( outResponse.GetOdds() < 100 ) && ( random->RandomInt( 1, 100 ) <= outResponse.GetOdds() ) )
		return false;

	return true;
#endif
}

#ifdef MAPBASE
//-----------------------------------------------------------------------------
// Purpose: Merges modifiers with set.
//-----------------------------------------------------------------------------
void CAI_Expresser::MergeModifiers( AI_CriteriaSet& set, const char *modifiers )
{
	char copy_modifiers[ 255 ];
	const char *pCopy;
	char key[ 128 ] = { 0 };
	char value[ 128 ] = { 0 };

	Q_strncpy( copy_modifiers, modifiers, sizeof( copy_modifiers ) );
	pCopy = copy_modifiers;

	while( pCopy )
	{
		pCopy = SplitContext( pCopy, key, sizeof( key ), value, sizeof( value ), NULL );

		if( *key && *value )
		{
			set.AppendCriteria( key, value, CONCEPT_WEIGHT );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Searches for a possible response, takes an AI_CriteriaSet instead.
// Input  : concept - 
//			NULL - 
// Output : AI_Response
//-----------------------------------------------------------------------------
AI_Response *CAI_Expresser::SpeakFindResponse( AIConcept_t concept, const AI_CriteriaSet &modifiers )
{
	IResponseSystem *rs = GetOuter()->GetResponseSystem();
	if ( !rs )
	{
		Assert( !"No response system installed for CAI_Expresser::GetOuter()!!!" );
		return NULL;
	}

	AI_CriteriaSet set;
	// Always include the concept name
	set.AppendCriteria( "concept", concept, CONCEPT_WEIGHT );

	// Tier 1: Criteria
	// Let our outer fill in most match criteria
	GetOuter()->ModifyOrAppendCriteria( set );

	// Append local player criteria to set, but not if this is a player doing the talking
	if ( !GetOuter()->IsPlayer() )
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex( 1 );
		if( pPlayer )
			pPlayer->ModifyOrAppendPlayerCriteria( set );
	}

	// Tier 2: Modifiers
	set.MergeSet(modifiers);

	// Tier 3: Contexts
	GetOuter()->ReAppendContextCriteria( set );

	// Now that we have a criteria set, ask for a suitable response
	AI_Response *result = new AI_Response;
	Assert( result && "new AI_Response: Returned a NULL AI_Response!" );
	bool found = rs->FindBestResponse( set, *result, this );

	if ( rr_debugresponses.GetInt() == 3 )
	{
		if ( ( GetOuter()->MyNPCPointer() && GetOuter()->m_debugOverlays & OVERLAY_NPC_SELECTED_BIT ) || GetOuter()->IsPlayer() )
		{
			const char *pszName;
			if ( GetOuter()->IsPlayer() )
			{
				pszName = ((CBasePlayer*)GetOuter())->GetPlayerName();
			}
			else
			{
				pszName = GetOuter()->GetDebugName();
			}

			if ( found )
			{
				char response[ 256 ];
				result->GetResponse( response, sizeof( response ) );

				Warning( "RESPONSERULES: %s spoke '%s'. Found response '%s'.\n", pszName, concept, response );
			}
			else
			{
				Warning( "RESPONSERULES: %s spoke '%s'. Found no matching response.\n", pszName, concept );
			}
		}
	}

	if ( !found )
	{
		//Assert( !"rs->FindBestResponse: Returned a NULL AI_Response!" );
		delete result;
		return NULL;
	}

	char response[ 256 ];
	result->GetResponse( response, sizeof( response ) );

	if ( !response[0] )
	{
		delete result;
		return NULL;
	}

	if ( result->GetOdds() < 100 && random->RandomInt( 1, 100 ) <= result->GetOdds() )
	{
		delete result;
		return NULL;
	}

	return result;
}
#endif

//-----------------------------------------------------------------------------
// Purpose: Dispatches the result
// Input  : *response - 
//-----------------------------------------------------------------------------
#ifdef MAPBASE
bool CAI_Expresser::SpeakDispatchResponse( AIConcept_t concept, AI_Response &result, IRecipientFilter *filter, const AI_CriteriaSet *modifiers )
#else
bool CAI_Expresser::SpeakDispatchResponse( AIConcept_t concept, AI_Response &result, IRecipientFilter *filter /* = NULL */ )
#endif
{
#ifdef MAPBASE
	if (response[0] == '$')
	{
		const char *context = response + 1;
		const char *replace = GetOuter()->GetContextValue(context);

		// If we can't find the context, check modifiers
		if (!replace && modifiers)
		{
			for (int i = 0; i < modifiers->GetCount(); i++)
			{
				if (FStrEq(context, modifiers->GetName(i)))
				{
					replace = modifiers->GetValue(i);
					break;
				}
			}
		}

		if (replace)
		{
			CGMsg( 1, CON_GROUP_CHOREO, "Replacing %s with %s...\n", response, replace );
			Q_strncpy(response, replace, sizeof(response));

			// Precache it now because it may not have been precached before
			switch ( result->GetType() )
			{
			case RESPONSE_SPEAK:
				{
					GetOuter()->PrecacheScriptSound( response );
				}
				break;

			case RESPONSE_SCENE:
				{
					// TODO: Gender handling?
					PrecacheInstancedScene( response );
				}
				break;
			}
		}
	}
#endif

	bool spoke = false;
	float delay = response.GetDelay();
	const char *szResponse = response.GetResponsePtr();
	soundlevel_t soundlevel = response.GetSoundLevel();

	if ( IsSpeaking() && concept[0] != 0 )
	{
		CGMsg( 1, CON_GROUP_CHOREO, "SpeakDispatchResponse:  Entity ( %i/%s ) already speaking, forcing '%s'\n", GetOuter()->entindex(), STRING( GetOuter()->GetEntityName() ), concept );

		// Tracker 15911:  Can break the game if we stop an imported map placed lcs here, so only
		//  cancel actor out of instanced scripted scenes.  ywb
		RemoveActorFromScriptedScenes( GetOuter(), true /*instanced scenes only*/ );
		GetOuter()->SentenceStop();

		if ( IsRunningScriptedScene( GetOuter() ) )
		{
			CGMsg( 1, CON_GROUP_CHOREO, "SpeakDispatchResponse:  Entity ( %i/%s ) refusing to speak due to scene entity, tossing '%s'\n", GetOuter()->entindex(), STRING( GetOuter()->GetEntityName() ), concept );
			return false;
		}
	}

	switch ( response.GetType() )
	{
	default:
	case RESPONSE_NONE:
		break;

	case RESPONSE_SPEAK:
		if ( !response.ShouldntUseScene() )
		{
			// This generates a fake CChoreoScene wrapping the sound.txt name
#ifdef MAPBASE
			spoke = SpeakAutoGeneratedScene( szResponse, delay, result, filter );
#else
			spoke = SpeakAutoGeneratedScene( szResponse, delay );
#endif
		}
		else
		{
			float speakTime = GetResponseDuration( response );
			GetOuter()->EmitSound( szResponse );

			CGMsg( 1, CON_GROUP_CHOREO, "SpeakDispatchResponse:  Entity ( %i/%s ) playing sound '%s'\n", GetOuter()->entindex(), STRING( GetOuter()->GetEntityName() ), response );
			NoteSpeaking( speakTime, delay );
			spoke = true;
		}
		break;

	case RESPONSE_SENTENCE:
		spoke = ( -1 != SpeakRawSentence( szResponse, delay, VOL_NORM, soundlevel ) ) ? true : false;
		break;

	case RESPONSE_SCENE:
		spoke = SpeakRawScene( szResponse, delay, &response, filter );
		break;

	case RESPONSE_RESPONSE:
		// This should have been recursively resolved already
		Assert( 0 );
		break;
	case RESPONSE_PRINT:
		if ( g_pDeveloper->GetInt() > 0 )
		{
			Vector vPrintPos;
			GetOuter()->CollisionProp()->NormalizedToWorldSpace( Vector(0.5,0.5,1.0f), &vPrintPos );
			NDebugOverlay::Text( vPrintPos, szResponse, true, 1.5 );
			spoke = true;
		}
		break;
	}

	if ( spoke )
	{
		m_flLastTimeAcceptedSpeak = gpGlobals->curtime;
		if ( DebuggingSpeech() && g_pDeveloper->GetInt() > 0 && response.GetType() != RESPONSE_PRINT )
		{
			Vector vPrintPos;
			GetOuter()->CollisionProp()->NormalizedToWorldSpace( Vector(0.5,0.5,1.0f), &vPrintPos );
			NDebugOverlay::Text( vPrintPos, CFmtStr( "%s: %s", concept, szResponse ), true, 1.5 );
		}

#ifdef MAPBASE
		if (response.GetContext())
		{
			const char *pszContext = response.GetContext();

			// Check for operators
			char *pOperator = Q_strstr(pszContext, ":")+1;
			if (pOperator && (pOperator[0] == '+' || pOperator[0] == '-' ||
				pOperator[0] == '*' || pOperator[0] == '/'))
			{
				pszContext = ParseApplyContext(pszContext);
			}

			int iContextFlags = response.GetContextFlags();
			if ( iContextFlags & APPLYCONTEXT_SQUAD )
			{
				CAI_BaseNPC *pNPC = GetOuter()->MyNPCPointer();
				if (pNPC && pNPC->GetSquad())
				{
					AISquadIter_t iter;
					CAI_BaseNPC *pSquadmate = pNPC->GetSquad()->GetFirstMember( &iter );
					while ( pSquadmate )
					{
						pSquadmate->AddContext( pszContext );

						pSquadmate = pNPC->GetSquad()->GetNextMember( &iter );
					}
				}
			}
			if ( iContextFlags & APPLYCONTEXT_ENEMY )
			{
				CBaseEntity *pEnemy = GetOuter()->GetEnemy();
				if ( pEnemy )
				{
					pEnemy->AddContext( pszContext );
				}
			}
			if ( iContextFlags & APPLYCONTEXT_WORLD )
			{
				CBaseEntity *pEntity = CBaseEntity::Instance( engine->PEntityOfEntIndex( 0 ) );
				if ( pEntity )
				{
					pEntity->AddContext( pszContext );
				}
			}
			if ( iContextFlags == 0 || iContextFlags & APPLYCONTEXT_SELF )
			{
				GetOuter()->AddContext( pszContext );
			}
		}
#else
		if ( response.IsApplyContextToWorld() )
		{
			CBaseEntity *pEntity = CBaseEntity::Instance( engine->PEntityOfEntIndex( 0 ) );
			if ( pEntity )
			{
				pEntity->AddContext( response.GetContext() );
			}
		}
		else
		{
			GetOuter()->AddContext( response.GetContext() );
		}
#endif

		SetSpokeConcept( concept, &response );
	}

	return spoke;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *response - 
// Output : float
//-----------------------------------------------------------------------------
float CAI_Expresser::GetResponseDuration( AI_Response& response )
{
	const char *szResponse = response.GetResponsePtr();

	switch ( response.GetType() )
	{
	default:
	case RESPONSE_NONE:
		break;

	case RESPONSE_SPEAK:
		return GetOuter()->GetSoundDuration( szResponse, STRING( GetOuter()->GetModelName() ) );

	case RESPONSE_SENTENCE:
		Assert( 0 );
		return 999.0f;

	case RESPONSE_SCENE:
		return GetSceneDuration( szResponse );

	case RESPONSE_RESPONSE:
		// This should have been recursively resolved already
		Assert( 0 );
		break;

	case RESPONSE_PRINT:
		return 1.0;
	}

	return 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose: Placeholder for rules based response system
// Input  : concept - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CAI_Expresser::Speak( AIConcept_t concept, const char *modifiers /*= NULL*/, char *pszOutResponseChosen /* = NULL*/, size_t bufsize /* = 0 */, IRecipientFilter *filter /* = NULL */ )
{
    AI_Response response;
	bool result = SpeakFindResponse( response, concept, modifiers );
	if ( !result )
		return false;

	SpeechMsg( GetOuter(), "%s (%p) spoke %s (%f)\n", STRING(GetOuter()->GetEntityName()), GetOuter(), concept, gpGlobals->curtime );

	bool spoke = SpeakDispatchResponse( concept, response, filter );
	if ( pszOutResponseChosen )
	{
        const char *szResponse = response.GetResponsePtr();
        Q_strncpy( pszOutResponseChosen, szResponse, bufsize );
	}
	
	return spoke;
}

#ifdef MAPBASE
//-----------------------------------------------------------------------------
// Purpose: Uses an AI_CriteriaSet directly instead of using context-format modifier text.
// Input  : concept - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CAI_Expresser::Speak( AIConcept_t concept, const AI_CriteriaSet& modifiers, char *pszOutResponseChosen /* = NULL*/, size_t bufsize /* = 0 */, IRecipientFilter *filter /* = NULL */ )
{
	AI_Response *result = SpeakFindResponse( concept, modifiers );
	if ( !result )
	{
		return false;
	}

	SpeechMsg( GetOuter(), "%s (%p) spoke %s (%f)\n", STRING(GetOuter()->GetEntityName()), GetOuter(), concept, gpGlobals->curtime );

	bool spoke = SpeakDispatchResponse( concept, result, filter, &modifiers );
	if ( pszOutResponseChosen )
	{
		result->GetResponse( pszOutResponseChosen, bufsize );
	}
	
	return spoke;
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CAI_Expresser::SpeakRawScene( const char *pszScene, float delay, AI_Response *response, IRecipientFilter *filter /* = NULL */ )
{
	float sceneLength = GetOuter()->PlayScene( pszScene, delay, response, filter );
	if ( sceneLength > 0 )
	{
		SpeechMsg( GetOuter(), "SpeakRawScene( %s, %f) %f\n", pszScene, delay, sceneLength );

#if defined( HL2_EPISODIC ) || defined( TF_DLL )
		char szInstanceFilename[256];
		GetOuter()->GenderExpandString( pszScene, szInstanceFilename, sizeof( szInstanceFilename ) );
		// Only mark ourselves as speaking if the scene has speech
		if ( GetSceneSpeechCount(szInstanceFilename) > 0 )
		{
			NoteSpeaking( sceneLength, delay );
		}
#else
		NoteSpeaking( sceneLength, delay );
#endif

		return true;
	}
	return false;
}

// This will create a fake .vcd/CChoreoScene to wrap the sound to be played
#ifdef MAPBASE
bool CAI_Expresser::SpeakAutoGeneratedScene( char const *soundname, float delay, AI_Response *response, IRecipientFilter *filter )
#else
bool CAI_Expresser::SpeakAutoGeneratedScene( char const *soundname, float delay )
#endif
{
#ifdef MAPBASE
	float speakTime = GetOuter()->PlayAutoGeneratedSoundScene( soundname, delay, response, filter );
#else
	float speakTime = GetOuter()->PlayAutoGeneratedSoundScene( soundname );
#endif
	if ( speakTime > 0 )
	{
		SpeechMsg( GetOuter(), "SpeakAutoGeneratedScene( %s, %f) %f\n", soundname, delay, speakTime );
		NoteSpeaking( speakTime, delay );
		return true;
	}
	return false;
}

//-------------------------------------

int CAI_Expresser::SpeakRawSentence( const char *pszSentence, float delay, float volume, soundlevel_t soundlevel, CBaseEntity *pListener )
{
	int sentenceIndex = -1;

	if ( !pszSentence )
		return sentenceIndex;

	if ( pszSentence[0] == AI_SP_SPECIFIC_SENTENCE )
	{
		sentenceIndex = SENTENCEG_Lookup( pszSentence );

		if( sentenceIndex == -1 )
		{
			// sentence not found
			return -1;
		}

		CPASAttenuationFilter filter( GetOuter(), soundlevel );
		CBaseEntity::EmitSentenceByIndex( filter, GetOuter()->entindex(), CHAN_VOICE, sentenceIndex, volume, soundlevel, 0, GetVoicePitch());
	}
	else
	{
		sentenceIndex = SENTENCEG_PlayRndSz( GetOuter()->NetworkProp()->edict(), pszSentence, volume, soundlevel, 0, GetVoicePitch() );
	}

	SpeechMsg( GetOuter(), "SpeakRawSentence( %s, %f) %f\n", pszSentence, delay, engine->SentenceLength( sentenceIndex ) );
	NoteSpeaking( engine->SentenceLength( sentenceIndex ), delay );

	return sentenceIndex;
}

//-------------------------------------

void CAI_Expresser::BlockSpeechUntil( float time ) 	
{ 
	SpeechMsg( GetOuter(), "BlockSpeechUntil(%f) %f\n", time, time - gpGlobals->curtime );
	m_flBlockedTalkTime = time; 
}


//-------------------------------------

void CAI_Expresser::NoteSpeaking( float duration, float delay )
{
	duration += delay;
	
	GetSink()->OnStartSpeaking();

	if ( duration <= 0 )
	{
		// no duration :( 
		m_flStopTalkTime = gpGlobals->curtime + 3;
		duration = 0;
	}
	else
	{
		m_flStopTalkTime = gpGlobals->curtime + duration;
	}

	m_flStopTalkTimeWithoutDelay = m_flStopTalkTime - delay;

	SpeechMsg( GetOuter(), "NoteSpeaking( %f, %f ) (stop at %f)\n", duration, delay, m_flStopTalkTime );

	if ( GetSink()->UseSemaphore() )
	{
		CAI_TimedSemaphore *pSemaphore = GetMySpeechSemaphore( GetOuter() );
		if ( pSemaphore )
		{
			pSemaphore->Acquire( duration, GetOuter() );
		}
	}
}

//-------------------------------------

void CAI_Expresser::ForceNotSpeaking( void )
{
	if ( IsSpeaking() )
	{
		m_flStopTalkTime = gpGlobals->curtime;
		m_flStopTalkTimeWithoutDelay = gpGlobals->curtime;

		CAI_TimedSemaphore *pSemaphore = GetMySpeechSemaphore( GetOuter() );
		if ( pSemaphore )
		{
			if ( pSemaphore->GetOwner() == GetOuter() )
			{
				pSemaphore->Release();
			}
		}
	}
}

//-------------------------------------

bool CAI_Expresser::IsSpeaking( void )
{
	if ( m_flStopTalkTime > gpGlobals->curtime )
		SpeechMsg( GetOuter(), "IsSpeaking() %f\n", m_flStopTalkTime - gpGlobals->curtime );

	if ( m_flLastTimeAcceptedSpeak == gpGlobals->curtime ) // only one speak accepted per think
		return true;

	return ( m_flStopTalkTime > gpGlobals->curtime );
}

//-------------------------------------

bool CAI_Expresser::CanSpeak()
{
	if ( m_flLastTimeAcceptedSpeak == gpGlobals->curtime ) // only one speak accepted per think
		return false;

	float timeOk = MAX( m_flStopTalkTime, m_flBlockedTalkTime );
	return ( timeOk <= gpGlobals->curtime );
}

//-----------------------------------------------------------------------------
// Purpose: Returns true if it's ok for this entity to speak after himself.
//			The base CanSpeak() includes the default speech delay, and won't
//			return true until that delay time has passed after finishing the
//			speech. This returns true as soon as the speech finishes.
//-----------------------------------------------------------------------------
bool CAI_Expresser::CanSpeakAfterMyself()
{
	if ( m_flLastTimeAcceptedSpeak == gpGlobals->curtime ) // only one speak accepted per think
		return false;

	float timeOk = MAX( m_flStopTalkTimeWithoutDelay, m_flBlockedTalkTime );
	return ( timeOk <= gpGlobals->curtime );
}

//-------------------------------------
bool CAI_Expresser::CanSpeakConcept( AIConcept_t concept )
{
	// Not in history?
	int iter = m_ConceptHistories.Find( concept );
	if ( iter == m_ConceptHistories.InvalidIndex() )
	{
		return true;
	}

	ConceptHistory_t *history = &m_ConceptHistories[iter];
	Assert( history );

	AI_Response *response = history->response;
	if ( !response )
		return true;

	if ( response->GetSpeakOnce() ) 
		return false;

	float respeakDelay = response->GetRespeakDelay();

	if ( respeakDelay != 0.0f )
	{
		if ( history->timeSpoken != -1 && ( gpGlobals->curtime < history->timeSpoken + respeakDelay ) )
			return false;
	}

	return true;
}

//-------------------------------------

bool CAI_Expresser::SpokeConcept( AIConcept_t concept )
{
	return GetTimeSpokeConcept( concept ) != -1.f;
}

//-------------------------------------

float CAI_Expresser::GetTimeSpokeConcept( AIConcept_t concept )
{
	int iter = m_ConceptHistories.Find( concept );
	if ( iter == m_ConceptHistories.InvalidIndex() ) 
		return -1;
	
	ConceptHistory_t *h = &m_ConceptHistories[iter];
	return h->timeSpoken;
}

//-------------------------------------

void CAI_Expresser::SetSpokeConcept( AIConcept_t concept, AI_Response *response, bool bCallback )
{
	int idx = m_ConceptHistories.Find( concept );
	if ( idx == m_ConceptHistories.InvalidIndex() )
	{
		ConceptHistory_t h;
		h.timeSpoken = gpGlobals->curtime;
		idx = m_ConceptHistories.Insert( concept, h );
	}

	ConceptHistory_t *slot = &m_ConceptHistories[ idx ];

	slot->timeSpoken = gpGlobals->curtime;

	// Update response info
	if ( response )
	{
		delete slot->response;
		slot->response = new AI_Response( *response );
	}

#ifdef MAPBASE
	// This "weapondelay" was just in player allies before.
	// Putting it here eliminates the need to implement OnSpokeConcept on all NPCs for weapondelay.
	if ( bCallback )
	{
		if( response != NULL && (response->GetParams()->flags & AI_ResponseParams::RG_WEAPONDELAY) )
		{
			if ( GetOuter()->IsNPC() )
			{
				// Stop shooting, as instructed, so that my speech can be heard.
				GetOuter()->MyNPCPointer()->GetShotRegulator()->FireNoEarlierThan( gpGlobals->curtime + response->GetWeaponDelay() );
			}
			else
			{
				char szResponse[64];
				response->GetName(szResponse, sizeof(szResponse));
				Warning("%s response %s wants to use weapondelay, but %s is not a NPC!\n", GetOuter()->GetDebugName(), szResponse, GetOuter()->GetDebugName());
			}
		}

		GetSink()->OnSpokeConcept( concept, response );
	}
#else
	if ( bCallback )
		GetSink()->OnSpokeConcept( concept, response );
#endif
}

//-------------------------------------

void CAI_Expresser::ClearSpokeConcept( AIConcept_t concept )
{
	m_ConceptHistories.Remove( concept );
}

#ifdef MAPBASE
//-------------------------------------

AIConcept_t CAI_Expresser::GetLastSpokeConcept( AIConcept_t excludeConcept /* = NULL */ )
{
	int iLastSpokenIndex = m_ConceptHistories.InvalidIndex();
	float flLast = 0.0f;
	for ( int i = m_ConceptHistories.First(); i != m_ConceptHistories.InvalidIndex(); i = m_ConceptHistories.Next(i ) )
	{
		ConceptHistory_t *h = &m_ConceptHistories[ i ];

		// If an 'exclude concept' was provided, skip over this entry in the history if it matches the exclude concept
		if ( excludeConcept != NULL && FStrEq( m_ConceptHistories.GetElementName( i ), excludeConcept ) )
			continue;

		if ( h->timeSpoken >= flLast )
		{
			iLastSpokenIndex = i;
			flLast = h->timeSpoken;
		}
	}

	return iLastSpokenIndex != m_ConceptHistories.InvalidIndex() ? m_ConceptHistories.GetElementName( iLastSpokenIndex ) : NULL;
}
#endif

//-------------------------------------

void CAI_Expresser::DumpHistories()
{
	int c = 1;
	for ( int i = m_ConceptHistories.First(); i != m_ConceptHistories.InvalidIndex(); i = m_ConceptHistories.Next(i ) )
	{
		ConceptHistory_t *h = &m_ConceptHistories[ i ];

		DevMsg( "%i: %s at %f\n", c++, m_ConceptHistories.GetElementName( i ), h->timeSpoken );
	}
}

//-------------------------------------

bool CAI_Expresser::IsValidResponse( ResponseType_t type, const char *pszValue )
{
	if ( type == RESPONSE_SCENE )
	{
		char szInstanceFilename[256];
		GetOuter()->GenderExpandString( pszValue, szInstanceFilename, sizeof( szInstanceFilename ) );
		return ( GetSceneDuration( szInstanceFilename ) > 0 );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CAI_TimedSemaphore *CAI_Expresser::GetMySpeechSemaphore( CBaseEntity *pNpc ) 
{
	if ( !pNpc->MyNPCPointer() )
		return NULL;

	return (pNpc->MyNPCPointer()->IsPlayerAlly() ? &g_AIFriendliesTalkSemaphore : &g_AIFoesTalkSemaphore );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CAI_Expresser::SpeechMsg( CBaseEntity *pFlex, const char *pszFormat, ... )
{
	if ( !DebuggingSpeech() )
		return;

	char string[ 2048 ];
	va_list argptr;
	va_start( argptr, pszFormat );
	Q_vsnprintf( string, sizeof(string), pszFormat, argptr );
	va_end( argptr );

	if ( pFlex->MyNPCPointer() )
	{
		DevMsg( pFlex->MyNPCPointer(), "%s", string );
	}
	else 
	{
		CGMsg( 1, CON_GROUP_CHOREO, "%s", string );
	}
	UTIL_LogPrintf( "%s", string );
}

#ifdef MAPBASE
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
char *CAI_Expresser::ParseApplyContext( const char *szContext )
{
	char szKey[128];
	char szValue[128];
	float flDuration = 0.0;

	SplitContext(szContext, szKey, sizeof(szKey), szValue, sizeof(szValue), &flDuration);

	// This is the value without the operator
	const char *pszValue = szValue + 1;

	// This is the operator itself
	char cOperator = szValue[0];

	// This is the value to operate with (e.g. the "7" in "+7")
	float flNewValue = atof( pszValue );

	if (flNewValue == 0.0f)
	{
		// If it's really 0, then this is a waste of time
		Warning("\"%s\" was detected by applyContext operators as an operable number, but it's not.\n", szValue);
		return szContext;
	}

	// This is the existing value; will be operated upon and become the final assignment
	float flValue = 0.0f;
	const char *szExistingValue = GetOuter()->GetContextValue(szKey);
	if (szExistingValue)
		flValue = atof(szExistingValue);

	// Do the operation based on what the character was
	switch (cOperator)
	{
		case '+':		flValue += flNewValue;	break;
		case '-':		flValue -= flNewValue;	break;
		case '*':		flValue *= flNewValue;	break;
		case '/':		flValue /= flNewValue;	break;
	}

	Q_snprintf(szValue, sizeof(szValue), "%f", flValue);

	// Remove all trailing 0's from the float to maintain whole integers
	int i;
	for (i = strlen(szValue)-1; szValue[i] == '0'; i--)
	{
		szValue[i] = '\0';
	}

	// If there were only zeroes past the period, this is a whole number. Remove the period
	if (szValue[i] == '.')
		szValue[i] = '\0';

	return UTIL_VarArgs("%s:%s:%f", szKey, szValue, flDuration);
}
#endif


//-----------------------------------------------------------------------------

void CAI_ExpresserHost_NPC_DoModifyOrAppendCriteria( CAI_BaseNPC *pSpeaker, AI_CriteriaSet& set )
{
	// Append current activity name
	const char *pActivityName = pSpeaker->GetActivityName( pSpeaker->GetActivity() );
	if ( pActivityName )
	{
  		set.AppendCriteria( "activity", pActivityName );
	}

	static const char *pStateNames[] = { "None", "Idle", "Alert", "Combat", "Scripted", "PlayDead", "Dead" };
	if ( (int)pSpeaker->m_NPCState < ARRAYSIZE(pStateNames) )
	{
		set.AppendCriteria( "npcstate", UTIL_VarArgs( "[NPCState::%s]", pStateNames[pSpeaker->m_NPCState] ) );
	}

#ifndef MAPBASE
	if ( pSpeaker->GetEnemy() )
	{
		set.AppendCriteria( "enemy", pSpeaker->GetEnemy()->GetClassname() );
		set.AppendCriteria( "timesincecombat", "-1" );
	}
	else
	{
		if ( pSpeaker->GetLastEnemyTime() == 0.0 )
			set.AppendCriteria( "timesincecombat", "999999.0" );
		else
			set.AppendCriteria( "timesincecombat", UTIL_VarArgs( "%f", gpGlobals->curtime - pSpeaker->GetLastEnemyTime() ) );
	}
#endif

	set.AppendCriteria( "speed", UTIL_VarArgs( "%.3f", pSpeaker->GetSmoothedVelocity().Length() ) );

	CBaseCombatWeapon *weapon = pSpeaker->GetActiveWeapon();
	if ( weapon )
	{
		set.AppendCriteria( "weapon", weapon->GetClassname() );
	}
	else
	{
		set.AppendCriteria( "weapon", "none" );
	}

	CBasePlayer *pPlayer = AI_GetSinglePlayer();
	if ( pPlayer )
	{
		Vector distance = pPlayer->GetAbsOrigin() - pSpeaker->GetAbsOrigin();

		set.AppendCriteria( "distancetoplayer", UTIL_VarArgs( "%f", distance.Length() ) );

	}
	else
	{
		set.AppendCriteria( "distancetoplayer", UTIL_VarArgs( "%i", MAX_COORD_RANGE ) );
	}

	if ( pSpeaker->HasCondition( COND_SEE_PLAYER ) )
	{
		set.AppendCriteria( "seeplayer", "1" );
	}
	else
	{
		set.AppendCriteria( "seeplayer", "0" );
	}

	if ( pPlayer && pPlayer->FInViewCone( pSpeaker ) && pPlayer->FVisible( pSpeaker ) )
	{
		set.AppendCriteria( "seenbyplayer", "1" );
	}
	else
	{
		set.AppendCriteria( "seenbyplayer", "0" );
	}
}

//-----------------------------------------------------------------------------

//=============================================================================
// HPE_BEGIN:
// [Forrest] Remove npc_speakall from Counter-Strike.
//=============================================================================
#ifndef CSTRIKE_DLL
extern CBaseEntity *FindPickerEntity( CBasePlayer *pPlayer );
CON_COMMAND( npc_speakall, "Force the npc to try and speak all their responses" )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	CBaseEntity *pEntity;

	if ( args[1] && *args[1] )
	{
		pEntity = gEntList.FindEntityByName( NULL, args[1], NULL );
		if ( !pEntity )
		{
			pEntity = gEntList.FindEntityByClassname( NULL, args[1] );
		}
	}
	else
	{
		pEntity = FindPickerEntity( UTIL_GetCommandClient() );
	}
		
	if ( pEntity )
	{
		CAI_BaseNPC *pNPC = pEntity->MyNPCPointer();
		if (pNPC)
		{
			if ( pNPC->GetExpresser() )
			{
				bool save = engine->LockNetworkStringTables( false );
				pNPC->GetExpresser()->TestAllResponses();
				engine->LockNetworkStringTables( save );
			}
		}
	}
}
#endif
//=============================================================================
// HPE_END
//=============================================================================

//-----------------------------------------------------------------------------

CMultiplayer_Expresser::CMultiplayer_Expresser( CBaseFlex *pOuter ) : CAI_Expresser( pOuter )
{
	m_bAllowMultipleScenes = false;
}

bool CMultiplayer_Expresser::IsSpeaking( void )
{
	if ( m_bAllowMultipleScenes )
	{
		return false;
	}

	return CAI_Expresser::IsSpeaking();
}


void CMultiplayer_Expresser::AllowMultipleScenes()
{
	m_bAllowMultipleScenes = true;
}

void CMultiplayer_Expresser::DisallowMultipleScenes()
{
	m_bAllowMultipleScenes = false;
}
