/**
 * =============================================================================
 * CS2Fixes
 * Copyright (C) 2023 Source2ZE
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "cdetour.h"
#include "common.h"
#include "module.h"
#include "addresses.h"
#include "commands.h"
#include "interfaces/cs2_interfaces.h"
#include "detours.h"
#include "ctimer.h"
#include "irecipientfilter.h"
#include "entity/ccsplayercontroller.h"
#include "entity/ccsplayerpawn.h"
#include "entity/cbasemodelentity.h"
#include "entity/ccsweaponbase.h"
#include "entity/ctriggerpush.h"
#include "entity/cgamerules.h"
#include "playermanager.h"
#include "igameevents.h"
#include "gameconfig.h"
#include "httpmanager.h"

#define VPROF_ENABLED
#include "tier0/vprof.h"

#include "tier0/memdbgon.h"

extern CGlobalVars *gpGlobals;
extern CEntitySystem *g_pEntitySystem;
extern IGameEventManager2 *g_gameEventManager;
extern CCSGameRules *g_pGameRules;

DECLARE_DETOUR(UTIL_SayTextFilter, Detour_UTIL_SayTextFilter);
DECLARE_DETOUR(UTIL_SayText2Filter, Detour_UTIL_SayText2Filter);
DECLARE_DETOUR(IsHearingClient, Detour_IsHearingClient);
DECLARE_DETOUR(CSoundEmitterSystem_EmitSound, Detour_CSoundEmitterSystem_EmitSound);
DECLARE_DETOUR(CCSWeaponBase_Spawn, Detour_CCSWeaponBase_Spawn);
DECLARE_DETOUR(TriggerPush_Touch, Detour_TriggerPush_Touch);
DECLARE_DETOUR(CGameRules_Constructor, Detour_CGameRules_Constructor);

void FASTCALL Detour_CGameRules_Constructor(CGameRules *pThis)
{
	g_pGameRules = (CCSGameRules*)pThis;
	CGameRules_Constructor(pThis);
}

void FASTCALL Detour_TriggerPush_Touch(CTriggerPush* pPush, Z_CBaseEntity* pOther)
{
	MoveType_t movetype = pOther->m_MoveType();

	// VPhysics handling doesn't need any changes
	if (movetype == MOVETYPE_VPHYSICS)
	{
		TriggerPush_Touch(pPush, pOther);
		return;
	}

	Z_CBaseEntity* pPushEnt = (Z_CBaseEntity*)pPush;

	// SF_TRIG_PUSH_ONCE is handled fine already
	if (pPushEnt->m_spawnflags() & SF_TRIG_PUSH_ONCE)
	{
		TriggerPush_Touch(pPush, pOther);
		return;
	}

	if (movetype == MOVETYPE_NONE || movetype == MOVETYPE_PUSH || movetype == MOVETYPE_NOCLIP)
		return;

	CCollisionProperty* collisionProp = pOther->m_pCollision();
	if (!IsSolid(collisionProp->m_nSolidType(), collisionProp->m_usSolidFlags()))
		return;

	if (!pPush->PassesTriggerFilters(pOther))
		return;

	if (pOther->m_CBodyComponent()->m_pSceneNode()->m_pParent())
		return;

	Vector vecAbsDir;

	matrix3x4_t mat = pPushEnt->m_CBodyComponent()->m_pSceneNode()->EntityToWorldTransform();
	
	Vector pushDir = pPush->m_vecPushDirEntitySpace();

	// i had issues with vectorrotate on linux so i did it here
	vecAbsDir.x = pushDir.x * mat[0][0] + pushDir.y * mat[0][1] + pushDir.z * mat[0][2];
	vecAbsDir.y = pushDir.x * mat[1][0] + pushDir.y * mat[1][1] + pushDir.z * mat[1][2];
	vecAbsDir.z = pushDir.x * mat[2][0] + pushDir.y * mat[2][1] + pushDir.z * mat[2][2];

	Vector vecPush = vecAbsDir * pPush->m_flPushSpeed();

	uint32 flags = pOther->m_fFlags();

	if (flags & (FL_BASEVELOCITY))
	{
		vecPush = vecPush + pOther->m_vecBaseVelocity();
	}

	if (vecPush.z > 0 && (flags & FL_ONGROUND))
	{
		addresses::SetGroundEntity(pOther, nullptr);
		Vector origin = pOther->GetAbsOrigin();
		origin.z += 1.0f;

		pOther->Teleport(&origin, nullptr, nullptr);
	}

	pOther->m_vecBaseVelocity(vecPush);

	flags |= (FL_BASEVELOCITY);
	pOther->m_fFlags(flags);
}

void FASTCALL Detour_CCSWeaponBase_Spawn(CBaseEntity *pThis, void *a2)
{
	const char *pszClassName = pThis->m_pEntity->m_designerName.String();

#ifdef _DEBUG
	Message("Weapon spawn: %s\n", pszClassName);
#endif

	CCSWeaponBase_Spawn(pThis, a2);

	FixWeapon((CCSWeaponBase *)pThis);
}

void FASTCALL Detour_CSoundEmitterSystem_EmitSound(ISoundEmitterSystemBase *pSoundEmitterSystem, CEntityIndex *a2, IRecipientFilter &filter, uint32 a4, void *a5)
{
	//ConMsg("Detour_CSoundEmitterSystem_EmitSound\n");
	CSoundEmitterSystem_EmitSound(pSoundEmitterSystem, a2, filter, a4, a5);
}

bool FASTCALL Detour_IsHearingClient(void* serverClient, int index)
{
	ZEPlayer* player = g_playerManager->GetPlayer(index);
	if (player && player->IsMuted())
		return false;

	return IsHearingClient(serverClient, index);
}

void FASTCALL Detour_UTIL_SayTextFilter(IRecipientFilter &filter, const char *pText, CCSPlayerController *pPlayer, uint64 eMessageType)
{
	if (pPlayer)
		return UTIL_SayTextFilter(filter, pText, pPlayer, eMessageType);

	char buf[256];
	V_snprintf(buf, sizeof(buf), "%s %s", " \7CONSOLE:\4", pText + sizeof("Console:"));

	UTIL_SayTextFilter(filter, buf, pPlayer, eMessageType);
}


void FASTCALL Detour_UTIL_SayText2Filter(
	IRecipientFilter &filter,
	CCSPlayerController *pEntity,
	uint64 eMessageType,
	const char *msg_name,
	const char *param1,
	const char *param2,
	const char *param3,
	const char *param4)
{
	int entindex = filter.GetRecipientIndex(0).Get() + 1;
	CCSPlayerController *target = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)entindex);

	     int iCommandPlayer = pEntity->GetPlayerSlot();

    ZEPlayer *pPlayer = g_playerManager->GetPlayer(iCommandPlayer);
	
		char sBuffer[256];
        if (pPlayer->IsAdminFlagSet(ADMFLAG_CUSTOM1)) // o
        {
            V_snprintf(sBuffer, sizeof(sBuffer), " \1★ [\4Helper\1] \10%s: \5%s", param1, param2);
        }
        else if (pPlayer->IsAdminFlagSet(ADMFLAG_CUSTOM2)) // p
        {
            V_snprintf(sBuffer, sizeof(sBuffer), " \1★ [\4Admin\1] \10%s: \5%s", param1, param2);
        }
        else if (pPlayer->IsAdminFlagSet(ADMFLAG_CUSTOM3)) // q
        {
            V_snprintf(sBuffer, sizeof(sBuffer), " \1★ [\4Mod\1]\10 %s: \5%s", param1, param2);
        }
        else if (pPlayer->IsAdminFlagSet(ADMFLAG_CUSTOM4)) // r
        {
            V_snprintf(sBuffer, sizeof(sBuffer), " \1★ [\4Manager\1]\10 %s: \5%s", param1, param2);
        }
		else if (pPlayer->IsAdminFlagSet(ADMFLAG_CUSTOM5)) // u
        {
            V_snprintf(sBuffer, sizeof(sBuffer), " \1★ [\26Gold Member\1]\10 %s: \5%s", param1, param2);
        }
		else if (pPlayer->IsAdminFlagSet(ADMFLAG_CUSTOM6)) // s
        {
            V_snprintf(sBuffer, sizeof(sBuffer), " \1★ [\7Co-Owner\1]\10 %s: \5%s", param1, param2);
        }
        else if (pPlayer->IsAdminFlagSet(ADMFLAG_CUSTOM7)) // v
        {
            V_snprintf(sBuffer, sizeof(sBuffer), " \1★[\3Owner\1]\10 %s: \5%s", param1, param2);
        }
        else if (pPlayer->IsAdminFlagSet(ADMFLAG_CUSTOM8)) // t
        {
            V_snprintf(sBuffer, sizeof(sBuffer), " \1★[\13Fondator\1]\7 %s: \1%s", param1, param2);
        }
     
        else {
            V_snprintf(sBuffer, sizeof(sBuffer), " \1★\4Player\1 %s: \1%s", param1, param2);
        }
    
    UTIL_SayTextFilter(filter, sBuffer, pEntity, eMessageType);
}

void Detour_Log()
{
	return;
}

bool FASTCALL Detour_IsChannelEnabled(LoggingChannelID_t channelID, LoggingSeverity_t severity)
{
	return false;
}

CDetour<decltype(Detour_Log)> g_LoggingDetours[] =
{
	CDetour<decltype(Detour_Log)>( Detour_Log, "Msg" ),
	//CDetour<decltype(Detour_Log)>( Detour_Log, "?ConMsg@@YAXPEBDZZ" ),
	//CDetour<decltype(Detour_Log)>( Detour_Log, "?ConColorMsg@@YAXAEBVColor@@PEBDZZ" ),
	CDetour<decltype(Detour_Log)>( Detour_Log, "ConDMsg" ),
	CDetour<decltype(Detour_Log)>( Detour_Log, "DevMsg" ),
	CDetour<decltype(Detour_Log)>( Detour_Log, "Warning" ),
	CDetour<decltype(Detour_Log)>( Detour_Log, "DevWarning" ),
	//CDetour<decltype(Detour_Log)>( Detour_Log, "?DevWarning@@YAXPEBDZZ" ),
	CDetour<decltype(Detour_Log)>( Detour_Log, "LoggingSystem_Log" ),
	CDetour<decltype(Detour_Log)>( Detour_Log, "LoggingSystem_LogDirect" ),
	CDetour<decltype(Detour_Log)>( Detour_Log, "LoggingSystem_LogAssert" ),
	//CDetour<decltype(Detour_Log)>( Detour_IsChannelEnabled, "LoggingSystem_IsChannelEnabled" ),
};

void ToggleLogs()
{
	static bool bBlock = false;

	if (!bBlock)
	{
		Message("Logging is now OFF.\n");

		for (int i = 0; i < sizeof(g_LoggingDetours) / sizeof(*g_LoggingDetours); i++)
			g_LoggingDetours[i].EnableDetour();
	}
	else
	{
		Message("Logging is now ON.\n");

		for (int i = 0; i < sizeof(g_LoggingDetours) / sizeof(*g_LoggingDetours); i++)
			g_LoggingDetours[i].DisableDetour();
	}

	bBlock = !bBlock;
}

CUtlVector<CDetourBase *> g_vecDetours;

bool InitDetours(CGameConfig *gameConfig)
{
	bool success = true;

	g_vecDetours.PurgeAndDeleteElements();

	for (int i = 0; i < sizeof(g_LoggingDetours) / sizeof(*g_LoggingDetours); i++)
	{
		if (!g_LoggingDetours[i].CreateDetour(gameConfig))
			success = false;
	}

	if (!UTIL_SayTextFilter.CreateDetour(gameConfig))
		success = false;
	UTIL_SayTextFilter.EnableDetour();

	if (!UTIL_SayText2Filter.CreateDetour(gameConfig))
		success = false;
	UTIL_SayText2Filter.EnableDetour();

	if (!IsHearingClient.CreateDetour(gameConfig))
		success = false;
	IsHearingClient.EnableDetour();

	if (!CSoundEmitterSystem_EmitSound.CreateDetour(gameConfig))
		success = false;
	CSoundEmitterSystem_EmitSound.EnableDetour();

	if (!CCSWeaponBase_Spawn.CreateDetour(gameConfig))
		success = false;
	CCSWeaponBase_Spawn.EnableDetour();

	if (!TriggerPush_Touch.CreateDetour(gameConfig))
		success = false;
	TriggerPush_Touch.EnableDetour();

	if (!CGameRules_Constructor.CreateDetour(gameConfig))
		success = false;
	CGameRules_Constructor.EnableDetour();



	return success;
}

void FlushAllDetours()
{
	g_vecDetours.Purge();
}
