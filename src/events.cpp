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

#include "common.h"
#include "KeyValues.h"
#include "commands.h"
#include "ctimer.h"
#include "eventlistener.h"
#include "entity/cbaseplayercontroller.h"
#include "cdetour.h"

#include "tier0/memdbgon.h"
#include "playermanager.h"
#include "entity/ccsplayercontroller.h"
#include "adminsystem.h"
#include <memory>
#include <string>
#include <stdexcept>

extern IGameEventManager2 *g_gameEventManager;
extern IServerGameClients *g_pSource2GameClients;
extern CEntitySystem *g_pEntitySystem;
extern CGlobalVars *gpGlobals;
extern IVEngineServer2* g_pEngineServer2;


CUtlVector<CGameEventListener *> g_vecEventListeners;

void RegisterEventListeners()
{
	static bool bRegistered = false;

	if (bRegistered || !g_gameEventManager)
		return;

	FOR_EACH_VEC(g_vecEventListeners, i)
	{
		g_gameEventManager->AddListener(g_vecEventListeners[i], g_vecEventListeners[i]->GetEventName(), true);
	}

	bRegistered = true;
}

void UnregisterEventListeners()
{
	if (!g_gameEventManager)
		return;

	FOR_EACH_VEC(g_vecEventListeners, i)
	{
		g_gameEventManager->RemoveListener(g_vecEventListeners[i]);
	}

	g_vecEventListeners.Purge();
}


int g_iBombTimerCounter = 0;

GAME_EVENT_F(bomb_planted)
{
    ConVar* cvar = g_pCVar->GetConVar(g_pCVar->FindConVar("mp_c4timer"));

    int iC4;
    memcpy(&iC4, &cvar->values, sizeof(iC4));

    g_iBombTimerCounter = iC4;

    new CTimer(1.0f, false, []()
    {
        if (g_iBombTimerCounter <= 0)
            return -1.0f;

        g_iBombTimerCounter--;

        ClientPrintAll(HUD_PRINTCENTER, "C4: %d", g_iBombTimerCounter);
        return 1.0f;
    });
}

GAME_EVENT_F(bomb_defused)
{
    g_iBombTimerCounter = 0;
}

GAME_EVENT_F(round_start)
{
    g_iBombTimerCounter = 0;

    bool useServerCommand = true;

    new CTimer(60.0f, true, [useServerCommand]() mutable 
    {
        if (useServerCommand) {
            g_pEngineServer2->ServerCommand("echo test");
        } else {
            g_pEngineServer2->ServerCommand("echo test");
        }

        useServerCommand = !useServerCommand; 
        return 60.0f; 
    });
}

GAME_EVENT_F(round_end)
{
    g_iBombTimerCounter = 0;
}

GAME_EVENT_F(player_team)
{
		pEvent->SetBool("silent", true);
}

// CONVAR_TODO: have a convar for forcing debris collision



GAME_EVENT_F(player_spawn)
{
	CCSPlayerController *pController = (CCSPlayerController *)pEvent->GetPlayerController("userid");

	if (!pController)
		return;

		int iPlayer = pController->GetPlayerSlot();
		ZEPlayer* pZEPlayer = g_playerManager->GetPlayer(iPlayer);

	if (pZEPlayer)
		{
			pZEPlayer->SetUsedMedkit(false);
		}

	CHandle<CCSPlayerController> hController = pController->GetHandle();

	// Gotta do this on the next frame...
	new CTimer(0.0f, false, [hController]()
	{
		CCSPlayerController *pController = hController.Get();

		if (!pController || !pController->m_bPawnIsAlive())
			return -1.0f;

		CBasePlayerPawn *pPawn = pController->GetPawn();

		// Just in case somehow there's health but the player is, say, an observer
		if (!pPawn || !pPawn->IsAlive())
			return -1.0f;

		int iPlayer = pController->GetPlayerSlot();
		ZEPlayer* pZEPlayer = g_playerManager->GetPlayer(iPlayer);

		if(!pZEPlayer)
		return -1.0f;

		if (pZEPlayer->IsAdminFlagSet(ADMFLAG_CUSTOM1))
        {
            pController->m_szClan("[HELPER]");
        }
        else if (pZEPlayer->IsAdminFlagSet(ADMFLAG_CUSTOM2))
        {
            pController->m_szClan("[ADMINISTRATOR]");
        }
        else if (pZEPlayer->IsAdminFlagSet(ADMFLAG_CUSTOM3))
        {
            pController->m_szClan("[MODERATOR]");
        }
        else if (pZEPlayer->IsAdminFlagSet(ADMFLAG_CUSTOM4))
        {
            pController->m_szClan("[VETERAN]");
        }
        else if (pZEPlayer->IsAdminFlagSet(ADMFLAG_CUSTOM5))
        {
            pController->m_szClan("[MANAGER]");
        }
        else if (pZEPlayer->IsAdminFlagSet(ADMFLAG_CUSTOM6))
        {
            pController->m_szClan("[CO-OWNER]");
        }
		else if (pZEPlayer->IsAdminFlagSet(ADMFLAG_CUSTOM7))
        {
            pController->m_szClan("[TESTER]");
        }
		else if (pZEPlayer->IsAdminFlagSet(ADMFLAG_CUSTOM8))
        {
            pController->m_szClan("[SUPERVIZOR]");
        }
        else if (pZEPlayer->IsAdminFlagSet(ADMFLAG_CHEATS))
        {
            pController->m_szClan("[OWNER]");
        }
        else {
			pController->m_szClan("[Player]");
        }	

		return -1.0f;
	});
}

GAME_EVENT_F(player_hurt)
{
    CBasePlayerController *pController = (CBasePlayerController*)pEvent->GetPlayerController("userid");
    ZEPlayer* pZEPlayer = g_playerManager->GetPlayer(pController->GetPlayerSlot());

    CBasePlayerController* died = (CBasePlayerController*)pEvent->GetPlayerController("userid");
    CBasePlayerController* killer = (CBasePlayerController*)pEvent->GetPlayerController("attacker");
    ZEPlayer* pZEKiller = g_playerManager->GetPlayer(killer->GetPlayerSlot());  // Get the ZEPlayer object for the killer

    uint16 health = pEvent->GetInt("dmg_health");

    if (pZEKiller->IsAdminFlagSet(ADMFLAG_CONVARS))  // Check the flag on the killer
    {
        ClientPrint(killer, HUD_PRINTCENTER, "-\4%d ", health);
    }
}

GAME_EVENT_F(player_death)
{

	CBasePlayerController *pController = (CBasePlayerController*)pEvent->GetPlayerController("userid");
	CBasePlayerController *pAttacker = (CBasePlayerController*)pEvent->GetPlayerController("attacker");
	float distance = pEvent->GetFloat("distance");

	if (!pController || !pAttacker)
		return;

	ClientPrint(pController, HUD_PRINTTALK, CHAT_PREFIX"You were killed by \4%s \1from \2%.1fm \1away.", pAttacker->GetPlayerName(), distance);
	ClientPrint(pAttacker, HUD_PRINTTALK, CHAT_PREFIX"You killed \4%s \1from \4%.1fm \1away.", pController->GetPlayerName(), distance);
}