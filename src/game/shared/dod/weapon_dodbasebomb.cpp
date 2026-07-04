//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

#include "cbase.h"
#include "weapon_dodbasebomb.h"

#include "in_buttons.h"
#include "dod_gamerules.h"
#include "decals.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"
#include "KeyValues.h"
#include "obstacle_pushaway.h"

#ifdef CLIENT_DLL
	#include "c_dod_player.h"
	#include "c_te_legacytempents.h"
	#include "tempent.h"
	#include "engine/IEngineSound.h"
	#include "dlight.h"
	#include "iefx.h"
	#include "soundemittersystem/isoundemittersystembase.h"
	#include <bitbuf.h>
#else
	#include "dod_player.h"
	#include "dod_bombtarget.h"
	#include "collisionutils.h"
	#include "in_buttons.h"
	#include "vguiscreen.h"
	#include "explode.h"
	#include "triggers.h"
#endif


IMPLEMENT_NETWORKCLASS_ALIASED( DODBaseBombWeapon, DT_BaseBombWeapon )


BEGIN_NETWORK_TABLE( CDODBaseBombWeapon, DT_BaseBombWeapon )

#ifdef CLIENT_DLL
	//RecvPropBool( RECVINFO(m_bDeployed) )
#else
	//SendPropBool( SENDINFO(m_bDeployed) )
#endif

END_NETWORK_TABLE()


BEGIN_PREDICTION_DATA( CDODBaseBombWeapon )
END_PREDICTION_DATA()

#ifndef CLIENT_DLL

BEGIN_DATADESC( CDODBaseBombWeapon )
END_DATADESC()

#endif

LINK_ENTITY_TO_CLASS( weapon_basebomb, CDODBaseBombWeapon );
PRECACHE_WEAPON_REGISTER( weapon_basebomb );

acttable_t CDODBaseBombWeapon::m_acttable[] = 
{
	{ ACT_PRONE_IDLE,						ACT_DOD_PRONEWALK_IDLE_PISTOL,			false },	//?
	{ ACT_PRONE_FORWARD,					ACT_DOD_PRONEWALK_IDLE_PISTOL,			false },	//?
	{ ACT_DOD_STAND_IDLE,					ACT_DOD_STAND_IDLE_TNT,					false },
	{ ACT_DOD_CROUCH_IDLE,					ACT_DOD_CROUCH_IDLE_TNT,				false },	//?
	{ ACT_DOD_CROUCHWALK_IDLE,				ACT_DOD_CROUCHWALK_IDLE_TNT,			false },
	{ ACT_DOD_WALK_IDLE,					ACT_DOD_WALK_IDLE_TNT,					false },
	{ ACT_DOD_RUN_IDLE,						ACT_DOD_RUN_IDLE_TNT,					false },
	{ ACT_SPRINT,							ACT_DOD_SPRINT_IDLE_TNT,				false },

	// Hand Signals
	{ ACT_DOD_HS_IDLE,						ACT_DOD_HS_IDLE_PISTOL,					false },
	{ ACT_DOD_HS_CROUCH,					ACT_DOD_HS_CROUCH_PISTOL,				false },
};

IMPLEMENT_ACTTABLE( CDODBaseBombWeapon );

CDODBaseBombWeapon::CDODBaseBombWeapon()
{
}

void CDODBaseBombWeapon::Spawn( )
{
	WEAPON_FILE_INFO_HANDLE	hWpnInfo = LookupWeaponInfoSlot( GetClassname() );

	Assert( hWpnInfo != GetInvalidWeaponInfoHandle() );

	CDODWeaponInfo *pWeaponInfo = dynamic_cast< CDODWeaponInfo* >( GetFileWeaponInfoFromHandle( hWpnInfo ) );

	Assert( pWeaponInfo && "Failed to get CDODWeaponInfo in weapon spawn" );

	m_pWeaponInfo = pWeaponInfo;

	SetPlanting( false );

	BaseClass::Spawn();
}

void CDODBaseBombWeapon::Precache()
{
	BaseClass::Precache();
}

bool CDODBaseBombWeapon::Deploy( )
{
#ifdef CLIENT_DLL
	CDODPlayer *pPlayer = GetDODPlayerOwner();

	if ( pPlayer )
	{
		pPlayer->HintMessage( HINT_BOMB_FIRST_SELECT );
	}
#endif

	return DefaultDeploy( (char*)GetViewModel(), (char*)GetWorldModel(), GetDrawActivity(), (char*)GetAnimPrefix() );
}

void CDODBaseBombWeapon::PrimaryAttack()
{
#ifndef CLIENT_DLL
	if ( IsPlanting() )
	{
		CDODBombTarget *pTarget = (CDODBombTarget *)m_hBombTarget.Get();
		CBasePlayer *pPlayer = GetPlayerOwner();

		if ( !pTarget || !pPlayer )
		{
			CancelPlanting();
			return;
		}

		if ( pTarget->CanPlantHere( GetDODPlayerOwner() ) == false )
		{
			// if the target is not active anymore, cancel ( someone planted there already? )
			CancelPlanting();
		}
		else if ( ( pTarget->GetAbsOrigin() - pPlayer->WorldSpaceCenter() ).Length() > DOD_BOMB_PLANT_RADIUS )
		{
			// if we're too far away, cancel
			CancelPlanting();			
		}
		else if ( IsLookingAtBombTarget( pPlayer, pTarget ) == false || ( pPlayer->GetFlags() & FL_ONGROUND ) == 0 )
		{
			// not looking at the target anymore
			CancelPlanting();
		}
		else if ( gpGlobals->curtime > m_flPlantCompleteTime )
		{
			// we finished the plant
			CompletePlant();
		}
		else
		{
			m_flNextPlantCheck = gpGlobals->curtime + 0.2;
		}

		return;
	}

	// find nearby, visible bomb targets
	CBaseEntity *pEnt = NULL;
	CDODBombTarget *pBestTarget = NULL;

	float flBestDist = FLT_MAX;

	CDODPlayer *pPlayer = GetDODPlayerOwner();

	if ( !pPlayer )
		return;
	
	while( ( pEnt = gEntList.FindEntityByClassname( pEnt, "dod_bomb_target" ) ) != NULL )
	{
		CDODBombTarget *pTarget = static_cast<CDODBombTarget *>( pEnt );

		if ( !pTarget->CanPlantHere( pPlayer ) )
			continue;

		Vector pos = pPlayer->WorldSpaceCenter();

		float flDist = ( pos - pTarget->GetAbsOrigin() ).Length();

		// if we are looking directly at a bomb target and it is within our radius, that automatically wins
		if ( flDist < flBestDist &&
			flDist < DOD_BOMB_PLANT_RADIUS &&
			IsLookingAtBombTarget( pPlayer, pTarget ) &&
			( pPlayer->GetFlags() & FL_ONGROUND ) )
		{
			flBestDist = flDist;
			pBestTarget = pTarget;
		}
	}

	if ( pBestTarget )
	{
		StartPlanting( pBestTarget );
	}

	m_flNextPlantCheck = gpGlobals->curtime + 0.2;

	// true if the player is not holding primary attack
	m_bUsePlant = !( pPlayer->m_nButtons & (IN_ATTACK|IN_ATTACK2) );
#endif
}

void CDODBaseBombWeapon::SecondaryAttack()
{
	PrimaryAttack();
}

#ifndef CLIENT_DLL
void CDODBaseBombWeapon::StartPlanting( CDODBombTarget *pTarget )
{
	// we have already checked that we can plant here

	// store a pointer to the target we're bombing
	m_hBombTarget = pTarget;

	// must do this after setting the bomb target as we tell the planter
	// what target they are at
	SetPlanting( true );

	// set the timer for when we'll be done
	m_flPlantCompleteTime = gpGlobals->curtime + DOD_BOMB_PLANT_TIME;

	// play the planting animation
	SendWeaponAnim( ACT_VM_PRIMARYATTACK );

	CDODPlayer *pPlayer = GetDODPlayerOwner();

	if ( pPlayer )
	{
		pPlayer->DoAnimationEvent( PLAYERANIMEVENT_PLANT_TNT );

		pPlayer->SetMaxSpeed( 1 );

		pPlayer->SetProgressBarTime( DOD_BOMB_PLANT_TIME );
	}
}

bool CDODBaseBombWeapon::CancelPlanting( void )
{
	bool bHolster = false;

	SetPlanting( false );

	// play a stop animation
	SendWeaponAnim( ACT_VM_IDLE );

	CDODPlayer *pPlayer = GetDODPlayerOwner();

	if ( pPlayer )
	{
		pPlayer->DoAnimationEvent( PLAYERANIMEVENT_CANCEL_GESTURES );

		// restore player speed
		pPlayer->SetMaxSpeed( 600 );

		pPlayer->ResetProgressBar();

		if ( m_bUsePlant )
		{
			pPlayer->SelectLastItem();

			bHolster = true;
		}
	}

	return bHolster;
}

void CDODBaseBombWeapon::CompletePlant( void )
{
	CDODPlayer *pPlayer = ToDODPlayer( GetPlayerOwner() );

	if ( pPlayer )
	{
		SetPlanting( false );

		// restore player speed
		GetDODPlayerOwner()->SetMaxSpeed( 600 );

		// Tell the target that we finished planting the bomb
		((CDODBombTarget *)m_hBombTarget.Get())->CompletePlanting( pPlayer );

		// destroy the bomb weapon
		pPlayer->Weapon_Drop( this, NULL, NULL );
		UTIL_Remove(this);

		pPlayer->ResetProgressBar();

		pPlayer->SelectLastItem();
	}
}

bool CDODBaseBombWeapon::IsLookingAtBombTarget( CBasePlayer *pPlayer, CDODBombTarget *pTarget )
{
	Vector forward;
	AngleVectors( pPlayer->EyeAngles(), &forward );

	Vector toBomb = pTarget->GetAbsOrigin() - pPlayer->EyePosition();
	toBomb.NormalizeInPlace();

	return ( DotProduct( forward, toBomb ) >= 0.8 );
}

#endif

void CDODBaseBombWeapon::ItemPostFrame()
{
#ifndef CLIENT_DLL
	CBasePlayer *pPlayer = GetPlayerOwner();

	if ( !pPlayer )
		return;

	if ( pPlayer->m_nButtons & (IN_ATTACK|IN_ATTACK2) )
	{
		PrimaryAttack();
	}
	// Only use the time check if we are planting with the +use key
	// adds a slight lag to breaking the player lock otherwise
	else if ( !m_bUsePlant || m_flNextPlantCheck < gpGlobals->curtime )
	{
		if ( IsPlanting() )
		{
			// reset all planting
			bool bHolster = CancelPlanting();

			// sometimes after canceling we put the weapon away and switch
			// to our last weapon. In that case, we don't want to send any more
			// anim calls b/c it confuses the client.
			if ( bHolster )
			{
				// we've put this weapon away, stop everything
				return;
			}

			// anim now
			m_flTimeWeaponIdle = 0;
		}
		
		// idle
		if (m_flTimeWeaponIdle > gpGlobals->curtime)
			return;

		SendWeaponAnim( GetIdleActivity() );

		m_flTimeWeaponIdle = gpGlobals->curtime + SequenceDuration();

		// if we're not planting, why do we have the bomb out?
		// switch to our next best weapon
		pPlayer->SelectLastItem();
	}
	
#endif	// CLIENT_DLL
}

bool CDODBaseBombWeapon::Holster( CBaseCombatWeapon *pSwitchingTo )
{
#ifndef CLIENT_DLL
	if ( IsPlanting() )
		CancelPlanting();
#endif

	return BaseClass::Holster( pSwitchingTo );
}

void CDODBaseBombWeapon::SetPlanting( bool bPlanting )
{
	m_bPlanting = bPlanting;

#ifndef CLIENT_DLL
	CDODPlayer *pPlayer = GetDODPlayerOwner();

	if ( pPlayer )
	{
		pPlayer->SetPlanting( m_bPlanting ? (CDODBombTarget *)m_hBombTarget.Get() : NULL );
	}
#endif
}

bool CDODBaseBombWeapon::IsPlanting( void )
{
	return m_bPlanting;
}

// CSS BOMB BELOW!!!

ConVar mp_c4timer(
	"mp_c4timer",
	"45",
	FCVAR_REPLICATED | FCVAR_NOTIFY,
	"how long from when the C4 is armed until it blows",
	true, 10,	// min value
	true, 90	// max value
);
#define BLINK_INTERVAL 2.0
#define PLANTED_C4_MODEL "models/weapons/w_c4_planted.mdl"
#define HEIST_MODE_C4_TIME 25

int g_sModelIndexC4Glow = -1;

#define WEAPON_C4_ARM_TIME	3.0


#ifdef CLIENT_DLL

#else


	LINK_ENTITY_TO_CLASS( planted_c4, CPlantedC4 );
	PRECACHE_REGISTER( planted_c4 );

	BEGIN_DATADESC( CPlantedC4 )
		DEFINE_FUNCTION( C4Think )
	END_DATADESC()
	

	IMPLEMENT_SERVERCLASS_ST( CPlantedC4, DT_PlantedC4 )
		SendPropBool( SENDINFO(m_bBombTicking) ),
		SendPropFloat( SENDINFO(m_flC4Blow), 0, SPROP_NOSCALE ),
		SendPropFloat( SENDINFO(m_flTimerLength), 0, SPROP_NOSCALE ),
		SendPropFloat( SENDINFO(m_flDefuseLength), 0, SPROP_NOSCALE ),
		SendPropFloat( SENDINFO(m_flDefuseCountDown), 0, SPROP_NOSCALE ),
	END_SEND_TABLE()

	
BEGIN_PREDICTION_DATA( CPlantedC4 )
END_PREDICTION_DATA()



	CUtlVector< CPlantedC4* > g_PlantedC4s;


	CPlantedC4::CPlantedC4()
	{
		g_PlantedC4s.AddToTail( this );
	}

	CPlantedC4::~CPlantedC4()
	{
		g_PlantedC4s.FindAndRemove( this );

		int i;
		// Kill the control panels
		for ( i = m_hScreens.Count(); --i >= 0; )
		{
			DestroyVGuiScreen( m_hScreens[i].Get() );
		}
		m_hScreens.RemoveAll();
	}

	int CPlantedC4::UpdateTransmitState()
	{
		return SetTransmitState( FL_EDICT_FULLCHECK );
	}

	int CPlantedC4::ShouldTransmit( const CCheckTransmitInfo *pInfo )
	{
		// Terrorists always need this object for the radar
		// Everybody needs it for hiding the round timer and showing the planted C4 scenario icon
		return FL_EDICT_ALWAYS;
	}

	void CPlantedC4::Precache()
	{
		g_sModelIndexC4Glow = PrecacheModel( "sprites/ledglow.vmt" );
		PrecacheModel( PLANTED_C4_MODEL );
		PrecacheVGuiScreen( "c4_panel" );

		engine->ForceModelBounds( PLANTED_C4_MODEL, Vector( -7, -13, -3 ), Vector( 9, 12, 11 ) );
	}

	void CPlantedC4::GetControlPanelInfo( int nPanelIndex, const char *&pPanelName )
	{
		pPanelName = "c4_panel";
	}

	void CPlantedC4::GetControlPanelClassName( int nPanelIndex, const char *&pPanelName )
	{
		pPanelName = "vgui_screen";
	}

	//-----------------------------------------------------------------------------
	// This is called by the base object when it's time to spawn the control panels
	//-----------------------------------------------------------------------------
	void CPlantedC4::SpawnControlPanels()
	{
		char buf[64];

		// FIXME: Deal with dynamically resizing control panels?

		// If we're attached to an entity, spawn control panels on it instead of use
		CBaseAnimating *pEntityToSpawnOn = this;
		char *pOrgLL = "controlpanel%d_ll";
		char *pOrgUR = "controlpanel%d_ur";
		char *pAttachmentNameLL = pOrgLL;
		char *pAttachmentNameUR = pOrgUR;

		Assert( pEntityToSpawnOn );

		// Lookup the attachment point...
		int nPanel;
		for ( nPanel = 0; true; ++nPanel )
		{
			Q_snprintf( buf, sizeof( buf ), pAttachmentNameLL, nPanel );
			int nLLAttachmentIndex = pEntityToSpawnOn->LookupAttachment(buf);
			if (nLLAttachmentIndex <= 0)
			{
				// Try and use my panels then
				pEntityToSpawnOn = this;
				Q_snprintf( buf, sizeof( buf ), pOrgLL, nPanel );
				nLLAttachmentIndex = pEntityToSpawnOn->LookupAttachment(buf);
				if (nLLAttachmentIndex <= 0)
					return;
			}

			Q_snprintf( buf, sizeof( buf ), pAttachmentNameUR, nPanel );
			int nURAttachmentIndex = pEntityToSpawnOn->LookupAttachment(buf);
			if (nURAttachmentIndex <= 0)
			{
				// Try and use my panels then
				Q_snprintf( buf, sizeof( buf ), pOrgUR, nPanel );
				nURAttachmentIndex = pEntityToSpawnOn->LookupAttachment(buf);
				if (nURAttachmentIndex <= 0)
					return;
			}

			const char *pScreenName;
			GetControlPanelInfo( nPanel, pScreenName );
			if (!pScreenName)
				continue;

			const char *pScreenClassname;
			GetControlPanelClassName( nPanel, pScreenClassname );
			if ( !pScreenClassname )
				continue;

			// Compute the screen size from the attachment points...
			matrix3x4_t	panelToWorld;
			pEntityToSpawnOn->GetAttachment( nLLAttachmentIndex, panelToWorld );

			matrix3x4_t	worldToPanel;
			MatrixInvert( panelToWorld, worldToPanel );

			// Now get the lower right position + transform into panel space
			Vector lr, lrlocal;
			pEntityToSpawnOn->GetAttachment( nURAttachmentIndex, panelToWorld );
			MatrixGetColumn( panelToWorld, 3, lr );
			VectorTransform( lr, worldToPanel, lrlocal );

			float flWidth = lrlocal.x;
			float flHeight = lrlocal.y;

			CVGuiScreen *pScreen = CreateVGuiScreen( pScreenClassname, pScreenName, pEntityToSpawnOn, this, nLLAttachmentIndex );
			pScreen->ChangeTeam( GetTeamNumber() );
			pScreen->SetActualSize( flWidth, flHeight );
			pScreen->SetActive( true );
			pScreen->MakeVisibleOnlyToTeammates( false );
			int nScreen = m_hScreens.AddToTail( );
			m_hScreens[nScreen].Set( pScreen );			
		}
	}

	void CPlantedC4::SetTransmit( CCheckTransmitInfo *pInfo, bool bAlways )
	{
		// Are we already marked for transmission?
		if ( pInfo->m_pTransmitEdict->Get( entindex() ) )
			return;

		BaseClass::SetTransmit( pInfo, bAlways );

		// Force our screens to be sent too.
		for ( int i=0; i < m_hScreens.Count(); i++ )
		{
			CVGuiScreen *pScreen = m_hScreens[i].Get();
			pScreen->SetTransmit( pInfo, bAlways );
		}
	}

	CPlantedC4* CPlantedC4::ShootSatchelCharge( CDODPlayer *pevOwner, Vector vecStart, QAngle vecAngles )
	{
		CPlantedC4 *pGrenade = dynamic_cast< CPlantedC4* >( CreateEntityByName( "planted_c4" ) );
		if ( pGrenade )
		{
			vecAngles[0] = 0;
			vecAngles[2] = 0;
			pGrenade->Init( pevOwner, vecStart, vecAngles );
			return pGrenade;
		}
		else
		{
			Warning( "Can't create planted_c4 entity!\n" );
			return NULL;
		}
	}


	void CPlantedC4::Init(CDODPlayer*pevOwner, Vector vecStart, QAngle vecAngles )
	{
		SetMoveType( MOVETYPE_NONE );
		SetSolid( SOLID_NONE );

		SetModel( PLANTED_C4_MODEL );	// Change this to c4 model

		SetCollisionBounds( Vector( 0, 0, 0 ), Vector( 8, 8, 8 ) );
		

		SetAbsOrigin( vecStart );
		SetAbsAngles( vecAngles );
		SetOwnerEntity( pevOwner );
		
		// Detonate in "time" seconds
		SetThink( &CPlantedC4::C4Think );

		SetNextThink( gpGlobals->curtime + 0.1f );
		
		m_flTimerLength = mp_c4timer.GetInt();

		m_flC4Blow = gpGlobals->curtime + m_flTimerLength;
		m_flNextDefuse = 0;

		m_bStartDefuse = false;
		m_bBombTicking = true;
		SetFriction( 0.9 );

		m_flDefuseLength = 0.0f;
		
		SpawnControlPanels();
	}

	void CPlantedC4::C4Think()
	{
		if (!IsInWorld())
		{
			UTIL_Remove( this );
			return;
		}

		//Bomb is dead, don't think anymore
		if( !m_bBombTicking )
		{
			SetThink( NULL );
			return;
		}
				

		SetNextThink( gpGlobals->curtime + 0.12 );

#ifndef CLIENT_DLL
		// let the bots hear the bomb beeping
		// BOTPORT: Emit beep events at same time as client effects
		IGameEvent * event = gameeventmanager->CreateEvent( "bomb_beep" );
		if( event )
		{
			event->SetInt( "entindex", entindex() );
			gameeventmanager->FireEvent( event );
		}
#endif
		
		// IF the timer has expired ! blow this bomb up!
		if (m_flC4Blow <= gpGlobals->curtime)
		{
			// give the defuser credit for defusing the bomb
			CBasePlayer *pBombOwner = dynamic_cast< CBasePlayer* >( GetOwnerEntity() );
			if ( pBombOwner )
			{
				pBombOwner->IncrementFragCount( 3 );
			}

			DODGameRules()->m_bBombDropped = false;

			trace_t tr;
			Vector vecSpot = GetAbsOrigin();
			vecSpot[2] += 8;

			UTIL_TraceLine( vecSpot, vecSpot + Vector ( 0, 0, -40 ), MASK_SOLID, this, COLLISION_GROUP_NONE, &tr );

			Explode( &tr, DMG_BLAST );

			DODGameRules()->m_bBombPlanted = false;

			IGameEvent * event = gameeventmanager->CreateEvent( "bomb_exploded" );
			if( event )
			{
				event->SetInt( "userid", pBombOwner?pBombOwner->GetUserID():-1 );
				event->SetInt( "site", m_iBombSiteIndex );
				event->SetInt( "priority", 9 );
				gameeventmanager->FireEvent( event );
			}
		}

		//if the defusing process has started
		if ((m_bStartDefuse == true) && (m_pBombDefuser != NULL))
		{
			//if the defusing process has not ended yet
			if ( m_flDefuseCountDown > gpGlobals->curtime)
			{
				int iOnGround = FBitSet( m_pBombDefuser->GetFlags(), FL_ONGROUND );

				//if the bomb defuser has stopped defusing the bomb
				if( m_flNextDefuse < gpGlobals->curtime || !iOnGround )
				{
					if ( !iOnGround && m_pBombDefuser->IsAlive() )
						ClientPrint( m_pBombDefuser, HUD_PRINTCENTER, "#C4_Defuse_Must_Be_On_Ground");

					// release the player from being frozen
					m_pBombDefuser->ResetMaxSpeed();
					m_pBombDefuser->m_bIsDefusing = false;

#ifndef CLIENT_DLL
					// tell the bots someone has aborted defusing
					IGameEvent * event = gameeventmanager->CreateEvent( "bomb_abortdefuse" );
					if( event )
					{
						event->SetInt("userid", m_pBombDefuser->GetUserID() );
						event->SetInt( "priority", 6 );
						gameeventmanager->FireEvent( event );
					}
#endif

					//cancel the progress bar
					m_pBombDefuser->SetProgressBarTime( 0 );
					m_pBombDefuser = NULL;
					m_bStartDefuse = false;
					m_flDefuseCountDown = 0;
					m_flDefuseLength = 0;	//force it to show completely defused
				}

				return;
			}
			//if the defuse process has ended, kill the c4
			else if ( !m_pBombDefuser->IsDead() )
			{
				IGameEvent * event = gameeventmanager->CreateEvent( "bomb_defused" );
				if( event )
				{
					event->SetInt("userid", m_pBombDefuser->GetUserID() );
					event->SetInt("site", m_iBombSiteIndex );
					event->SetInt( "priority", 9 );
					gameeventmanager->FireEvent( event );
				}

			
				Vector soundPosition = m_pBombDefuser->GetAbsOrigin() + Vector( 0, 0, 5 );
				CPASAttenuationFilter filter( soundPosition );

				EmitSound( filter, entindex(), "c4.disarmfinish" );
								
				// The bomb has just been disarmed.. Check to see if the round should end now
				m_bBombTicking = false;

				// release the player from being frozen
				m_pBombDefuser->ResetMaxSpeed();
				m_pBombDefuser->m_bIsDefusing = false;

				DODGameRules()->m_bBombDefused = true;
				//DODGameRules()->CheckWinConditions();
				DODGameRules()->SetWinningTeam(TEAM_ALLIES);

				// give the defuser credit for defusing the bomb
				m_pBombDefuser->IncrementFragCount( 3 );

				DODGameRules()->m_bBombDropped = false;
				DODGameRules()->m_bBombPlanted = false;

				// Clear their progress bar.
				m_pBombDefuser->SetProgressBarTime( 0 );

				m_pBombDefuser = NULL;
				m_bStartDefuse = false;

				m_flDefuseLength = 10;

				return;
			}

#ifndef CLIENT_DLL
			// tell the bots someone has aborted defusing
			IGameEvent * event = gameeventmanager->CreateEvent( "bomb_abortdefuse" );
			if( event )
			{
				event->SetInt("userid", m_pBombDefuser->GetUserID() );
				event->SetInt( "priority", 6 );
				gameeventmanager->FireEvent( event );
			}
#endif

			//if it gets here then the previouse defuser has taken off or been killed
			// release the player from being frozen
			m_pBombDefuser->ResetMaxSpeed();
			m_pBombDefuser->m_bIsDefusing = false;
			m_bStartDefuse = false;
			m_pBombDefuser = NULL;
		}
	}

	// Regular explosions
	void CPlantedC4::Explode( trace_t *pTrace, int bitsDamageType )
	{
		// Check to see if the round is over after the bomb went off...
		DODGameRules()->m_bTargetBombed = true;
		m_bBombTicking = false;
		//DODGameRules()->CheckWinConditions();
		DODGameRules()->SetWinningTeam(TEAM_AXIS);

		// Do the Damage
		float flBombRadius = 500;
		if ( g_pMapInfo )
			flBombRadius = g_pMapInfo->m_flBombRadius;

		// Output to the bomb target ent
		CBaseEntity *pTarget = NULL;
		variant_t emptyVariant;
		while ((pTarget = gEntList.FindEntityByClassname( pTarget, "func_bomb_target" )) != NULL)
		{
			//Adrian - But only to the one we want!
			if ( pTarget->entindex() != m_iBombSiteIndex )
				 continue;
			
			pTarget->AcceptInput( "BombExplode", this, this, emptyVariant, 0 );
				break;
		}	

		// Pull out of the wall a bit
		if ( pTrace->fraction != 1.0 )
		{
			SetAbsOrigin( pTrace->endpos + (pTrace->plane.normal * 0.6) );
		}

		{
			Vector pos = GetAbsOrigin() + Vector( 0,0,8 );

			// add an explosion TE so it affects clientside physics
			CPASFilter filter( pos );
			te->Explosion( filter, 0.0,
				&pos, 
				g_sModelIndexFireball,
				50.0, 
				25,
				TE_EXPLFLAG_NONE,
				flBombRadius * 3.5,
				200 );
		}
		
		// Fireball sprite and sound!!
		{
			Vector fireballPos = GetAbsOrigin();
			CPVSFilter filter( fireballPos );
			te->Sprite( filter, 0, &fireballPos, g_sModelIndexFireball, 100, 150 );
		}

		{
			Vector fireballPos = GetAbsOrigin() + Vector( 
				random->RandomFloat( -512, 512 ), 
				random->RandomFloat( -512, 512 ),
				random->RandomFloat( -10, 10 ) );

			CPVSFilter filter( fireballPos );
			te->Sprite( filter, 0, &fireballPos, g_sModelIndexFireball, 100, 150 );
		}

		{
			Vector fireballPos = GetAbsOrigin() + Vector( 
				random->RandomFloat( -512, 512 ), 
				random->RandomFloat( -512, 512 ),
				random->RandomFloat( -10, 10 ) );

			CPVSFilter filter( fireballPos );
			te->Sprite( filter, 0, &fireballPos, g_sModelIndexFireball, 100, 150 );
		}

		// Sound! for everyone
		CBroadcastRecipientFilter filter;
		EmitSound( filter, entindex(), "c4.explode" );


		// Decal!
		UTIL_DecalTrace( pTrace, "Scorch" );

		
		// Shake!
		UTIL_ScreenShake( pTrace->endpos, 25.0, 150.0, 1.0, 3000, SHAKE_START );


		SetOwnerEntity( NULL ); // can't traceline attack owner if this is set

		DODGameRules()->RadiusDamage( 
			CTakeDamageInfo( this, GetOwnerEntity(), flBombRadius, bitsDamageType ),
			GetAbsOrigin(),
			flBombRadius * 3.5,	//Matt - don't ask me, this is how CS does it.
			CLASS_NONE,
			true );	// IGNORE THE WORLD!!

		// send director message, that something important happed here
		/*
		MESSAGE_BEGIN( MSG_SPEC, SVC_DIRECTOR );
			WRITE_BYTE ( 9 );	// command length in bytes
			WRITE_BYTE ( DRC_CMD_EVENT );	// bomb explode
			WRITE_SHORT( ENTINDEX(this->edict()) );	// index number of primary entity
			WRITE_SHORT( 0 );	// index number of secondary entity
			WRITE_LONG( 15 | DRC_FLAG_FINAL );   // eventflags (priority and flags)
		MESSAGE_END();
		*/

		UTIL_Remove( this );
	}

	
	// For CTs to defuse the c4
	void CPlantedC4::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
	{
		//Can't defuse if its already defused or if it has blown up
		if( !m_bBombTicking )
		{
			SetUse( NULL );
			return;
		}

		CDODPlayer*player = dynamic_cast<CDODPlayer* >( pActivator );

		if ( !player || player->GetTeamNumber() != TEAM_ALLIES )
		 	return;

		if ( m_bStartDefuse )
		{
			if ( player != m_pBombDefuser )
			{
				if ( player->m_iNextTimeCheck < gpGlobals->curtime )
				{
					ClientPrint( player, HUD_PRINTCENTER, "#Bomb_Already_Being_Defused" );
					player->m_iNextTimeCheck = gpGlobals->curtime + 1;
				}
				return;
			}

			m_flNextDefuse = gpGlobals->curtime + 0.5;
		}
		else
		{
			// freeze the player in place while defusing
			player->SetMaxSpeed( 1 );

			IGameEvent * event = gameeventmanager->CreateEvent("bomb_begindefuse" );
			if( event )
			{
				event->SetInt( "userid", player->GetUserID() );
				// DODCSS TODO: ADD DEFUSE KIT!! - Vvis :3 
				//if ( player->HasDefuser() )
				//{
				//	event->SetInt( "haskit", 1 );
				//	// TODO show messages on clients on event 
				//	ClientPrint( player, HUD_PRINTCENTER, "#Defusing_Bomb_With_Defuse_Kit" );
				//}
				//else
				{
					event->SetInt( "haskit", 0 );
					// TODO show messages on clients on event 
					ClientPrint( player, HUD_PRINTCENTER, "#Defusing_Bomb_Without_Defuse_Kit" );
				}
				event->SetInt( "priority", 8 );
                gameeventmanager->FireEvent( event );
			}

			Vector soundPosition = player->GetAbsOrigin() + Vector( 0, 0, 5 );
			CPASAttenuationFilter filter( soundPosition );

			EmitSound( filter, entindex(), "c4.disarmstart" );

			//m_flDefuseLength = player->HasDefuser() ? 5 : 10;

			m_flDefuseLength = 10;

			m_flNextDefuse = gpGlobals->curtime + 0.5;
			m_pBombDefuser = player;
			m_bStartDefuse = TRUE;
			player->m_bIsDefusing = true;
			
			m_flDefuseCountDown = gpGlobals->curtime + m_flDefuseLength;

			//start the progress bar
			player->SetProgressBarTime( m_flDefuseLength );
		}
	}


#endif



// -------------------------------------------------------------------------------- //
// Tables.
// -------------------------------------------------------------------------------- //

IMPLEMENT_NETWORKCLASS_ALIASED( C4, DT_WeaponC4 )

BEGIN_NETWORK_TABLE( CC4, DT_WeaponC4 )
	#ifdef CLIENT_DLL
		RecvPropBool( RECVINFO( m_bStartedArming ) ),
		RecvPropBool( RECVINFO( m_bBombPlacedAnimation ) ),
		RecvPropFloat( RECVINFO( m_fArmedTime ) )
	#else
		SendPropBool( SENDINFO( m_bStartedArming ) ),
		SendPropBool( SENDINFO( m_bBombPlacedAnimation ) ),
		SendPropFloat( SENDINFO( m_fArmedTime ), 0, SPROP_NOSCALE )
	#endif
END_NETWORK_TABLE()

#if defined CLIENT_DLL
BEGIN_PREDICTION_DATA( CC4 )
	DEFINE_PRED_FIELD( m_bStartedArming, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_bBombPlacedAnimation, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_fArmedTime, FIELD_FLOAT, FTYPEDESC_INSENDTABLE )
END_PREDICTION_DATA()
#endif

LINK_ENTITY_TO_CLASS( weapon_c4, CC4 );
PRECACHE_WEAPON_REGISTER( weapon_c4 );

acttable_t CC4::m_acttable[] =
{
	{ ACT_PRONE_IDLE,						ACT_DOD_PRONEWALK_IDLE_PISTOL,			false },	//?
	{ ACT_PRONE_FORWARD,					ACT_DOD_PRONEWALK_IDLE_PISTOL,			false },	//?
	{ ACT_DOD_STAND_IDLE,					ACT_DOD_STAND_IDLE_TNT,					false },
	{ ACT_DOD_CROUCH_IDLE,					ACT_DOD_CROUCH_IDLE_TNT,				false },	//?
	{ ACT_DOD_CROUCHWALK_IDLE,				ACT_DOD_CROUCHWALK_IDLE_TNT,			false },
	{ ACT_DOD_WALK_IDLE,					ACT_DOD_WALK_IDLE_TNT,					false },
	{ ACT_DOD_RUN_IDLE,						ACT_DOD_RUN_IDLE_TNT,					false },
	{ ACT_SPRINT,							ACT_DOD_SPRINT_IDLE_TNT,				false },

	// Hand Signals
	{ ACT_DOD_HS_IDLE,						ACT_DOD_HS_IDLE_PISTOL,					false },
	{ ACT_DOD_HS_CROUCH,					ACT_DOD_HS_CROUCH_PISTOL,				false },
};

IMPLEMENT_ACTTABLE(CC4);

// -------------------------------------------------------------------------------- //
// Globals.
// -------------------------------------------------------------------------------- //

CUtlVector< CC4* > g_C4s;



// -------------------------------------------------------------------------------- //
// CC4 implementation.
// -------------------------------------------------------------------------------- //

CC4::CC4()
{
	g_C4s.AddToTail( this );

#if defined( CLIENT_DLL )
	m_szScreenText[0] = '\0';
#endif

}


CC4::~CC4()
{
	g_C4s.FindAndRemove( this );
}

void CC4::Spawn()
{
	BaseClass::Spawn();

	//Don't allow players to shoot the C4 around
	SetCollisionGroup( COLLISION_GROUP_DEBRIS );

	//Don't be damaged / moved by explosions
	m_takedamage = DAMAGE_NO;

	m_bBombPlanted = false;
}

void CC4::ItemPostFrame()
{
	CDODPlayer *pPlayer = ToDODPlayer(GetPlayerOwner());
	if ( !pPlayer )
		return;

	// Disable all the firing code.. the C4 grenade is all custom.
	if ( pPlayer->m_nButtons & IN_ATTACK )
	{
		PrimaryAttack();
	}
	else
	{
		WeaponIdle();
		
		#ifndef CLIENT_DLL
			pPlayer->ResetMaxSpeed();
		#endif
	}
}

#if defined( CLIENT_DLL )

	bool CC4::OnFireEvent( C_BaseViewModel *pViewModel, const Vector& origin, const QAngle& angles, int event, const char *options )
	{
		if( event == 7001 )
		{
			//set the screen text to the string in 'options'
			Q_strncpy( m_szScreenText, options, 16 );

			return true;
		}
		return BaseClass::OnFireEvent( pViewModel, origin, angles, event, options );
	}

	char *CC4::GetScreenText( void )
	{
		if( m_bStartedArming )
			return m_szScreenText;
		else
			return "";
	}

#endif //CLIENT_DLL

#ifdef GAME_DLL
	
		
	unsigned int CC4::PhysicsSolidMaskForEntity( void ) const
	{
		return BaseClass::PhysicsSolidMaskForEntity() | CONTENTS_PLAYERCLIP;
	}

	void CC4::Precache()
	{
		PrecacheVGuiScreen( "c4_view_panel" );
		
		PrecacheScriptSound( "c4.disarmfinish" );
		PrecacheScriptSound( "c4.explode" );
		PrecacheScriptSound( "c4.disarmstart" );
		PrecacheScriptSound( "c4.plant" );
		PrecacheScriptSound( "C4.PlantSound" );

		BaseClass::Precache();
	}

	//-----------------------------------------------------------------------------
	// Purpose: Gets info about the control panels
	//-----------------------------------------------------------------------------
	void CC4::GetControlPanelInfo( int nPanelIndex, const char *&pPanelName )
	{
		pPanelName = "c4_view_panel";
	}

	bool CC4::Holster( CBaseCombatWeapon *pSwitchingTo )
	{
		CDODPlayer*pPlayer = ToDODPlayer(GetPlayerOwner());
		if ( pPlayer )
			pPlayer->SetProgressBarTime( 0 );

		m_bStartedArming = false; // stop arming sequence
		
		return BaseClass::Holster( pSwitchingTo );
	}


	bool CC4::ShouldRemoveOnRoundRestart()
	{
		// Doesn't matter if we have an owner or not.. always remove the C4 when the round restarts.
		// The gamerules will give another C4 to some lucky player.
		CDODPlayer *pPlayer = ToDODPlayer(GetPlayerOwner());
		if ( pPlayer && pPlayer->GetActiveWeapon() == this )
			engine->ClientCommand( pPlayer->edict(), "lastinv reset\n" );
		return true;
	}

#endif


void CC4::PrimaryAttack()
{
	bool	PlaceBomb = false;
	CDODPlayer *pPlayer = ToDODPlayer(GetPlayerOwner());
	if ( !pPlayer )
		return;

	int onGround = FBitSet( pPlayer->GetFlags(), FL_ONGROUND );
	CBaseEntity *groundEntity = (onGround) ? pPlayer->GetGroundEntity() : NULL;
	if ( groundEntity )
	{
		// Don't let us stand on players, breakables, or pushaway physics objects to plant
		if ( groundEntity->IsPlayer() ||
			IsPushableEntity( groundEntity ) ||
#ifndef CLIENT_DLL
			IsBreakableEntity( groundEntity ) ||
#endif // !CLIENT_DLL
			IsPushAwayEntity( groundEntity ) )
		{
			onGround = false;
		}
	}

	if( m_bStartedArming == false && m_bBombPlanted == false )
	{
		if( pPlayer->m_bInBombZone && onGround )
		{
			m_bStartedArming = true;
			m_fArmedTime = gpGlobals->curtime + WEAPON_C4_ARM_TIME;
			m_bBombPlacedAnimation = false;


#if !defined( CLIENT_DLL )			
			// init the beep flags
			int i;
			for( i=0;i<NUM_BEEPS;i++ )
				m_bPlayedArmingBeeps[i] = false;

			// freeze the player in place while planting
			pPlayer->SetMaxSpeed( 1 );

			// player "arming bomb" animation
			pPlayer->SetAnimation( PLAYER_ATTACK1 );
	
			pPlayer->SetNextAttack( gpGlobals->curtime );

			IGameEvent * event = gameeventmanager->CreateEvent( "bomb_beginplant" );
			if( event )
			{
				event->SetInt("userid", pPlayer->GetUserID() );
				event->SetInt("site", pPlayer->m_iBombSiteIndex );
				event->SetInt( "priority", 8 );
				gameeventmanager->FireEvent( event );
			}
#endif

			SendWeaponAnim( ACT_VM_PRIMARYATTACK );

			//FX_PlantBomb( pPlayer->entindex(), pPlayer->Weapon_ShootPosition() );
		}
		else
		{
			if ( !pPlayer->m_bInBombZone )
			{
				ClientPrint( pPlayer, HUD_PRINTCENTER, "#C4_Plant_At_Bomb_Spot");
			}
			else
			{
				ClientPrint( pPlayer, HUD_PRINTCENTER, "#C4_Plant_Must_Be_On_Ground");
			}

			m_flNextPrimaryAttack = gpGlobals->curtime + 1.0;
			return;
		}
	}
	else
	{
		if ( !onGround || !pPlayer->m_bInBombZone )
		{
			if( !pPlayer->m_bInBombZone )
			{
				ClientPrint( pPlayer, HUD_PRINTCENTER, "#C4_Arming_Cancelled" );
			}
			else
			{
				ClientPrint( pPlayer, HUD_PRINTCENTER, "#C4_Plant_Must_Be_On_Ground" );
			}

			m_flNextPrimaryAttack = gpGlobals->curtime + 1.5;
			m_bStartedArming = false;

#if !defined( CLIENT_DLL )
			// release the player from being frozen, we've somehow left the bomb zone
			pPlayer->ResetMaxSpeed();

			pPlayer->SetProgressBarTime( 0 );

			//pPlayer->SetAnimation( PLAYER_HOLDBOMB );

			IGameEvent * event = gameeventmanager->CreateEvent( "bomb_abortplant" );
			if( event )
			{
				event->SetInt("userid", pPlayer->GetUserID() );
				event->SetInt("site", pPlayer->m_iBombSiteIndex );
				event->SetInt( "priority", 8 );
				gameeventmanager->FireEvent( event );
			}

#endif
			if(m_bBombPlacedAnimation == true) //this means the placement animation is canceled
			{
				SendWeaponAnim( ACT_VM_DRAW );
			}
			else
			{
				SendWeaponAnim( ACT_VM_IDLE );
			}
			
			return;
		}
		else
		{
#ifndef CLIENT_DLL
			PlayArmingBeeps();
#endif

			if( gpGlobals->curtime >= m_fArmedTime ) //the c4 is ready to be armed
			{
				//check to make sure the player is still in the bomb target area
				PlaceBomb = true;
			}
			else if( ( gpGlobals->curtime >= (m_fArmedTime - 0.75) ) && ( !m_bBombPlacedAnimation ) )
			{
				//call the c4 Placement animation 
				m_bBombPlacedAnimation = true;

				SendWeaponAnim( ACT_VM_SECONDARYATTACK );
				
#if !defined( CLIENT_DLL )
				// player "place" animation
				//pPlayer->SetAnimation( PLAYER_HOLDBOMB );
#endif
			}
		}
	}

	if ( PlaceBomb && m_bStartedArming )
	{
		m_bStartedArming = false;
		m_fArmedTime = 0;
		
		if( pPlayer->m_bInBombZone )
		{
#if !defined( CLIENT_DLL )

			CPlantedC4 *pC4 = CPlantedC4::ShootSatchelCharge( pPlayer, pPlayer->GetAbsOrigin(), pPlayer->GetAbsAngles() );

			if ( pC4 )
			{
				pC4->SetBombSiteIndex( pPlayer->m_iBombSiteIndex );

				trace_t tr;
				UTIL_TraceEntity( pC4, GetAbsOrigin(), GetAbsOrigin() + Vector(0,0,-200), MASK_SOLID, this, COLLISION_GROUP_NONE, &tr );
				pC4->SetAbsOrigin( tr.endpos );

				CBombTarget *pBombTarget = (CBombTarget*)UTIL_EntityByIndex( pPlayer->m_iBombSiteIndex );
				
				if ( pBombTarget )
				{
					CBaseEntity *pAttachPoint = gEntList.FindEntityByName( NULL, pBombTarget->GetBombMountTarget() );

					if ( pAttachPoint )
					{
						pC4->SetAbsOrigin( pAttachPoint->GetAbsOrigin() );
						pC4->SetAbsAngles( pAttachPoint->GetAbsAngles() );
						pC4->SetParent( pAttachPoint );
					}

					variant_t emptyVariant;
					pBombTarget->AcceptInput( "BombPlanted", pC4, pC4, emptyVariant, 0 );
				}
			}

			IGameEvent * event = gameeventmanager->CreateEvent( "bomb_planted" );
			if( event )
			{
				event->SetInt("userid", pPlayer->GetUserID() );
				event->SetInt("site", pPlayer->m_iBombSiteIndex );
				event->SetInt("posx", pPlayer->GetAbsOrigin().x );
				event->SetInt("posy", pPlayer->GetAbsOrigin().y );
				event->SetInt( "priority", 8 );
				gameeventmanager->FireEvent( event );
			}

			// Fire a beep event also so the bots have a chance to hear the bomb
			event = gameeventmanager->CreateEvent( "bomb_beep" );

			if ( event )
			{
				event->SetInt( "entindex", entindex() );
				gameeventmanager->FireEvent( event );
			}

			pPlayer->SetProgressBarTime( 0 );

			DODGameRules()->m_bBombDropped = false;
			DODGameRules()->m_bBombPlanted = true;

			// Play the plant sound.
			Vector plantPosition = pPlayer->GetAbsOrigin() + Vector( 0, 0, 5 );
			CPASAttenuationFilter filter( plantPosition );
			EmitSound( filter, entindex(), "c4.plant" );

			// release the player from being frozen
			pPlayer->ResetMaxSpeed();

			// No more c4!
			pPlayer->Weapon_Drop( this, NULL, NULL );
			UTIL_Remove( this );
#endif
			//don't allow the planting to start over again next frame.
			m_bBombPlanted = true;

			return;
		}
		else
		{
			ClientPrint( pPlayer, HUD_PRINTCENTER, "#C4_Activated_At_Bomb_Spot" );

#if !defined( CLIENT_DLL )
			//pPlayer->SetAnimation( PLAYER_HOLDBOMB );

			// release the player from being frozen
			pPlayer->ResetMaxSpeed();

			IGameEvent * event = gameeventmanager->CreateEvent( "bomb_abortplant" );
			if( event )
			{
				event->SetInt("userid", pPlayer->GetUserID() );
				event->SetInt("site", pPlayer->m_iBombSiteIndex );
				event->SetInt( "priority", 8 );
				gameeventmanager->FireEvent( event );
			}
#endif

			m_flNextPrimaryAttack = gpGlobals->curtime + 1.0;
			return;
		}
	}

	m_flNextPrimaryAttack = gpGlobals->curtime + 0.3;
	SetWeaponIdleTime( gpGlobals->curtime + SharedRandomFloat("C4IdleTime", 10, 15 ) );
}

void CC4::WeaponIdle()
{
	if ( m_bStartedArming )
	{
		m_bStartedArming = false; //if the player releases the attack button cancel the arming sequence

		CDODPlayer *pPlayer = ToDODPlayer(GetPlayerOwner());
		if ( !pPlayer )
			return;

		#if !defined( CLIENT_DLL )
			// release the player from being frozen
			pPlayer->ResetMaxSpeed();

			m_flNextPrimaryAttack = gpGlobals->curtime + 1.0;

			pPlayer->SetProgressBarTime( 0 );

			IGameEvent * event = gameeventmanager->CreateEvent( "bomb_abortplant" );
			if( event )
			{
				event->SetInt("userid", pPlayer->GetUserID() );
				event->SetInt("site", pPlayer->m_iBombSiteIndex );
				event->SetInt( "priority", 8 );
				gameeventmanager->FireEvent( event );
			}

		#endif 

		// TODO: make this use SendWeaponAnim and activities when the C4 has the activities hooked up.
		if ( pPlayer )
		{
			SendWeaponAnim( ACT_VM_IDLE );
			pPlayer->SetNextAttack( gpGlobals->curtime );
		}

		if(m_bBombPlacedAnimation == true) //this means the placement animation is canceled
			SendWeaponAnim( ACT_VM_DRAW );
		else
			SendWeaponAnim( ACT_VM_IDLE );
	}
}

void CC4::UpdateShieldState( void )
{
	//ADRIANTODO
	CDODPlayer *pPlayer = ToDODPlayer(GetPlayerOwner());
	if ( !pPlayer )
		return;
	
	//if ( pPlayer->HasShield() )
	//{
	//	pPlayer->SetShieldDrawnState( false );

	//	CBaseViewModel *pVM = pPlayer->GetViewModel( 1 );

	//	if ( pVM )
	//	{
	//		pVM->AddEffects( EF_NODRAW );
	//	}
	//		//pPlayer->SetHitBoxSet( 3 );
	//}
	//else
	//	BaseClass::UpdateShieldState();
}


int m_iBeepFrames[NUM_BEEPS] = { 27, 37, 45, 51, 57, 63, 67 };
int iNumArmingAnimFrames = 83;

void CC4::PlayArmingBeeps( void )
{
	float flStartTime = m_fArmedTime - WEAPON_C4_ARM_TIME;

	float flProgress = ( gpGlobals->curtime - flStartTime ) / ( WEAPON_C4_ARM_TIME - 0.75 );

	int currentFrame = (int)( (float)iNumArmingAnimFrames * flProgress );

	int i;
	for( i=0;i<NUM_BEEPS;i++ )
	{
		if( currentFrame <= m_iBeepFrames[i] )
		{
			break;
		}
		else if( !m_bPlayedArmingBeeps[i] )
		{
			m_bPlayedArmingBeeps[i] = true;

			CDODPlayer *owner = ToDODPlayer(GetPlayerOwner());
			Vector soundPosition = owner->GetAbsOrigin() + Vector( 0, 0, 5 );
			CPASAttenuationFilter filter( soundPosition );

			filter.RemoveRecipient( owner );

			// remove anyone that is first person spec'ing the planter
			int i;
			CBasePlayer *pPlayer;
			for( i=1;i<=gpGlobals->maxClients;i++ )
			{
				pPlayer = UTIL_PlayerByIndex( i );

				if ( !pPlayer )
					continue;

				if( pPlayer->GetObserverMode() == OBS_MODE_IN_EYE && pPlayer->GetObserverTarget() == GetOwner() )
				{
					filter.RemoveRecipient( pPlayer );
				}
			}

			EmitSound(filter, entindex(), "c4.click");
			
			break;
		}
	}
}

void CC4::OnPickedUp( CBaseCombatCharacter *pNewOwner )
{
	BaseClass::OnPickedUp( pNewOwner );

#if !defined( CLIENT_DLL )
	CDODPlayer *pPlayer = dynamic_cast<CDODPlayer*>( pNewOwner );

	IGameEvent * event = gameeventmanager->CreateEvent( "bomb_pickup" );
	if ( event )
	{
		event->SetInt( "userid", pPlayer->GetUserID() );
		event->SetInt( "priority", 6 );
		gameeventmanager->FireEvent( event );
	}

	/*if ( pPlayer->m_bShowHints && !(pPlayer->m_iDisplayHistoryBits & DHF_BOMB_RETRIEVED) )
	{
		pPlayer->m_iDisplayHistoryBits |= DHF_BOMB_RETRIEVED;
		pPlayer->HintMessage( "#Hint_you_have_the_bomb", false );
	}
	else*/
	{
		ClientPrint( pPlayer, HUD_PRINTCENTER, "#Got_bomb" );
	}
#endif
}

// HACK - Ask Mike Booth...
#ifndef CLIENT_DLL
	//#include "cs_bot.h"
#endif

void CC4::Drop( const Vector &vecVelocity )
{
#if !defined( CLIENT_DLL )

	m_bStartedArming = false; // stop arming sequence

	if ( !DODGameRules()->m_bBombPlanted ) // its not dropped if its planted
	{
		// tell the bots about the dropped bomb
		//TheCSBots()->SetLooseBomb( this );

		CBasePlayer *pPlayer = dynamic_cast<CBasePlayer *>(GetOwnerEntity());
		Assert( pPlayer );
		if ( pPlayer )
		{
			IGameEvent * event = gameeventmanager->CreateEvent("bomb_dropped" );
			if ( event )
			{
				event->SetInt( "userid", pPlayer->GetUserID() );
				event->SetInt( "priority", 6 );
				gameeventmanager->FireEvent( event );
			}
		}
	}
#endif
	BaseClass::Drop( vecVelocity );
}
#ifdef CLIENT_DLL
// ------------------------------------------------------------------------------------------ //
// CPlantedC4 class.
// For now to show the planted c4 on the radar - client proxy to remove the CBaseAnimating 
// network vars?
// ------------------------------------------------------------------------------------------ //

class C_PlantedC4 : public C_BaseAnimating
{
public:
	DECLARE_CLASS( C_PlantedC4, CBaseAnimating );
	DECLARE_CLIENTCLASS();

	C_PlantedC4();
	virtual ~C_PlantedC4();

	void Spawn( void );
	virtual void SetDormant( bool bDormant );

	void ClientThink( void );

	int GetSecondsRemaining( void ) { return ceil( m_flC4Blow - gpGlobals->curtime ); }

	inline bool IsBombActive( void ) { return m_bBombTicking; }
	CNetworkVar( bool, m_bBombTicking );

	float m_flNextGlow;
	float m_flNextBeep;

	float m_flC4Blow;
	float m_flTimerLength;

	CNetworkVar( float, m_flDefuseLength );	
	CNetworkVar( float, m_flDefuseCountDown ); 

	float GetDefuseProgress( void )
	{	
		float flProgress = 1.0f;

		if( m_flDefuseLength > 0.0 )
		{
			flProgress = ( ( m_flDefuseCountDown - gpGlobals->curtime ) / m_flDefuseLength );
		}

		return flProgress;
	}

	float	m_flNextRadarFlashTime;	// next time to change flash state
	bool	m_bRadarFlash;			// is the flash on or off
};

extern CUtlVector< C_PlantedC4* > g_PlantedC4s;

#define PLANTEDC4_MSG_JUSTBLEW 1

ConVar cl_c4dynamiclight( "cl_c4dynamiclight", "0", 0, "Draw dynamic light when planted c4 flashes" );

IMPLEMENT_CLIENTCLASS_DT(C_PlantedC4, DT_PlantedC4, CPlantedC4)
	RecvPropBool( RECVINFO(m_bBombTicking) ),
	RecvPropFloat( RECVINFO(m_flC4Blow) ),
	RecvPropFloat( RECVINFO(m_flTimerLength) ),
	RecvPropFloat( RECVINFO(m_flDefuseLength) ),
	RecvPropFloat( RECVINFO(m_flDefuseCountDown) ),
END_RECV_TABLE()

CUtlVector< C_PlantedC4* > g_PlantedC4s;

C_PlantedC4::C_PlantedC4()
{
	g_PlantedC4s.AddToTail( this );

	m_flNextRadarFlashTime = gpGlobals->curtime;
	m_bRadarFlash = true;

	// Don't beep right away, leave time for the planting sound
	m_flNextGlow = gpGlobals->curtime + 1.0;
	m_flNextBeep = gpGlobals->curtime + 1.0;
}


C_PlantedC4::~C_PlantedC4()
{
	g_PlantedC4s.FindAndRemove( this );
}

void C_PlantedC4::SetDormant( bool bDormant )
{
	BaseClass::SetDormant( bDormant );
	
	// Remove us from the list of planted C4s.
	if ( bDormant )
	{
		g_PlantedC4s.FindAndRemove( this );
	}
	else
	{
		if ( g_PlantedC4s.Find( this ) == -1 )
			g_PlantedC4s.AddToTail( this );
	}
}

void C_PlantedC4::Spawn( void )
{
	BaseClass::Spawn();

	SetNextClientThink( CLIENT_THINK_ALWAYS );
}

void C_PlantedC4::ClientThink( void )
{
	BaseClass::ClientThink();

	// If it's dormant, don't beep or anything..
	if ( IsDormant() )
		return;

	if ( !m_bBombTicking )
	{
		// disbale C4 thinking if not armed
		SetNextClientThink( CLIENT_THINK_NEVER );
		return;
	}


	if( gpGlobals->curtime > m_flNextBeep )
	{
		// as it gets closer to going off, increase the radius

		CLocalPlayerFilter filter;
		float attenuation;
		float freq;

		//the percent complete of the bomb timer
		float fComplete = ( ( m_flC4Blow - gpGlobals->curtime ) / m_flTimerLength );
		
		fComplete = clamp( fComplete, 0.0f, 1.0f );

		attenuation = min( 0.3 + 0.6 * fComplete, 1.0 );
		
		CSoundParameters params;

		if ( GetParametersForSound( "C4.PlantSound", params, NULL ) )
		{
			EmitSound_t ep( params );
			ep.m_SoundLevel = ATTN_TO_SNDLVL( attenuation );
			ep.m_pOrigin = &GetAbsOrigin();

			EmitSound( filter, SOUND_FROM_WORLD, ep );
		}

		freq = max( 0.1 + 0.9 * fComplete, 0.15 );

		m_flNextBeep = gpGlobals->curtime + freq;
	}

	if( gpGlobals->curtime > m_flNextGlow )
	{
		int modelindex = modelinfo->GetModelIndex( "sprites/ledglow.vmt" );

		float scale = 0.8f;
		Vector vPos = GetAbsOrigin();
		const Vector offset( 0, 0, 4 );

		// See if the c4 ended up underwater - we need to pull the flash up, or it won't get seen
		//if ( enginetrace->GetPointContents( vPos ) & (CONTENTS_WATER|CONTENTS_SLIME) )
		//{
		//	C_DODPlayer *player = GetLocalOrInEyeCSPlayer();
		//	if ( player )
		//	{
		//		const Vector& eyes = player->EyePosition();

		//		if ( ( enginetrace->GetPointContents( eyes ) & (CONTENTS_WATER|CONTENTS_SLIME) ) == 0 )
		//		{
		//			// trace from the player to the water
		//			trace_t waterTrace;
		//			UTIL_TraceLine( eyes, vPos, (CONTENTS_WATER|CONTENTS_SLIME), player, COLLISION_GROUP_NONE, &waterTrace );

		//			if( waterTrace.allsolid != 1 )
		//			{
		//				// now trace from the C4 to the edge of the water (in case there was something solid in the water)
		//				trace_t solidTrace;
		//				UTIL_TraceLine( vPos, waterTrace.endpos, MASK_SOLID, this, COLLISION_GROUP_NONE, &solidTrace );

		//				if( solidTrace.allsolid != 1 )
		//				{
		//					float waterDist = (solidTrace.endpos - vPos).Length();
		//					float remainingDist = (solidTrace.endpos - eyes).Length();

		//					scale = scale * remainingDist / ( remainingDist + waterDist );
		//					vPos = solidTrace.endpos;
		//				}
		//			}
		//		}
		//	}
		//}

		vPos += offset;

		tempents->TempSprite( vPos, vec3_origin, scale, modelindex, kRenderTransAdd, 0, 1.0, 0.05, FTENT_SPRANIMATE | FTENT_SPRANIMATELOOP );

		if( cl_c4dynamiclight.GetBool() )
		{
			dlight_t *dl;

			dl = effects->CL_AllocDlight( entindex() );

			if( dl ) 
			{
				dl->origin = GetAbsOrigin() + offset; // can't use vPos because it might have been moved
				dl->color.r = 255;
				dl->color.g = 0;
				dl->color.b = 0;
				dl->radius = 64;
				dl->die = gpGlobals->curtime + 0.01;
			}
		}

		float freq = 0.1 + 0.9 * ( ( m_flC4Blow - gpGlobals->curtime ) / m_flTimerLength );

		if( freq < 0.15 ) freq = 0.15;

		m_flNextGlow = gpGlobals->curtime + freq;
	}	
}

#endif