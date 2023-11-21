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

#include "detours.h"
#include "common.h"
#include "utlstring.h"
#include "recipientfilters.h"
#include "commands.h"
#include "utils/entity.h"
#include "entity/cbaseentity.h"
#include "entity/ccsweaponbase.h"
#include "entity/ccsplayercontroller.h"
#include "entity/ccsplayerpawn.h"
#include "entity/cbasemodelentity.h"
#include "playermanager.h"
#include "adminsystem.h"
#include "ctimer.h"
#include "httpmanager.h"
#undef snprintf
#include "vendor/nlohmann/json.hpp"

#include "tier0/memdbgon.h"

using json = nlohmann::json;

extern CEntitySystem *g_pEntitySystem;
extern IVEngineServer2* g_pEngineServer2;
extern ISteamHTTP* g_http;
extern CGlobalVars *gpGlobals;

WeaponMapEntry_t WeaponMap[] = {
	{"bizon",		  "weapon_bizon",			 1400, 26},
	{"mac10",		  "weapon_mac10",			 1400, 27},
	{"mp7",			"weapon_mp7",				 1700, 23},
	{"mp9",			"weapon_mp9",				 1250, 34},
	{"p90",			"weapon_p90",				 2350, 19},
	{"ump45",		  "weapon_ump45",			 1700, 24},
	{"ak47",			 "weapon_ak47",			 2500, 7},
	{"aug",			"weapon_aug",				 3500, 8},
	{"famas",		  "weapon_famas",			 2250, 10},
	{"galilar",		"weapon_galilar",			 2000, 13},
	{"m4a4",			 "weapon_m4a1",			 3100, 16},
	{"m4a1",			 "weapon_m4a1_silencer", 3100, 60},
	{"sg556",		  "weapon_sg556",			 3500, 39},
	{"awp",			"weapon_awp",				 4750, 9},
	{"g3sg1",		  "weapon_g3sg1",			 5000, 11},
	{"scar20",		   "weapon_scar20",			 5000, 38},
	{"ssg08",		  "weapon_ssg08",			 2500, 40},
	{"mag7",			 "weapon_mag7",			 2000, 29},
	{"nova",			 "weapon_nova",			 1500, 35},
	{"sawedoff",		 "weapon_sawedoff",		 1500, 29},
	{"xm1014",		   "weapon_xm1014",			 3000, 25},
	{"m249",			 "weapon_m249",			 5750, 14},
	{"negev",		  "weapon_negev",			 5750, 28},
	{"deagle",		   "weapon_deagle",			 700 , 1},
	{"elite",		  "weapon_elite",			 800 , 2},
	{"fiveseven",	  "weapon_fiveseven",		 500 , 3},
	{"glock",		  "weapon_glock",			 200 , 4},
	{"hkp2000",		"weapon_hkp2000",			 200 , 32},
	{"p250",			 "weapon_p250",			 300 , 36},
	{"tec9",			 "weapon_tec9",			 500 , 30},
	{"usp_silencer",	 "weapon_usp_silencer",	 200 , 61},
	{"cz75a",		  "weapon_cz75a",			 500 , 63},
	{"revolver",		 "weapon_revolver",		 600 , 64},
	{"he",			"weapon_hegrenade",			 300 , 44, 1},
	{"molotov",		"weapon_molotov",			 850 , 46, 1},
	{"knife",		"weapon_knife",				 0	 , 42},	// default CT knife
	{"kevlar",		   "item_kevlar",			 600 , 50},
};

void ParseWeaponCommand(CCSPlayerController *pController, const char *pszWeaponName)
{
	if (!pController || !pController->m_hPawn())
		return;

	CCSPlayerPawn* pPawn = (CCSPlayerPawn*)pController->GetPawn();

	for (int i = 0; i < sizeof(WeaponMap) / sizeof(*WeaponMap); i++)
	{
		WeaponMapEntry_t weaponEntry = WeaponMap[i];

		if (!V_stricmp(pszWeaponName, weaponEntry.command))
		{
			if (pController->m_hPawn()->m_iHealth() <= 0) {
				ClientPrint(pController, HUD_PRINTTALK, CHAT_PREFIX"You can only buy weapons when alive.");
				return;
			}
			CCSPlayer_ItemServices *pItemServices = pPawn->m_pItemServices;
			int money = pController->m_pInGameMoneyServices->m_iAccount;
			if (money >= weaponEntry.iPrice)
			{
				if (weaponEntry.maxAmount)
				{
					CUtlVector<WeaponPurchaseCount_t>* weaponPurchases = pPawn->m_pActionTrackingServices->m_weaponPurchasesThisRound().m_weaponPurchases;
					bool found = false;
					FOR_EACH_VEC(*weaponPurchases, i)
					{
						WeaponPurchaseCount_t& purchase = (*weaponPurchases)[i];
						if (purchase.m_nItemDefIndex == weaponEntry.iItemDefIndex)
						{
							if (purchase.m_nCount >= weaponEntry.maxAmount)
							{
								ClientPrint(pController, HUD_PRINTTALK, CHAT_PREFIX"You cannot use !%s anymore(Max %i)", weaponEntry.command, weaponEntry.maxAmount);
								return;
							}
							purchase.m_nCount += 1;
							found = true;
							break;
						}
					}

					if (!found)
					{
						WeaponPurchaseCount_t purchase = {};

						purchase.m_nCount = 1;
						purchase.m_nItemDefIndex = weaponEntry.iItemDefIndex;

						weaponPurchases->AddToTail(purchase);
					}
				}

				pController->m_pInGameMoneyServices->m_iAccount = money - weaponEntry.iPrice;
				pItemServices->GiveNamedItem(weaponEntry.szWeaponName);
			}

			break;
		}
	}
}

void ParseChatCommand(const char *pMessage, CCSPlayerController *pController)
{
	if (!pController || !pController->IsConnected())
		return;

	CCommand args;
	args.Tokenize(pMessage);

	uint16 index = g_CommandList.Find(hash_32_fnv1a_const(args[0]));

	if (g_CommandList.IsValidIndex(index))
	{
		(*g_CommandList[index])(args, pController);
	}
	else
	{
		ParseWeaponCommand(pController, args[0]);
	}
}

bool CChatCommand::CheckCommandAccess(CBasePlayerController *pPlayer, uint64 flags)
{
	if (!pPlayer)
		return false;

	int slot = pPlayer->GetPlayerSlot();

	ZEPlayer *pZEPlayer = g_playerManager->GetPlayer(slot);

	if (!pZEPlayer->IsAdminFlagSet(flags))
	{
		ClientPrint(pPlayer, HUD_PRINTTALK, CHAT_PREFIX "You don't have access to this command.");
		return false;
	}

	return true;
}

void ClientPrintAll(int hud_dest, const char *msg, ...)
{
	va_list args;
	va_start(args, msg);

	char buf[256];
	V_vsnprintf(buf, sizeof(buf), msg, args);

	va_end(args);

	addresses::UTIL_ClientPrintAll(hud_dest, buf, nullptr, nullptr, nullptr, nullptr);
	ConMsg("%s\n", buf);
}

void ClientPrint(CBasePlayerController *player, int hud_dest, const char *msg, ...)
{
	va_list args;
	va_start(args, msg);

	char buf[256];
	V_vsnprintf(buf, sizeof(buf), msg, args);

	va_end(args);

	if (player)
		addresses::ClientPrint(player, hud_dest, buf, nullptr, nullptr, nullptr, nullptr);
	else
		ConMsg("%s\n", buf);
}

// make a command that prints test

CON_COMMAND_CHAT(vipinfo, "vip info")
{
	if (!player)
		return;

	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"\1Starting health: \13 100-115.");
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"\1Starting armor: \13 110-120.");
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"\1Money add every round: \13 1000-3000.");
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"\1Starting with: \13 defeuser, he, smoke, molotov, flashbang.");
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"\1Smoke color: \13r\4a\24i\17n\3b\14o\16w.");
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"\1To buy \13V\17I\24P, join our discord: \13 https://discord.com/invite/F5MJ3gKeD2");
}

CON_COMMAND_CHAT(rs, "reset your score")
{
	if (!player)
		return;

	player->m_pActionTrackingServices->m_matchStats().m_iKills = 0;
	player->m_pActionTrackingServices->m_matchStats().m_iDeaths = 0;
	player->m_pActionTrackingServices->m_matchStats().m_iAssists = 0;
	player->m_pActionTrackingServices->m_matchStats().m_iDamage = 0;
	player->m_iScore = 0;
	player->m_iMVPs = 0;

	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"You successfully reset your score.");
}

CON_COMMAND_CHAT(preturi, "price info")
{
	if (!player)
		return;


	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"-------------------------------------------------");
        ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX" \x01 Discord Server: https://discord.com/invite/F5MJ3gKeD2");
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"-------------------------------------------------");
}

CON_COMMAND_CHAT(prices, "prices info")
{
	if (!player)
		return;


	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"-------------------------------------------------");
        ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX" \x01 Discord Server: https://discord.com/invite/F5MJ3gKeD2");
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"-------------------------------------------------");
}


CON_COMMAND_CHAT(lionzew, "lionzew contact")
{
	if (!player)
		return;


	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"-------------------------------------------------");
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX" \x01 Direct Profile Link: \x13 https://steamcommunity.com/id/lionzew/ \01");
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX" \x01 Linked Profile:  \x13 og-stars.ro/lionzew \x01");
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX" \x01 Discord: \x06 lionzew \x01");
        ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX" \x01 Discord Server: https://discord.com/invite/F5MJ3gKeD2");
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"-------------------------------------------------");
}

CON_COMMAND_CHAT(discord, "discord")
{
	if (!player)
		return;


	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"-------------------------------------------------");
        ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX" \x01 Discord Server: https://discord.com/invite/F5MJ3gKeD2");
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"-------------------------------------------------");
}

CON_COMMAND_CHAT(lion, "lion contact")
{
	if (!player)
		return;


	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"-------------------------------------------------");
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX" \x01 Direct Profile Link: \x13 https://steamcommunity.com/id/lionzew/ \01");
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX" \x01 Linked Profile:  \x13 og-stars.ro/lionzew \x01");
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX" \x01 Discord: \x06 lionzew \x01");
        ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX" \x01 Discord Server: https://discord.com/invite/F5MJ3gKeD2");
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"-------------------------------------------------");
}

CON_COMMAND_CHAT(godzilla, "godzilla contact")
{
	if (!player)
		return;


	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"-------------------------------------------------");
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX" \x01 Direct Profile Link: \x13 https://steamcommunity.com/profiles/76561199373910807 \01");
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX" \x01 Linked Profile:  \x13 og-stars.ro/godzilla \x01");
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX" \x01 Discord: \x06 GoDzIlLa \x01");
        ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX" \x01 Discord Server: https://discord.com/invite/F5MJ3gKeD2");
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"-------------------------------------------------");
}

CON_COMMAND_CHAT(who, "get list of all admin players online")
{
    if (!player)
        return;

	int iCommandPlayer = player->GetPlayerSlot();

	ZEPlayer* pPlayer1 = g_playerManager->GetPlayer(iCommandPlayer);


	if (!pPlayer1->IsAdminFlagSet(ADMFLAG_CHEATS))
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"You don't have access to this command.");
		return;
	}

    const char* pszFlagNames[] = {
        "ADMFLAG_CUSTOM1",
        "ADMFLAG_CUSTOM2",
        "ADMFLAG_CUSTOM3",
        "ADMFLAG_CUSTOM4",
        "ADMFLAG_CUSTOM5",
        "ADMFLAG_CUSTOM6",
        "ADMFLAG_CUSTOM7",
        "ADMFLAG_CUSTOM8"
    };

    const char* pszCustomNames[] = {
        "Helper",
        "Admin",
        "Mod",
        "Manager",
	"Gold Member",
	"Co-Owner",
	"Owner",
	"Fondator"
    };

    const int iNumFlags = sizeof(pszFlagNames) / sizeof(pszFlagNames[0]);

    uint64_t uiFlagValues[] = {
        ADMFLAG_CUSTOM1,
        ADMFLAG_CUSTOM2,
        ADMFLAG_CUSTOM3,
        ADMFLAG_CUSTOM4,
        ADMFLAG_CUSTOM5,
        ADMFLAG_CUSTOM6,
        ADMFLAG_CUSTOM7,
        ADMFLAG_CUSTOM8
     
    };

    for (int i = 1; i <= gpGlobals->maxClients; i++)
    {
        CBasePlayerController* cPlayer = (CBasePlayerController*)g_pEntitySystem->GetBaseEntity((CEntityIndex)i);
        if (!cPlayer)
            continue;

        ZEPlayer* pPlayer = g_playerManager->GetPlayer(i - 1);
        if (!pPlayer)
            continue;

        std::string sAdminFlags = "";
        for (int j = 0; j < iNumFlags; j++)
        {
            if (pPlayer->IsAdminFlagSet(uiFlagValues[j]))
            {
                if (j < 4)
                {
                    sAdminFlags += pszCustomNames[j];
                }
                else
                {
                    sAdminFlags += pszFlagNames[j];
                }
                sAdminFlags += " ";
            }
        }

        if (sAdminFlags.empty())
            continue;

        ClientPrint(player, HUD_PRINTCONSOLE, CHAT_PREFIX "Player %s has admin flags: %s", cPlayer->GetPlayerName(), sAdminFlags.c_str());
    }
}

CON_COMMAND_CHAT(a, "admins chat")
{
    if (!player)
        return;

    int iCommandPlayer = player->GetPlayerSlot();

    ZEPlayer *pPlayer = g_playerManager->GetPlayer(iCommandPlayer);
    if (args.ArgC() < 2)
    {
        
        ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Usage: /a <message> to admins");
        return;
    }

for (int i = 0; i < MAXPLAYERS; i++)
{
    ZEPlayer* pAdmin = g_playerManager->GetPlayer(i);
    CBasePlayerController* cPlayer = (CBasePlayerController*)g_pEntitySystem->GetBaseEntity((CEntityIndex)(i + 1));

    if (!cPlayer || !pAdmin || pAdmin->IsFakeClient() || !pAdmin->IsAdminFlagSet(ADMFLAG_SLAY))
        continue;
        ClientPrint(cPlayer, HUD_PRINTTALK, " \1(\2ADMINS CHAT\1) \4%s: \1 \13%s", player->GetPlayerName(), args.ArgS());
}
}

CON_COMMAND_CHAT(medic, "medic")
{
	if (!player)
		return;

	int health = 0;
	int iPlayer = player->GetPlayerSlot();

	Z_CBaseEntity* pEnt = (Z_CBaseEntity*)player->GetPawn();

	//ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIXM"pZEPlayer testing...");

	ZEPlayer* pZEPlayer = g_playerManager->GetPlayer(iPlayer);
	if (!pZEPlayer)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"pZEPlayer not valid.");
		return;
	}

	if (!pZEPlayer->IsAdminFlagSet(ADMFLAG_CUSTOM10))//x
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"You don't have acces to this command.");
		return;
	}

	//ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIXM"pZEPlayer valid.");



	if (pZEPlayer->WasUsingMedkit())
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"You already used your medkit in this round");
		return;
	}

	if (pEnt->m_iHealth() < 1)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"You need to be alive in order to use medkit.");
		return;
	}


		if (pEnt->m_iHealth() > 99)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"You have enough life.");
		return;
	}

	health = pEnt->m_iHealth() + 50;

	if (health > 100)
		health = 100;

	pEnt->m_iHealth = health;

	pZEPlayer->SetUsedMedkit(true);

	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Medkit used! Your health is now \4%d", health);
	g_pEngineServer2->ClientCommand(player->GetPlayerSlot(), "play items/healthshot_success_01");
}

// Lookup a weapon classname in the weapon map and "initialize" it.
// Both m_bInitialized and m_iItemDefinitionIndex need to be set for a weapon to be pickable and not crash clients,
// and m_iItemDefinitionIndex needs to be the correct ID from weapons.vdata so the gun behaves as it should.
void FixWeapon(CCSWeaponBase *pWeapon)
{
	// Weapon could be already initialized with the correct data from GiveNamedItem, in that case we don't need to do anything
	if (!pWeapon || pWeapon->m_AttributeManager().m_Item().m_bInitialized())
		return;

	const char *pszClassName = pWeapon->m_pEntity->m_designerName.String();

	for (int i = 0; i < sizeof(WeaponMap) / sizeof(*WeaponMap); i++)
	{
		if (!V_stricmp(WeaponMap[i].szWeaponName, pszClassName))
		{
			DevMsg("Fixing a %s with index = %d and initialized = %d\n", pszClassName,
				pWeapon->m_AttributeManager().m_Item().m_iItemDefinitionIndex(),
				pWeapon->m_AttributeManager().m_Item().m_bInitialized());

			pWeapon->m_AttributeManager().m_Item().m_bInitialized = true;
			pWeapon->m_AttributeManager().m_Item().m_iItemDefinitionIndex = WeaponMap[i].iItemDefIndex;
		}
	}
}
