//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: implementation of player info manager
//
//=============================================================================//
#ifndef PLAYERINFOMANAGER_H
#define PLAYERINFOMANAGER_H
#ifdef _WIN32
#pragma once
#endif


#include "game/server/iplayerinfo.h"

//-----------------------------------------------------------------------------
// Purpose: interface for plugins to get player info
//-----------------------------------------------------------------------------
class CPlayerInfoManager: public IPlayerInfoManager
{
public:
	virtual IPlayerInfo *GetPlayerInfo( edict_t *pEdict );
	virtual IPlayerInfo *GetPlayerInfo( int index );
	virtual CGlobalVars *GetGlobalVars();
	// accessor to hook into aliastoweaponid
	virtual int			AliasToWeaponId(const char *weaponName);
	// accessor to hook into weaponidtoalias
	virtual const char *WeaponIdToAlias(int weaponId);

};

class CPluginBotManager: public IBotManager
{
public:
	virtual IBotController *GetBotController( edict_t *pEdict );
	virtual edict_t *CreateBot( const char *botname );
};

#endif
