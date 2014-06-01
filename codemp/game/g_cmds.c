// Copyright (C) 1999-2000 Id Software, Inc.
//
#include "g_local.h"
#include "bg_saga.h"

#include "ui/menudef.h"			// for the voice chats

//rww - for getting bot commands...
int AcceptBotCommand(char *cmd, gentity_t *pl);
//end rww

void WP_SetSaber( int entNum, saberInfo_t *sabers, int saberNum, const char *saberName );

void Cmd_NPC_f( gentity_t *ent );
void SetTeamQuick(gentity_t *ent, int team, qboolean doBegin);

/*
==================
DeathmatchScoreboardMessage

==================
*/
void DeathmatchScoreboardMessage( gentity_t *ent ) {
	char		entry[1024];
	char		string[1400];
	int			stringlength;
	int			i, j;
	gclient_t	*cl;
	int			numSorted, scoreFlags, accuracy, perfect;

	// send the latest information on all clients
	string[0] = 0;
	stringlength = 0;
	scoreFlags = 0;

	numSorted = level.numConnectedClients;

	if (numSorted > MAX_CLIENT_SCORE_SEND)
	{
		numSorted = MAX_CLIENT_SCORE_SEND;
	}

	for (i=0 ; i < numSorted ; i++) {
		int		ping;

		cl = &level.clients[level.sortedClients[i]];

		if ( cl->pers.connected == CON_CONNECTING ) {
			ping = -1;
		} else {
			ping = cl->ps.ping < 999 ? cl->ps.ping : 999;
		}

		if( cl->accuracy_shots ) {
			accuracy = cl->accuracy_hits * 100 / cl->accuracy_shots;
		}
		else {
			accuracy = 0;
		}
		perfect = ( cl->ps.persistant[PERS_RANK] == 0 && cl->ps.persistant[PERS_KILLED] == 0 ) ? 1 : 0;

		Com_sprintf (entry, sizeof(entry),
			" %i %i %i %i %i %i %i %i %i %i %i %i %i %i", level.sortedClients[i],
			cl->ps.persistant[PERS_SCORE], ping, (level.time - cl->pers.enterTime)/60000,
			scoreFlags, g_entities[level.sortedClients[i]].s.powerups, accuracy,
			cl->ps.persistant[PERS_IMPRESSIVE_COUNT],
			cl->ps.persistant[PERS_EXCELLENT_COUNT],
			cl->ps.persistant[PERS_GAUNTLET_FRAG_COUNT],
			cl->ps.persistant[PERS_DEFEND_COUNT],
			cl->ps.persistant[PERS_ASSIST_COUNT],
			perfect,
			cl->ps.persistant[PERS_CAPTURES]);
		j = strlen(entry);
		if (stringlength + j > 1022)
			break;
		strcpy (string + stringlength, entry);
		stringlength += j;
	}

	//still want to know the total # of clients
	i = level.numConnectedClients;

	trap->SendServerCommand( ent-g_entities, va("scores %i %i %i%s", i,
		level.teamScores[TEAM_RED], level.teamScores[TEAM_BLUE],
		string ) );
}


/*
==================
Cmd_Score_f

Request current scoreboard information
==================
*/
void Cmd_Score_f( gentity_t *ent ) {
	DeathmatchScoreboardMessage( ent );
}

/*
==================
ConcatArgs
==================
*/
char	*ConcatArgs( int start ) {
	int		i, c, tlen;
	static char	line[MAX_STRING_CHARS];
	int		len;
	char	arg[MAX_STRING_CHARS];

	len = 0;
	c = trap->Argc();
	for ( i = start ; i < c ; i++ ) {
		trap->Argv( i, arg, sizeof( arg ) );
		tlen = strlen( arg );
		if ( len + tlen >= MAX_STRING_CHARS - 1 ) {
			break;
		}
		memcpy( line + len, arg, tlen );
		len += tlen;
		if ( i != c - 1 ) {
			line[len] = ' ';
			len++;
		}
	}

	line[len] = 0;

	return line;
}

/*
==================
StringIsInteger
==================
*/
qboolean StringIsInteger( const char *s ) {
	int			i=0, len=0;
	qboolean	foundDigit=qfalse;

	for ( i=0, len=strlen( s ); i<len; i++ )
	{
		if ( !isdigit( s[i] ) )
			return qfalse;

		foundDigit = qtrue;
	}

	return foundDigit;
}

/*
==================
ClientNumberFromString

Returns a player number for either a number or name string
Returns -1 if invalid
==================
*/
int ClientNumberFromString( gentity_t *to, const char *s, qboolean allowconnecting ) {
	gclient_t	*cl;
	int			idnum;
	char		cleanInput[MAX_NETNAME];

	if ( StringIsInteger( s ) )
	{// numeric values could be slot numbers
		idnum = atoi( s );
		if ( idnum >= 0 && idnum < level.maxclients )
		{
			cl = &level.clients[idnum];
			if ( cl->pers.connected == CON_CONNECTED )
				return idnum;
			else if ( allowconnecting && cl->pers.connected == CON_CONNECTING )
				return idnum;
		}
	}

	Q_strncpyz( cleanInput, s, sizeof(cleanInput) );
	Q_StripColor( cleanInput );

	for ( idnum=0,cl=level.clients; idnum < level.maxclients; idnum++,cl++ )
	{// check for a name match
		if ( cl->pers.connected != CON_CONNECTED )
			if ( !allowconnecting || cl->pers.connected < CON_CONNECTING )
				continue;

		if ( !Q_stricmp( cl->pers.netname_nocolor, cleanInput ) )
			return idnum;
	}

	trap->SendServerCommand( to-g_entities, va( "print \"User %s is not on the server\n\"", s ) );
	return -1;
}

/*
==================
Cmd_Give_f

Give items to a client
==================
*/
void G_Give( gentity_t *ent, const char *name, const char *args, int argc )
{
	gitem_t		*it;
	int			i;
	qboolean	give_all = qfalse;
	gentity_t	*it_ent;
	trace_t		trace;

	if ( !Q_stricmp( name, "all" ) )
		give_all = qtrue;

	if ( give_all )
	{
		for ( i=0; i<HI_NUM_HOLDABLE; i++ )
			ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << i);
	}

	if ( give_all || !Q_stricmp( name, "health") )
	{
		if ( argc == 3 )
			ent->health = Com_Clampi( 1, ent->client->ps.stats[STAT_MAX_HEALTH], atoi( args ) );
		else
		{
			if ( level.gametype == GT_SIEGE && ent->client->siegeClass != -1 )
				ent->health = bgSiegeClasses[ent->client->siegeClass].maxhealth;
			else
				ent->health = ent->client->ps.stats[STAT_MAX_HEALTH];
		}
		if ( !give_all )
			return;
	}

	if ( give_all || !Q_stricmp( name, "armor" ) || !Q_stricmp( name, "shield" ) )
	{
		if ( argc == 3 )
			ent->client->ps.stats[STAT_ARMOR] = Com_Clampi( 0, ent->client->ps.stats[STAT_MAX_HEALTH], atoi( args ) );
		else
		{
			if ( level.gametype == GT_SIEGE && ent->client->siegeClass != -1 )
				ent->client->ps.stats[STAT_ARMOR] = bgSiegeClasses[ent->client->siegeClass].maxarmor;
			else
				ent->client->ps.stats[STAT_ARMOR] = ent->client->ps.stats[STAT_MAX_HEALTH];
		}

		if ( !give_all )
			return;
	}

	if ( give_all || !Q_stricmp( name, "force" ) )
	{
		if ( argc == 3 )
			ent->client->ps.fd.forcePower = Com_Clampi( 0, ent->client->ps.fd.forcePowerMax, atoi( args ) );
		else
			ent->client->ps.fd.forcePower = ent->client->ps.fd.forcePowerMax;

		if ( !give_all )
			return;
	}

	if ( give_all || !Q_stricmp( name, "weapons" ) )
	{
		ent->client->ps.stats[STAT_WEAPONS] = (1 << (LAST_USEABLE_WEAPON+1)) - ( 1 << WP_NONE );
		if ( !give_all )
			return;
	}

	if ( !give_all && !Q_stricmp( name, "weaponnum" ) )
	{
		ent->client->ps.stats[STAT_WEAPONS] |= (1 << atoi( args ));
		return;
	}

	if ( give_all || !Q_stricmp( name, "ammo" ) )
	{
		int num = 999;
		if ( argc == 3 )
			num = Com_Clampi( 0, 999, atoi( args ) );
		for ( i=AMMO_BLASTER; i<AMMO_MAX; i++ )
			ent->client->ps.ammo[i] = num;
		if ( !give_all )
			return;
	}

	if ( !Q_stricmp( name, "excellent" ) ) {
		ent->client->ps.persistant[PERS_EXCELLENT_COUNT]++;
		return;
	}
	if ( !Q_stricmp( name, "impressive" ) ) {
		ent->client->ps.persistant[PERS_IMPRESSIVE_COUNT]++;
		return;
	}
	if ( !Q_stricmp( name, "gauntletaward" ) ) {
		ent->client->ps.persistant[PERS_GAUNTLET_FRAG_COUNT]++;
		return;
	}
	if ( !Q_stricmp( name, "defend" ) ) {
		ent->client->ps.persistant[PERS_DEFEND_COUNT]++;
		return;
	}
	if ( !Q_stricmp( name, "assist" ) ) {
		ent->client->ps.persistant[PERS_ASSIST_COUNT]++;
		return;
	}

	// spawn a specific item right on the player
	if ( !give_all ) {
		it = BG_FindItem( name );
		if ( !it )
			return;

		it_ent = G_Spawn();
		VectorCopy( ent->r.currentOrigin, it_ent->s.origin );
		it_ent->classname = it->classname;
		G_SpawnItem( it_ent, it );
		if ( !it_ent || !it_ent->inuse )
			return;
		FinishSpawningItem( it_ent );
		if ( !it_ent || !it_ent->inuse )
			return;
		memset( &trace, 0, sizeof( trace ) );
		Touch_Item( it_ent, ent, &trace );
		if ( it_ent->inuse )
			G_FreeEntity( it_ent );
	}
}

void Cmd_Give_f( gentity_t *ent )
{
	char name[MAX_TOKEN_CHARS] = {0};

	trap->Argv( 1, name, sizeof( name ) );
	G_Give( ent, name, ConcatArgs( 2 ), trap->Argc() );
}

void Cmd_GiveOther_f( gentity_t *ent )
{
	char		name[MAX_TOKEN_CHARS] = {0};
	int			i;
	char		otherindex[MAX_TOKEN_CHARS];
	gentity_t	*otherEnt = NULL;

	if ( trap->Argc () < 3 ) {
		trap->SendServerCommand( ent-g_entities, "print \"Usage: giveother <player id> <givestring>\n\"" );
		return;
	}

	trap->Argv( 1, otherindex, sizeof( otherindex ) );
	i = ClientNumberFromString( ent, otherindex, qfalse );
	if ( i == -1 ) {
		return;
	}

	otherEnt = &g_entities[i];
	if ( !otherEnt->inuse || !otherEnt->client ) {
		return;
	}

	if ( (otherEnt->health <= 0 || otherEnt->client->tempSpectate >= level.time || otherEnt->client->sess.sessionTeam == TEAM_SPECTATOR) )
	{
		// Intentionally displaying for the command user
		trap->SendServerCommand( ent-g_entities, va( "print \"%s\n\"", G_GetStringEdString( "MP_SVGAME", "MUSTBEALIVE" ) ) );
		return;
	}

	trap->Argv( 2, name, sizeof( name ) );

	G_Give( otherEnt, name, ConcatArgs( 3 ), trap->Argc()-1 );
}

/*
==================
Cmd_God_f

Sets client to godmode

argv(0) god
==================
*/
void Cmd_God_f( gentity_t *ent ) {
	char *msg = NULL;

	ent->flags ^= FL_GODMODE;
	if ( !(ent->flags & FL_GODMODE) )
		msg = "godmode OFF";
	else
		msg = "godmode ON";

	trap->SendServerCommand( ent-g_entities, va( "print \"%s\n\"", msg ) );
}


/*
==================
Cmd_Notarget_f

Sets client to notarget

argv(0) notarget
==================
*/
void Cmd_Notarget_f( gentity_t *ent ) {
	char *msg = NULL;

	ent->flags ^= FL_NOTARGET;
	if ( !(ent->flags & FL_NOTARGET) )
		msg = "notarget OFF";
	else
		msg = "notarget ON";

	trap->SendServerCommand( ent-g_entities, va( "print \"%s\n\"", msg ) );
}


/*
==================
Cmd_Noclip_f

argv(0) noclip
==================
*/
void Cmd_Noclip_f( gentity_t *ent ) {
	char *msg = NULL;

	if (!(ent->client->pers.bitvalue & (1 << 1)))
	{ // zyk: noclip admin command
		trap->SendServerCommand( ent-g_entities, "print \"You don't have this admin command.\n\"" );
		return;
	}

	if (ent->client->sess.amrpgmode == 2 && ent->client->pers.guardian_mode > 0)
	{
		trap->SendServerCommand( ent-g_entities, "print \"Cannot noclip while in a guardian battle.\n\"" );
		return;
	}

	// zyk: deactivating saber
	if ( ent->client->ps.saberHolstered < 2 )
	{
		Cmd_ToggleSaber_f(ent);
	}

	ent->client->noclip = !ent->client->noclip;
	if ( !ent->client->noclip )
		msg = "noclip OFF";
	else
		msg = "noclip ON";

	trap->SendServerCommand( ent-g_entities, va( "print \"%s\n\"", msg ) );
}


/*
==================
Cmd_LevelShot_f

This is just to help generate the level pictures
for the menus.  It goes to the intermission immediately
and sends over a command to the client to resize the view,
hide the scoreboard, and take a special screenshot
==================
*/
void Cmd_LevelShot_f( gentity_t *ent )
{
	if ( !ent->client->pers.localClient )
	{
		trap->SendServerCommand(ent-g_entities, "print \"The levelshot command must be executed by a local client\n\"");
		return;
	}

	// doesn't work in single player
	if ( level.gametype == GT_SINGLE_PLAYER )
	{
		trap->SendServerCommand(ent-g_entities, "print \"Must not be in singleplayer mode for levelshot\n\"" );
		return;
	}

	BeginIntermission();
	trap->SendServerCommand( ent-g_entities, "clientLevelShot" );
}

#if 0
/*
==================
Cmd_TeamTask_f

From TA.
==================
*/
void Cmd_TeamTask_f( gentity_t *ent ) {
	char userinfo[MAX_INFO_STRING];
	char		arg[MAX_TOKEN_CHARS];
	int task;
	int client = ent->client - level.clients;

	if ( trap->Argc() != 2 ) {
		return;
	}
	trap->Argv( 1, arg, sizeof( arg ) );
	task = atoi( arg );

	trap->GetUserinfo(client, userinfo, sizeof(userinfo));
	Info_SetValueForKey(userinfo, "teamtask", va("%d", task));
	trap->SetUserinfo(client, userinfo);
	ClientUserinfoChanged(client);
}
#endif

void G_Kill( gentity_t *ent ) {
	if ((level.gametype == GT_DUEL || level.gametype == GT_POWERDUEL) &&
		level.numPlayingClients > 1 && !level.warmupTime)
	{
		if (!g_allowDuelSuicide.integer)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "ATTEMPTDUELKILL")) );
			return;
		}
	}

	ent->flags &= ~FL_GODMODE;
	ent->client->ps.stats[STAT_HEALTH] = ent->health = -999;
	player_die (ent, ent, ent, 100000, MOD_SUICIDE);
}

/*
=================
Cmd_Kill_f
=================
*/
void Cmd_Kill_f( gentity_t *ent ) {
	G_Kill( ent );
}

void Cmd_KillOther_f( gentity_t *ent )
{
	int			i;
	char		otherindex[MAX_TOKEN_CHARS];
	gentity_t	*otherEnt = NULL;

	if ( trap->Argc () < 2 ) {
		trap->SendServerCommand( ent-g_entities, "print \"Usage: killother <player id>\n\"" );
		return;
	}

	trap->Argv( 1, otherindex, sizeof( otherindex ) );
	i = ClientNumberFromString( ent, otherindex, qfalse );
	if ( i == -1 ) {
		return;
	}

	otherEnt = &g_entities[i];
	if ( !otherEnt->inuse || !otherEnt->client ) {
		return;
	}

	if ( (otherEnt->health <= 0 || otherEnt->client->tempSpectate >= level.time || otherEnt->client->sess.sessionTeam == TEAM_SPECTATOR) )
	{
		// Intentionally displaying for the command user
		trap->SendServerCommand( ent-g_entities, va( "print \"%s\n\"", G_GetStringEdString( "MP_SVGAME", "MUSTBEALIVE" ) ) );
		return;
	}

	G_Kill( otherEnt );
}

gentity_t *G_GetDuelWinner(gclient_t *client)
{
	gclient_t *wCl;
	int i;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		wCl = &level.clients[i];

		if (wCl && wCl != client && /*wCl->ps.clientNum != client->ps.clientNum &&*/
			wCl->pers.connected == CON_CONNECTED && wCl->sess.sessionTeam != TEAM_SPECTATOR)
		{
			return &g_entities[wCl->ps.clientNum];
		}
	}

	return NULL;
}

/*
=================
BroadCastTeamChange

Let everyone know about a team change
=================
*/
void BroadcastTeamChange( gclient_t *client, int oldTeam )
{
	client->ps.fd.forceDoInit = 1; //every time we change teams make sure our force powers are set right

	if (level.gametype == GT_SIEGE)
	{ //don't announce these things in siege
		return;
	}

	if ( client->sess.sessionTeam == TEAM_RED ) {
		trap->SendServerCommand( -1, va("cp \"%s" S_COLOR_WHITE " %s\n\"",
			client->pers.netname, G_GetStringEdString("MP_SVGAME", "JOINEDTHEREDTEAM")) );
	} else if ( client->sess.sessionTeam == TEAM_BLUE ) {
		trap->SendServerCommand( -1, va("cp \"%s" S_COLOR_WHITE " %s\n\"",
		client->pers.netname, G_GetStringEdString("MP_SVGAME", "JOINEDTHEBLUETEAM")));
	} else if ( client->sess.sessionTeam == TEAM_SPECTATOR && oldTeam != TEAM_SPECTATOR ) {
		trap->SendServerCommand( -1, va("cp \"%s" S_COLOR_WHITE " %s\n\"",
		client->pers.netname, G_GetStringEdString("MP_SVGAME", "JOINEDTHESPECTATORS")));
	} else if ( client->sess.sessionTeam == TEAM_FREE ) {
		if (level.gametype == GT_DUEL || level.gametype == GT_POWERDUEL)
		{
			/*
			gentity_t *currentWinner = G_GetDuelWinner(client);

			if (currentWinner && currentWinner->client)
			{
				trap->SendServerCommand( -1, va("cp \"%s" S_COLOR_WHITE " %s %s\n\"",
				currentWinner->client->pers.netname, G_GetStringEdString("MP_SVGAME", "VERSUS"), client->pers.netname));
			}
			else
			{
				trap->SendServerCommand( -1, va("cp \"%s" S_COLOR_WHITE " %s\n\"",
				client->pers.netname, G_GetStringEdString("MP_SVGAME", "JOINEDTHEBATTLE")));
			}
			*/
			//NOTE: Just doing a vs. once it counts two players up
		}
		else
		{
			trap->SendServerCommand( -1, va("cp \"%s" S_COLOR_WHITE " %s\n\"",
			client->pers.netname, G_GetStringEdString("MP_SVGAME", "JOINEDTHEBATTLE")));
		}
	}

	G_LogPrintf( "ChangeTeam: %i [%s] (%s) \"%s^7\" %s -> %s\n", (int)(client - level.clients), client->sess.IP, client->pers.guid, client->pers.netname, TeamName( oldTeam ), TeamName( client->sess.sessionTeam ) );
}

qboolean G_PowerDuelCheckFail(gentity_t *ent)
{
	int			loners = 0;
	int			doubles = 0;

	if (!ent->client || ent->client->sess.duelTeam == DUELTEAM_FREE)
	{
		return qtrue;
	}

	G_PowerDuelCount(&loners, &doubles, qfalse);

	if (ent->client->sess.duelTeam == DUELTEAM_LONE && loners >= 1)
	{
		return qtrue;
	}

	if (ent->client->sess.duelTeam == DUELTEAM_DOUBLE && doubles >= 2)
	{
		return qtrue;
	}

	return qfalse;
}

/*
=================
SetTeam
=================
*/
qboolean g_dontPenalizeTeam = qfalse;
qboolean g_preventTeamBegin = qfalse;
void SetTeam( gentity_t *ent, char *s ) {
	int					team, oldTeam;
	gclient_t			*client;
	int					clientNum;
	spectatorState_t	specState;
	int					specClient;
	int					teamLeader;

	// fix: this prevents rare creation of invalid players
	if (!ent->inuse)
	{
		return;
	}

	//
	// see what change is requested
	//
	client = ent->client;

	clientNum = client - level.clients;
	specClient = 0;
	specState = SPECTATOR_NOT;
	if ( !Q_stricmp( s, "scoreboard" ) || !Q_stricmp( s, "score" )  ) {
		team = TEAM_SPECTATOR;
		specState = SPECTATOR_FREE; // SPECTATOR_SCOREBOARD disabling this for now since it is totally broken on client side
	} else if ( !Q_stricmp( s, "follow1" ) ) {
		team = TEAM_SPECTATOR;
		specState = SPECTATOR_FOLLOW;
		specClient = -1;
	} else if ( !Q_stricmp( s, "follow2" ) ) {
		team = TEAM_SPECTATOR;
		specState = SPECTATOR_FOLLOW;
		specClient = -2;
	} else if ( !Q_stricmp( s, "spectator" ) || !Q_stricmp( s, "s" ) ) {
		team = TEAM_SPECTATOR;
		specState = SPECTATOR_FREE;
	} else if ( level.gametype >= GT_TEAM ) {
		// if running a team game, assign player to one of the teams
		specState = SPECTATOR_NOT;
		if ( !Q_stricmp( s, "red" ) || !Q_stricmp( s, "r" ) ) {
			team = TEAM_RED;
		} else if ( !Q_stricmp( s, "blue" ) || !Q_stricmp( s, "b" ) ) {
			team = TEAM_BLUE;
		} else {
			// pick the team with the least number of players
			//For now, don't do this. The legalize function will set powers properly now.
			/*
			if (g_forceBasedTeams.integer)
			{
				if (ent->client->ps.fd.forceSide == FORCE_LIGHTSIDE)
				{
					team = TEAM_BLUE;
				}
				else
				{
					team = TEAM_RED;
				}
			}
			else
			{
			*/
				team = PickTeam( clientNum );
			//}
		}

		if ( g_teamForceBalance.integer && !g_jediVmerc.integer ) {
			int		counts[TEAM_NUM_TEAMS];

			//JAC: Invalid clientNum was being used
			counts[TEAM_BLUE] = TeamCount( ent-g_entities, TEAM_BLUE );
			counts[TEAM_RED] = TeamCount( ent-g_entities, TEAM_RED );

			// We allow a spread of two
			if ( team == TEAM_RED && counts[TEAM_RED] - counts[TEAM_BLUE] > 1 ) {
				//For now, don't do this. The legalize function will set powers properly now.
				/*
				if (g_forceBasedTeams.integer && ent->client->ps.fd.forceSide == FORCE_DARKSIDE)
				{
					trap->SendServerCommand( ent->client->ps.clientNum,
						va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "TOOMANYRED_SWITCH")) );
				}
				else
				*/
				{
					//JAC: Invalid clientNum was being used
					trap->SendServerCommand( ent-g_entities,
						va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "TOOMANYRED")) );
				}
				return; // ignore the request
			}
			if ( team == TEAM_BLUE && counts[TEAM_BLUE] - counts[TEAM_RED] > 1 ) {
				//For now, don't do this. The legalize function will set powers properly now.
				/*
				if (g_forceBasedTeams.integer && ent->client->ps.fd.forceSide == FORCE_LIGHTSIDE)
				{
					trap->SendServerCommand( ent->client->ps.clientNum,
						va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "TOOMANYBLUE_SWITCH")) );
				}
				else
				*/
				{
					//JAC: Invalid clientNum was being used
					trap->SendServerCommand( ent-g_entities,
						va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "TOOMANYBLUE")) );
				}
				return; // ignore the request
			}

			// It's ok, the team we are switching to has less or same number of players
		}

		//For now, don't do this. The legalize function will set powers properly now.
		/*
		if (g_forceBasedTeams.integer)
		{
			if (team == TEAM_BLUE && ent->client->ps.fd.forceSide != FORCE_LIGHTSIDE)
			{
				trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "MUSTBELIGHT")) );
				return;
			}
			if (team == TEAM_RED && ent->client->ps.fd.forceSide != FORCE_DARKSIDE)
			{
				trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "MUSTBEDARK")) );
				return;
			}
		}
		*/

	} else {
		// force them to spectators if there aren't any spots free
		team = TEAM_FREE;
	}

	oldTeam = client->sess.sessionTeam;

	if (level.gametype == GT_SIEGE)
	{
		if (client->tempSpectate >= level.time &&
			team == TEAM_SPECTATOR)
		{ //sorry, can't do that.
			return;
		}

		if ( team == oldTeam && team != TEAM_SPECTATOR )
			return;

		client->sess.siegeDesiredTeam = team;
		//oh well, just let them go.
		/*
		if (team != TEAM_SPECTATOR)
		{ //can't switch to anything in siege unless you want to switch to being a fulltime spectator
			//fill them in on their objectives for this team now
			trap->SendServerCommand(ent-g_entities, va("sb %i", client->sess.siegeDesiredTeam));

			trap->SendServerCommand( ent-g_entities, va("print \"You will be on the selected team the next time the round begins.\n\"") );
			return;
		}
		*/
		if (client->sess.sessionTeam != TEAM_SPECTATOR &&
			team != TEAM_SPECTATOR)
		{ //not a spectator now, and not switching to spec, so you have to wait til you die.
			//trap->SendServerCommand( ent-g_entities, va("print \"You will be on the selected team the next time you respawn.\n\"") );
			qboolean doBegin;
			if (ent->client->tempSpectate >= level.time)
			{
				doBegin = qfalse;
			}
			else
			{
				doBegin = qtrue;
			}

			if (doBegin)
			{
				// Kill them so they automatically respawn in the team they wanted.
				if (ent->health > 0)
				{
					ent->flags &= ~FL_GODMODE;
					ent->client->ps.stats[STAT_HEALTH] = ent->health = 0;
					player_die( ent, ent, ent, 100000, MOD_TEAM_CHANGE );
				}
			}

			if (ent->client->sess.sessionTeam != ent->client->sess.siegeDesiredTeam)
			{
				SetTeamQuick(ent, ent->client->sess.siegeDesiredTeam, qfalse);
			}

			return;
		}
	}

	// override decision if limiting the players
	if ( (level.gametype == GT_DUEL)
		&& level.numNonSpectatorClients >= 2 )
	{
		team = TEAM_SPECTATOR;
	}
	else if ( (level.gametype == GT_POWERDUEL)
		&& (level.numPlayingClients >= 3 || G_PowerDuelCheckFail(ent)) )
	{
		team = TEAM_SPECTATOR;
	}
	else if ( g_maxGameClients.integer > 0 &&
		level.numNonSpectatorClients >= g_maxGameClients.integer )
	{
		team = TEAM_SPECTATOR;
	}

	//
	// decide if we will allow the change
	//
	if ( team == oldTeam && team != TEAM_SPECTATOR ) {
		return;
	}

	//
	// execute the team change
	//

	//If it's siege then show the mission briefing for the team you just joined.
//	if (level.gametype == GT_SIEGE && team != TEAM_SPECTATOR)
//	{
//		trap->SendServerCommand(clientNum, va("sb %i", team));
//	}

	// if the player was dead leave the body
	if ( client->ps.stats[STAT_HEALTH] <= 0 && client->sess.sessionTeam != TEAM_SPECTATOR ) {
		MaintainBodyQueue(ent);
	}

	// he starts at 'base'
	client->pers.teamState.state = TEAM_BEGIN;
	if ( oldTeam != TEAM_SPECTATOR ) {
		// Kill him (makes sure he loses flags, etc)
		ent->flags &= ~FL_GODMODE;
		ent->client->ps.stats[STAT_HEALTH] = ent->health = 0;
		g_dontPenalizeTeam = qtrue;
		player_die (ent, ent, ent, 100000, MOD_SUICIDE);
		g_dontPenalizeTeam = qfalse;

	}
	// they go to the end of the line for tournaments
	if ( team == TEAM_SPECTATOR && oldTeam != team )
		AddTournamentQueue( client );

	// clear votes if going to spectator (specs can't vote)
	if ( team == TEAM_SPECTATOR )
		G_ClearVote( ent );
	// also clear team votes if switching red/blue or going to spec
	G_ClearTeamVote( ent, oldTeam );

	client->sess.sessionTeam = (team_t)team;
	client->sess.spectatorState = specState;
	client->sess.spectatorClient = specClient;

	client->sess.teamLeader = qfalse;
	if ( team == TEAM_RED || team == TEAM_BLUE ) {
		teamLeader = TeamLeader( team );
		// if there is no team leader or the team leader is a bot and this client is not a bot
		if ( teamLeader == -1 || ( !(g_entities[clientNum].r.svFlags & SVF_BOT) && (g_entities[teamLeader].r.svFlags & SVF_BOT) ) ) {
			//SetLeader( team, clientNum );
		}
	}
	// make sure there is a team leader on the team the player came from
	if ( oldTeam == TEAM_RED || oldTeam == TEAM_BLUE ) {
		CheckTeamLeader( oldTeam );
	}

	BroadcastTeamChange( client, oldTeam );

	//make a disappearing effect where they were before teleporting them to the appropriate spawn point,
	//if we were not on the spec team
	if (oldTeam != TEAM_SPECTATOR)
	{
		gentity_t *tent = G_TempEntity( client->ps.origin, EV_PLAYER_TELEPORT_OUT );
		tent->s.clientNum = clientNum;
	}

	// get and distribute relevent paramters
	if ( !ClientUserinfoChanged( clientNum ) )
		return;

	if (!g_preventTeamBegin)
	{
		ClientBegin( clientNum, qfalse );
	}
}

/*
=================
StopFollowing

If the client being followed leaves the game, or you just want to drop
to free floating spectator mode
=================
*/
extern void G_LeaveVehicle( gentity_t *ent, qboolean ConCheck );
void StopFollowing( gentity_t *ent ) {
	int i=0;
	ent->client->ps.persistant[ PERS_TEAM ] = TEAM_SPECTATOR;
	ent->client->sess.sessionTeam = TEAM_SPECTATOR;
	ent->client->sess.spectatorState = SPECTATOR_FREE;
	ent->client->ps.pm_flags &= ~PMF_FOLLOW;
	ent->r.svFlags &= ~SVF_BOT;
	ent->client->ps.clientNum = ent - g_entities;
	ent->client->ps.weapon = WP_NONE;
	G_LeaveVehicle( ent, qfalse ); // clears m_iVehicleNum as well
	ent->client->ps.emplacedIndex = 0;
	//ent->client->ps.m_iVehicleNum = 0;
	ent->client->ps.viewangles[ROLL] = 0.0f;
	ent->client->ps.forceHandExtend = HANDEXTEND_NONE;
	ent->client->ps.forceHandExtendTime = 0;
	ent->client->ps.zoomMode = 0;
	ent->client->ps.zoomLocked = 0;
	ent->client->ps.zoomLockTime = 0;
	ent->client->ps.saberMove = LS_NONE;
	ent->client->ps.legsAnim = 0;
	ent->client->ps.legsTimer = 0;
	ent->client->ps.torsoAnim = 0;
	ent->client->ps.torsoTimer = 0;
	ent->client->ps.isJediMaster = qfalse; // major exploit if you are spectating somebody and they are JM and you reconnect
	ent->client->ps.cloakFuel = 100; // so that fuel goes away after stop following them
	ent->client->ps.jetpackFuel = 100; // so that fuel goes away after stop following them
	ent->health = ent->client->ps.stats[STAT_HEALTH] = 100; // so that you don't keep dead angles if you were spectating a dead person
	ent->client->ps.bobCycle = 0;
	ent->client->ps.pm_type = PM_SPECTATOR;
	ent->client->ps.eFlags &= ~EF_DISINTEGRATION;
	for ( i=0; i<PW_NUM_POWERUPS; i++ )
		ent->client->ps.powerups[i] = 0;
}

/*
=================
Cmd_Team_f
=================
*/
void Cmd_Team_f( gentity_t *ent ) {
	int			oldTeam;
	char		s[MAX_TOKEN_CHARS];

	oldTeam = ent->client->sess.sessionTeam;

	if ( trap->Argc() != 2 ) {
		switch ( oldTeam ) {
		case TEAM_BLUE:
			trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "PRINTBLUETEAM")) );
			break;
		case TEAM_RED:
			trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "PRINTREDTEAM")) );
			break;
		case TEAM_FREE:
			trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "PRINTFREETEAM")) );
			break;
		case TEAM_SPECTATOR:
			trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "PRINTSPECTEAM")) );
			break;
		}
		return;
	}

	if ( ent->client->switchTeamTime > level.time ) {
		trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "NOSWITCH")) );
		return;
	}

	if (gEscaping)
	{
		return;
	}

	// if they are playing a tournament game, count as a loss
	if ( level.gametype == GT_DUEL
		&& ent->client->sess.sessionTeam == TEAM_FREE ) {//in a tournament game
		//disallow changing teams
		trap->SendServerCommand( ent-g_entities, "print \"Cannot switch teams in Duel\n\"" );
		return;
		//FIXME: why should this be a loss???
		//ent->client->sess.losses++;
	}

	if (level.gametype == GT_POWERDUEL)
	{ //don't let clients change teams manually at all in powerduel, it will be taken care of through automated stuff
		trap->SendServerCommand( ent-g_entities, "print \"Cannot switch teams in Power Duel\n\"" );
		return;
	}

	trap->Argv( 1, s, sizeof( s ) );

	SetTeam( ent, s );

	// fix: update team switch time only if team change really happend
	if (oldTeam != ent->client->sess.sessionTeam)
		ent->client->switchTeamTime = level.time + 5000;
}

/*
=================
Cmd_DuelTeam_f
=================
*/
void Cmd_DuelTeam_f(gentity_t *ent)
{
	int			oldTeam;
	char		s[MAX_TOKEN_CHARS];

	if (level.gametype != GT_POWERDUEL)
	{ //don't bother doing anything if this is not power duel
		return;
	}

	/*
	if (ent->client->sess.sessionTeam != TEAM_SPECTATOR)
	{
		trap->SendServerCommand( ent-g_entities, va("print \"You cannot change your duel team unless you are a spectator.\n\""));
		return;
	}
	*/

	if ( trap->Argc() != 2 )
	{ //No arg so tell what team we're currently on.
		oldTeam = ent->client->sess.duelTeam;
		switch ( oldTeam )
		{
		case DUELTEAM_FREE:
			trap->SendServerCommand( ent-g_entities, va("print \"None\n\"") );
			break;
		case DUELTEAM_LONE:
			trap->SendServerCommand( ent-g_entities, va("print \"Single\n\"") );
			break;
		case DUELTEAM_DOUBLE:
			trap->SendServerCommand( ent-g_entities, va("print \"Double\n\"") );
			break;
		default:
			break;
		}
		return;
	}

	if ( ent->client->switchDuelTeamTime > level.time )
	{ //debounce for changing
		trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "NOSWITCH")) );
		return;
	}

	trap->Argv( 1, s, sizeof( s ) );

	oldTeam = ent->client->sess.duelTeam;

	if (!Q_stricmp(s, "free"))
	{
		ent->client->sess.duelTeam = DUELTEAM_FREE;
	}
	else if (!Q_stricmp(s, "single"))
	{
		ent->client->sess.duelTeam = DUELTEAM_LONE;
	}
	else if (!Q_stricmp(s, "double"))
	{
		ent->client->sess.duelTeam = DUELTEAM_DOUBLE;
	}
	else
	{
		trap->SendServerCommand( ent-g_entities, va("print \"'%s' not a valid duel team.\n\"", s) );
	}

	if (oldTeam == ent->client->sess.duelTeam)
	{ //didn't actually change, so don't care.
		return;
	}

	if (ent->client->sess.sessionTeam != TEAM_SPECTATOR)
	{ //ok..die
		int curTeam = ent->client->sess.duelTeam;
		ent->client->sess.duelTeam = oldTeam;
		G_Damage(ent, ent, ent, NULL, ent->client->ps.origin, 99999, DAMAGE_NO_PROTECTION, MOD_SUICIDE);
		ent->client->sess.duelTeam = curTeam;
	}
	//reset wins and losses
	ent->client->sess.wins = 0;
	ent->client->sess.losses = 0;

	//get and distribute relevent paramters
	if ( ClientUserinfoChanged( ent->s.number ) )
		return;

	ent->client->switchDuelTeamTime = level.time + 5000;
}

int G_TeamForSiegeClass(const char *clName)
{
	int i = 0;
	int team = SIEGETEAM_TEAM1;
	siegeTeam_t *stm = BG_SiegeFindThemeForTeam(team);
	siegeClass_t *scl;

	if (!stm)
	{
		return 0;
	}

	while (team <= SIEGETEAM_TEAM2)
	{
		scl = stm->classes[i];

		if (scl && scl->name[0])
		{
			if (!Q_stricmp(clName, scl->name))
			{
				return team;
			}
		}

		i++;
		if (i >= MAX_SIEGE_CLASSES || i >= stm->numClasses)
		{
			if (team == SIEGETEAM_TEAM2)
			{
				break;
			}
			team = SIEGETEAM_TEAM2;
			stm = BG_SiegeFindThemeForTeam(team);
			i = 0;
		}
	}

	return 0;
}

/*
=================
Cmd_SiegeClass_f
=================
*/
void Cmd_SiegeClass_f( gentity_t *ent )
{
	char className[64];
	int team = 0;
	int preScore;
	qboolean startedAsSpec = qfalse;

	if (level.gametype != GT_SIEGE)
	{ //classes are only valid for this gametype
		return;
	}

	if (!ent->client)
	{
		return;
	}

	if (trap->Argc() < 1)
	{
		return;
	}

	if ( ent->client->switchClassTime > level.time )
	{
		trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "NOCLASSSWITCH")) );
		return;
	}

	if (ent->client->sess.sessionTeam == TEAM_SPECTATOR)
	{
		startedAsSpec = qtrue;
	}

	trap->Argv( 1, className, sizeof( className ) );

	team = G_TeamForSiegeClass(className);

	if (!team)
	{ //not a valid class name
		return;
	}

	if (ent->client->sess.sessionTeam != team)
	{ //try changing it then
		g_preventTeamBegin = qtrue;
		if (team == TEAM_RED)
		{
			SetTeam(ent, "red");
		}
		else if (team == TEAM_BLUE)
		{
			SetTeam(ent, "blue");
		}
		g_preventTeamBegin = qfalse;

		if (ent->client->sess.sessionTeam != team)
		{ //failed, oh well
			if (ent->client->sess.sessionTeam != TEAM_SPECTATOR ||
				ent->client->sess.siegeDesiredTeam != team)
			{
				trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "NOCLASSTEAM")) );
				return;
			}
		}
	}

	//preserve 'is score
	preScore = ent->client->ps.persistant[PERS_SCORE];

	//Make sure the class is valid for the team
	BG_SiegeCheckClassLegality(team, className);

	//Set the session data
	strcpy(ent->client->sess.siegeClass, className);

	// get and distribute relevent paramters
	if ( !ClientUserinfoChanged( ent->s.number ) )
		return;

	if (ent->client->tempSpectate < level.time)
	{
		// Kill him (makes sure he loses flags, etc)
		if (ent->health > 0 && !startedAsSpec)
		{
			ent->flags &= ~FL_GODMODE;
			ent->client->ps.stats[STAT_HEALTH] = ent->health = 0;
			player_die (ent, ent, ent, 100000, MOD_SUICIDE);
		}

		if (ent->client->sess.sessionTeam == TEAM_SPECTATOR || startedAsSpec)
		{ //respawn them instantly.
			ClientBegin( ent->s.number, qfalse );
		}
	}
	//set it back after we do all the stuff
	ent->client->ps.persistant[PERS_SCORE] = preScore;

	ent->client->switchClassTime = level.time + 5000;
}

/*
=================
Cmd_ForceChanged_f
=================
*/
void Cmd_ForceChanged_f( gentity_t *ent )
{
	char fpChStr[1024];
	const char *buf;
//	Cmd_Kill_f(ent);
	if (ent->client->sess.sessionTeam == TEAM_SPECTATOR)
	{ //if it's a spec, just make the changes now
		//trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "FORCEAPPLIED")) );
		//No longer print it, as the UI calls this a lot.
		WP_InitForcePowers( ent );
		goto argCheck;
	}

	buf = G_GetStringEdString("MP_SVGAME", "FORCEPOWERCHANGED");

	strcpy(fpChStr, buf);

	trap->SendServerCommand( ent-g_entities, va("print \"%s%s\n\"", S_COLOR_GREEN, fpChStr) );

	ent->client->ps.fd.forceDoInit = 1;
argCheck:
	if (level.gametype == GT_DUEL || level.gametype == GT_POWERDUEL)
	{ //If this is duel, don't even bother changing team in relation to this.
		return;
	}

	if (trap->Argc() > 1)
	{
		char	arg[MAX_TOKEN_CHARS];

		trap->Argv( 1, arg, sizeof( arg ) );

		if ( arg[0] )
		{ //if there's an arg, assume it's a combo team command from the UI.
			Cmd_Team_f(ent);
		}
	}
}

extern qboolean WP_SaberStyleValidForSaber( saberInfo_t *saber1, saberInfo_t *saber2, int saberHolstered, int saberAnimLevel );
extern qboolean WP_UseFirstValidSaberStyle( saberInfo_t *saber1, saberInfo_t *saber2, int saberHolstered, int *saberAnimLevel );
qboolean G_SetSaber(gentity_t *ent, int saberNum, char *saberName, qboolean siegeOverride)
{
	char truncSaberName[MAX_QPATH] = {0};

	if ( !siegeOverride && level.gametype == GT_SIEGE && ent->client->siegeClass != -1 &&
		(bgSiegeClasses[ent->client->siegeClass].saberStance || bgSiegeClasses[ent->client->siegeClass].saber1[0] || bgSiegeClasses[ent->client->siegeClass].saber2[0]) )
	{ //don't let it be changed if the siege class has forced any saber-related things
		return qfalse;
	}

	Q_strncpyz( truncSaberName, saberName, sizeof( truncSaberName ) );

	if ( saberNum == 0 && (!Q_stricmp( "none", truncSaberName ) || !Q_stricmp( "remove", truncSaberName )) )
	{ //can't remove saber 0 like this
		Q_strncpyz( truncSaberName, DEFAULT_SABER, sizeof( truncSaberName ) );
	}

	//Set the saber with the arg given. If the arg is
	//not a valid sabername defaults will be used.
	WP_SetSaber( ent->s.number, ent->client->saber, saberNum, truncSaberName );

	if ( !ent->client->saber[0].model[0] )
	{
		assert(0); //should never happen!
		Q_strncpyz( ent->client->pers.saber1, DEFAULT_SABER, sizeof( ent->client->pers.saber1 ) );
	}
	else
		Q_strncpyz( ent->client->pers.saber1, ent->client->saber[0].name, sizeof( ent->client->pers.saber1 ) );

	if ( !ent->client->saber[1].model[0] )
		Q_strncpyz( ent->client->pers.saber2, "none", sizeof( ent->client->pers.saber2 ) );
	else
		Q_strncpyz( ent->client->pers.saber2, ent->client->saber[1].name, sizeof( ent->client->pers.saber2 ) );

	if ( !WP_SaberStyleValidForSaber( &ent->client->saber[0], &ent->client->saber[1], ent->client->ps.saberHolstered, ent->client->ps.fd.saberAnimLevel ) )
	{
		WP_UseFirstValidSaberStyle( &ent->client->saber[0], &ent->client->saber[1], ent->client->ps.saberHolstered, &ent->client->ps.fd.saberAnimLevel );
		ent->client->ps.fd.saberAnimLevelBase = ent->client->saberCycleQueue = ent->client->ps.fd.saberAnimLevel;
	}

	return qtrue;
}

/*
=================
Cmd_Follow_f
=================
*/
void Cmd_Follow_f( gentity_t *ent ) {
	int		i;
	char	arg[MAX_TOKEN_CHARS];

	if ( ent->client->sess.spectatorState == SPECTATOR_NOT && ent->client->switchTeamTime > level.time ) {
		trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "NOSWITCH")) );
		return;
	}

	if ( trap->Argc() != 2 ) {
		if ( ent->client->sess.spectatorState == SPECTATOR_FOLLOW ) {
			StopFollowing( ent );
		}
		return;
	}

	trap->Argv( 1, arg, sizeof( arg ) );
	i = ClientNumberFromString( ent, arg, qfalse );
	if ( i == -1 ) {
		return;
	}

	// can't follow self
	if ( &level.clients[ i ] == ent->client ) {
		return;
	}

	// can't follow another spectator
	if ( level.clients[ i ].sess.sessionTeam == TEAM_SPECTATOR ) {
		return;
	}

	if ( level.clients[ i ].tempSpectate >= level.time ) {
		return;
	}

	// if they are playing a tournament game, count as a loss
	if ( (level.gametype == GT_DUEL || level.gametype == GT_POWERDUEL)
		&& ent->client->sess.sessionTeam == TEAM_FREE ) {
		//WTF???
		ent->client->sess.losses++;
	}

	// first set them to spectator
	if ( ent->client->sess.sessionTeam != TEAM_SPECTATOR ) {
		SetTeam( ent, "spectator" );
		// fix: update team switch time only if team change really happend
		if (ent->client->sess.sessionTeam == TEAM_SPECTATOR)
			ent->client->switchTeamTime = level.time + 5000;
	}

	ent->client->sess.spectatorState = SPECTATOR_FOLLOW;
	ent->client->sess.spectatorClient = i;
}

/*
=================
Cmd_FollowCycle_f
=================
*/
void Cmd_FollowCycle_f( gentity_t *ent, int dir ) {
	int		clientnum;
	int		original;
	qboolean	looped = qfalse;

	if ( ent->client->sess.spectatorState == SPECTATOR_NOT && ent->client->switchTeamTime > level.time ) {
		trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "NOSWITCH")) );
		return;
	}

	// if they are playing a tournament game, count as a loss
	if ( (level.gametype == GT_DUEL || level.gametype == GT_POWERDUEL)
		&& ent->client->sess.sessionTeam == TEAM_FREE ) {\
		//WTF???
		ent->client->sess.losses++;
	}
	// first set them to spectator
	if ( ent->client->sess.spectatorState == SPECTATOR_NOT ) {
		SetTeam( ent, "spectator" );
		// fix: update team switch time only if team change really happend
		if (ent->client->sess.sessionTeam == TEAM_SPECTATOR)
			ent->client->switchTeamTime = level.time + 5000;
	}

	if ( dir != 1 && dir != -1 ) {
		trap->Error( ERR_DROP, "Cmd_FollowCycle_f: bad dir %i", dir );
	}

	clientnum = ent->client->sess.spectatorClient;
	original = clientnum;

	do {
		clientnum += dir;
		if ( clientnum >= level.maxclients )
		{
			//JAC: Avoid /team follow1 crash
			if ( looped )
			{
				clientnum = original;
				break;
			}
			else
			{
				clientnum = 0;
				looped = qtrue;
			}
		}
		if ( clientnum < 0 ) {
			if ( looped )
			{
				clientnum = original;
				break;
			}
			else
			{
				clientnum = level.maxclients - 1;
				looped = qtrue;
			}
		}

		// can only follow connected clients
		if ( level.clients[ clientnum ].pers.connected != CON_CONNECTED ) {
			continue;
		}

		// can't follow another spectator
		if ( level.clients[ clientnum ].sess.sessionTeam == TEAM_SPECTATOR ) {
			continue;
		}

		// can't follow another spectator
		if ( level.clients[ clientnum ].tempSpectate >= level.time ) {
			return;
		}

		// this is good, we can use it
		ent->client->sess.spectatorClient = clientnum;
		ent->client->sess.spectatorState = SPECTATOR_FOLLOW;
		return;
	} while ( clientnum != original );

	// leave it where it was
}

void Cmd_FollowNext_f( gentity_t *ent ) {
	Cmd_FollowCycle_f( ent, 1 );
}

void Cmd_FollowPrev_f( gentity_t *ent ) {
	Cmd_FollowCycle_f( ent, -1 );
}

/*
==================
G_Say
==================
*/

static void G_SayTo( gentity_t *ent, gentity_t *other, int mode, int color, const char *name, const char *message, char *locMsg )
{
	if (!other) {
		return;
	}
	if (!other->inuse) {
		return;
	}
	if (!other->client) {
		return;
	}
	if ( other->client->pers.connected != CON_CONNECTED ) {
		return;
	}
	if ( mode == SAY_TEAM  && !OnSameTeam(ent, other) ) {
		return;
	}
	/*
	// no chatting to players in tournaments
	if ( (level.gametype == GT_DUEL || level.gametype == GT_POWERDUEL)
		&& other->client->sess.sessionTeam == TEAM_FREE
		&& ent->client->sess.sessionTeam != TEAM_FREE ) {
		//Hmm, maybe some option to do so if allowed?  Or at least in developer mode...
		return;
	}
	*/
	//They've requested I take this out.

	if (level.gametype == GT_SIEGE &&
		ent->client && (ent->client->tempSpectate >= level.time || ent->client->sess.sessionTeam == TEAM_SPECTATOR) &&
		other->client->sess.sessionTeam != TEAM_SPECTATOR &&
		other->client->tempSpectate < level.time)
	{ //siege temp spectators should not communicate to ingame players
		return;
	}

	if (locMsg)
	{
		trap->SendServerCommand( other-g_entities, va("%s \"%s\" \"%s\" \"%c\" \"%s\" %i",
			mode == SAY_TEAM ? "ltchat" : "lchat",
			name, locMsg, color, message, ent->s.number));
	}
	else
	{
		trap->SendServerCommand( other-g_entities, va("%s \"%s%c%c%s\" %i",
			mode == SAY_TEAM ? "tchat" : "chat",
			name, Q_COLOR_ESCAPE, color, message, ent->s.number));
	}
}

void G_Say( gentity_t *ent, gentity_t *target, int mode, const char *chatText ) {
	int			j;
	gentity_t	*other;
	int			color;
	char		name[64];
	// don't let text be too long for malicious reasons
	char		text[MAX_SAY_TEXT];
	char		location[64];
	char		*locMsg = NULL;

	if ( level.gametype < GT_TEAM && mode == SAY_TEAM ) {
		mode = SAY_ALL;
	}

	Q_strncpyz( text, chatText, sizeof(text) );

	Q_strstrip( text, "\n\r", "  " );

	switch ( mode ) {
	default:
	case SAY_ALL:
		G_LogPrintf( "say: %s: %s\n", ent->client->pers.netname, text );
		Com_sprintf (name, sizeof(name), "%s%c%c"EC": ", ent->client->pers.netname, Q_COLOR_ESCAPE, COLOR_WHITE );
		color = COLOR_GREEN;
		break;
	case SAY_TEAM:
		G_LogPrintf( "sayteam: %s: %s\n", ent->client->pers.netname, text );
		if (Team_GetLocationMsg(ent, location, sizeof(location)))
		{
			Com_sprintf (name, sizeof(name), EC"(%s%c%c"EC")"EC": ",
				ent->client->pers.netname, Q_COLOR_ESCAPE, COLOR_WHITE );
			locMsg = location;
		}
		else
		{
			Com_sprintf (name, sizeof(name), EC"(%s%c%c"EC")"EC": ",
				ent->client->pers.netname, Q_COLOR_ESCAPE, COLOR_WHITE );
		}
		color = COLOR_CYAN;
		break;
	case SAY_TELL:
		if (target && target->inuse && target->client && level.gametype >= GT_TEAM &&
			target->client->sess.sessionTeam == ent->client->sess.sessionTeam &&
			Team_GetLocationMsg(ent, location, sizeof(location)))
		{
			Com_sprintf (name, sizeof(name), EC"[%s%c%c"EC"]"EC": ", ent->client->pers.netname, Q_COLOR_ESCAPE, COLOR_WHITE );
			locMsg = location;
		}
		else
		{
			Com_sprintf (name, sizeof(name), EC"[%s%c%c"EC"]"EC": ", ent->client->pers.netname, Q_COLOR_ESCAPE, COLOR_WHITE );
		}
		color = COLOR_MAGENTA;
		break;
	}

	if ( target ) {
		G_SayTo( ent, target, mode, color, name, text, locMsg );
		return;
	}

	// echo the text to the console
	if ( dedicated.integer ) {
		trap->Print( "%s%s\n", name, text);
	}

	// send it to all the appropriate clients
	for (j = 0; j < level.maxclients; j++) {
		other = &g_entities[j];
		G_SayTo( ent, other, mode, color, name, text, locMsg );
	}
}


/*
==================
Cmd_Say_f
==================
*/
static void Cmd_Say_f( gentity_t *ent ) {
	char *p = NULL;

	if ( trap->Argc () < 2 )
		return;

	p = ConcatArgs( 1 );

	if ( strlen( p ) >= MAX_SAY_TEXT ) {
		p[MAX_SAY_TEXT-1] = '\0';
		G_SecurityLogPrintf( "Cmd_Say_f from %d (%s) has been truncated: %s\n", ent->s.number, ent->client->pers.netname, p );
	}

	G_Say( ent, NULL, SAY_ALL, p );
}

/*
==================
Cmd_SayTeam_f
==================
*/
static void Cmd_SayTeam_f( gentity_t *ent ) {
	char *p = NULL;

	if ( trap->Argc () < 2 )
		return;

	p = ConcatArgs( 1 );

	if ( strlen( p ) >= MAX_SAY_TEXT ) {
		p[MAX_SAY_TEXT-1] = '\0';
		G_SecurityLogPrintf( "Cmd_SayTeam_f from %d (%s) has been truncated: %s\n", ent->s.number, ent->client->pers.netname, p );
	}

	G_Say( ent, NULL, (level.gametype>=GT_TEAM) ? SAY_TEAM : SAY_ALL, p );
}

// zyk: rmeoves color codes from string like ^1, ^2, ^3 etc
char *zyk_remove_color_chars(char *arg)
{
	char new_string[1024] = {0};
	int i = 0, len = 0;

	len = strlen(arg);

	while( i < len)
	{
		if (i < (len-1) && arg[i] == '^' && 
			(arg[i+1] == '0' || arg[i+1] == '1' || arg[i+1] == '2' || arg[i+1] == '3' || 
			arg[i+1] == '4' || arg[i+1] == '5' || arg[i+1] == '6' || arg[i+1] == '7'))
		{
			i += 2;
		}
		else
		{
			strcpy(new_string,va("%s%c",new_string,arg[i]));
			i++;
		}
	}

	return G_NewString(new_string);
}

// zyk: gets the client id from a string which contains the player name
int zyk_get_client(char *arg)
{
	int i = 0;
	int arg_num = atoi(arg);
	gentity_t *this_ent = NULL;

	if (Q_stricmp(arg,"0") == 0 || (arg_num > 0 && arg_num < level.maxclients))
	{ // zyk: argument is the client id. Search for the player who has this id
		for (i = 0; i < level.maxclients; i++)
		{
			this_ent = &g_entities[i];
			if (this_ent && this_ent->client && this_ent->client->pers.connected == CON_CONNECTED && arg_num == this_ent->s.number)
			{
				return this_ent->s.number;
			}
		}
	}
	else
	{ // zyk: argument is part or full player netname
		for (i = 0; i < level.maxclients; i++)
		{
			this_ent = &g_entities[i];
			if (this_ent && this_ent->client && this_ent->client->pers.connected == CON_CONNECTED && Q_stristr(zyk_remove_color_chars(this_ent->client->pers.netname),zyk_remove_color_chars(arg)))
			{
				return this_ent->s.number;
			}
		}
	}
	return -1;
}

/*
==================
Cmd_Tell_f
==================
*/
static void Cmd_Tell_f( gentity_t *ent ) {
	int			targetNum;
	gentity_t	*target;
	char		*p;
	char		arg[MAX_TOKEN_CHARS];

	if ( trap->Argc () < 3 ) {
		trap->SendServerCommand( ent-g_entities, "print \"Usage: tell <player id or name> <message>\n\"" );
		return;
	}

	trap->Argv( 1, arg, sizeof( arg ) );
	targetNum = zyk_get_client( arg ); // zyk: changed this. Now it will use new function
	if ( targetNum == -1 ) {
		return;
	}

	target = &g_entities[targetNum];

	p = ConcatArgs( 2 );

	if ( strlen( p ) >= MAX_SAY_TEXT ) {
		p[MAX_SAY_TEXT-1] = '\0';
		G_SecurityLogPrintf( "Cmd_Tell_f from %d (%s) has been truncated: %s\n", ent->s.number, ent->client->pers.netname, p );
	}

	G_LogPrintf( "tell: %s to %s: %s\n", ent->client->pers.netname, target->client->pers.netname, p );
	G_Say( ent, target, SAY_TELL, p );
	// don't tell to the player self if it was already directed to this player
	// also don't send the chat back to a bot
	if ( ent != target && !(ent->r.svFlags & SVF_BOT)) {
		G_Say( ent, ent, SAY_TELL, p );
	}
}

//siege voice command
static void Cmd_VoiceCommand_f(gentity_t *ent)
{
	gentity_t *te;
	char arg[MAX_TOKEN_CHARS];
	char *s;
	int i = 0;

	if (level.gametype < GT_TEAM)
	{
		return;
	}

	if (trap->Argc() < 2)
	{
		return;
	}

	if (ent->client->sess.sessionTeam == TEAM_SPECTATOR ||
		ent->client->tempSpectate >= level.time)
	{
		trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "NOVOICECHATASSPEC")) );
		return;
	}

	trap->Argv(1, arg, sizeof(arg));

	if (arg[0] == '*')
	{ //hmm.. don't expect a * to be prepended already. maybe someone is trying to be sneaky.
		return;
	}

	s = va("*%s", arg);

	//now, make sure it's a valid sound to be playing like this.. so people can't go around
	//screaming out death sounds or whatever.
	while (i < MAX_CUSTOM_SIEGE_SOUNDS)
	{
		if (!bg_customSiegeSoundNames[i])
		{
			break;
		}
		if (!Q_stricmp(bg_customSiegeSoundNames[i], s))
		{ //it matches this one, so it's ok
			break;
		}
		i++;
	}

	if (i == MAX_CUSTOM_SIEGE_SOUNDS || !bg_customSiegeSoundNames[i])
	{ //didn't find it in the list
		return;
	}

	te = G_TempEntity(vec3_origin, EV_VOICECMD_SOUND);
	te->s.groundEntityNum = ent->s.number;
	te->s.eventParm = G_SoundIndex((char *)bg_customSiegeSoundNames[i]);
	te->r.svFlags |= SVF_BROADCAST;
}


static char	*gc_orders[] = {
	"hold your position",
	"hold this position",
	"come here",
	"cover me",
	"guard location",
	"search and destroy",
	"report"
};
static size_t numgc_orders = ARRAY_LEN( gc_orders );

void Cmd_GameCommand_f( gentity_t *ent ) {
	int				targetNum;
	unsigned int	order;
	gentity_t		*target;
	char			arg[MAX_TOKEN_CHARS] = {0};

	if ( trap->Argc() != 3 ) {
		trap->SendServerCommand( ent-g_entities, va( "print \"Usage: gc <player id> <order 0-%d>\n\"", numgc_orders - 1 ) );
		return;
	}

	trap->Argv( 2, arg, sizeof( arg ) );
	order = atoi( arg );

	if ( order >= numgc_orders ) {
		trap->SendServerCommand( ent-g_entities, va("print \"Bad order: %i\n\"", order));
		return;
	}

	trap->Argv( 1, arg, sizeof( arg ) );
	targetNum = ClientNumberFromString( ent, arg, qfalse );
	if ( targetNum == -1 )
		return;

	target = &g_entities[targetNum];
	if ( !target->inuse || !target->client )
		return;

	G_LogPrintf( "tell: %s to %s: %s\n", ent->client->pers.netname, target->client->pers.netname, gc_orders[order] );
	G_Say( ent, target, SAY_TELL, gc_orders[order] );
	// don't tell to the player self if it was already directed to this player
	// also don't send the chat back to a bot
	if ( ent != target && !(ent->r.svFlags & SVF_BOT) )
		G_Say( ent, ent, SAY_TELL, gc_orders[order] );
}

/*
==================
Cmd_Where_f
==================
*/
void Cmd_Where_f( gentity_t *ent ) {
	//JAC: This wasn't working for non-spectators since s.origin doesn't update for active players.
	if(ent->client && ent->client->sess.sessionTeam != TEAM_SPECTATOR )
	{//active players use currentOrigin
		trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", vtos( ent->r.currentOrigin ) ) );
	}
	else
	{
		trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", vtos( ent->s.origin ) ) );
	}
	//trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", vtos( ent->s.origin ) ) );
}

static const char *gameNames[] = {
	"Free For All",
	"Holocron FFA",
	"Jedi Master",
	"Duel",
	"Power Duel",
	"Single Player",
	"Team FFA",
	"Siege",
	"Capture the Flag",
	"Capture the Ysalamiri"
};

/*
==================
Cmd_CallVote_f
==================
*/
extern void SiegeClearSwitchData(void); //g_saga.c

qboolean G_VoteCapturelimit( gentity_t *ent, int numArgs, const char *arg1, const char *arg2 ) {
	int n = Com_Clampi( 0, 0x7FFFFFFF, atoi( arg2 ) );
	Com_sprintf( level.voteString, sizeof( level.voteString ), "%s %i", arg1, n );
	Com_sprintf( level.voteDisplayString, sizeof( level.voteDisplayString ), "%s", level.voteString );
	Q_strncpyz( level.voteStringClean, level.voteString, sizeof( level.voteStringClean ) );
	return qtrue;
}

qboolean G_VoteClientkick( gentity_t *ent, int numArgs, const char *arg1, const char *arg2 ) {
	int n = atoi ( arg2 );

	if ( n < 0 || n >= level.maxclients ) {
		trap->SendServerCommand( ent-g_entities, va( "print \"invalid client number %d.\n\"", n ) );
		return qfalse;
	}

	if ( g_entities[n].client->pers.connected == CON_DISCONNECTED ) {
		trap->SendServerCommand( ent-g_entities, va( "print \"there is no client with the client number %d.\n\"", n ) );
		return qfalse;
	}

	Com_sprintf( level.voteString, sizeof( level.voteString ), "%s %s", arg1, arg2 );
	Com_sprintf( level.voteDisplayString, sizeof( level.voteDisplayString ), "%s %s", arg1, g_entities[n].client->pers.netname );
	Q_strncpyz( level.voteStringClean, level.voteString, sizeof( level.voteStringClean ) );
	return qtrue;
}

qboolean G_VoteFraglimit( gentity_t *ent, int numArgs, const char *arg1, const char *arg2 ) {
	int n = Com_Clampi( 0, 0x7FFFFFFF, atoi( arg2 ) );
	Com_sprintf( level.voteString, sizeof( level.voteString ), "%s %i", arg1, n );
	Com_sprintf( level.voteDisplayString, sizeof( level.voteDisplayString ), "%s", level.voteString );
	Q_strncpyz( level.voteStringClean, level.voteString, sizeof( level.voteStringClean ) );
	return qtrue;
}

qboolean G_VoteGametype( gentity_t *ent, int numArgs, const char *arg1, const char *arg2 ) {
	int gt = atoi( arg2 );

	// ffa, ctf, tdm, etc
	if ( arg2[0] && isalpha( arg2[0] ) ) {
		gt = BG_GetGametypeForString( arg2 );
		if ( gt == -1 )
		{
			trap->SendServerCommand( ent-g_entities, va( "print \"Gametype (%s) unrecognised, defaulting to FFA/Deathmatch\n\"", arg2 ) );
			gt = GT_FFA;
		}
	}
	// numeric but out of range
	else if ( gt < 0 || gt >= GT_MAX_GAME_TYPE ) {
		trap->SendServerCommand( ent-g_entities, va( "print \"Gametype (%i) is out of range, defaulting to FFA/Deathmatch\n\"", gt ) );
		gt = GT_FFA;
	}

	// logically invalid gametypes, or gametypes not fully implemented in MP
	if ( gt == GT_SINGLE_PLAYER ) {
		trap->SendServerCommand( ent-g_entities, va( "print \"This gametype is not supported (%s).\n\"", arg2 ) );
		return qfalse;
	}

	level.votingGametype = qtrue;
	level.votingGametypeTo = gt;

	Com_sprintf( level.voteString, sizeof( level.voteString ), "%s %d", arg1, gt );
	Com_sprintf( level.voteDisplayString, sizeof( level.voteDisplayString ), "%s %s", arg1, gameNames[gt] );
	Q_strncpyz( level.voteStringClean, level.voteString, sizeof( level.voteStringClean ) );
	return qtrue;
}

qboolean G_VoteKick( gentity_t *ent, int numArgs, const char *arg1, const char *arg2 ) {
	int clientid = ClientNumberFromString( ent, arg2, qtrue );
	gentity_t *target = NULL;

	if ( clientid == -1 )
		return qfalse;

	target = &g_entities[clientid];
	if ( !target || !target->inuse || !target->client )
		return qfalse;

	Com_sprintf( level.voteString, sizeof( level.voteString ), "clientkick %d", clientid );
	Com_sprintf( level.voteDisplayString, sizeof( level.voteDisplayString ), "kick %s", target->client->pers.netname );
	Q_strncpyz( level.voteStringClean, level.voteString, sizeof( level.voteStringClean ) );
	return qtrue;
}

const char *G_GetArenaInfoByMap( const char *map );

void Cmd_MapList_f( gentity_t *ent ) {
	int i, toggle=0;
	char map[24] = "--", buf[512] = {0};

	Q_strcat( buf, sizeof( buf ), "Map list:" );

	for ( i=0; i<level.arenas.num; i++ ) {
		Q_strncpyz( map, Info_ValueForKey( level.arenas.infos[i], "map" ), sizeof( map ) );
		Q_StripColor( map );

		if ( G_DoesMapSupportGametype( map, level.gametype ) ) {
			char *tmpMsg = va( " ^%c%s", (++toggle&1) ? COLOR_GREEN : COLOR_YELLOW, map );
			if ( strlen( buf ) + strlen( tmpMsg ) >= sizeof( buf ) ) {
				trap->SendServerCommand( ent-g_entities, va( "print \"%s\"", buf ) );
				buf[0] = '\0';
			}
			Q_strcat( buf, sizeof( buf ), tmpMsg );
		}
	}

	trap->SendServerCommand( ent-g_entities, va( "print \"%s\n\"", buf ) );
}

qboolean G_VotePoll( gentity_t *ent, int numArgs, const char *arg1, const char *arg2 ) {
	// zyk: did not put message
	if ( numArgs < 3 ) {
		return qfalse;
	}

	Com_sprintf( level.voteString, sizeof( level.voteString ), "%s %s", arg1, arg2 );
	Com_sprintf( level.voteDisplayString, sizeof( level.voteDisplayString ), "poll %s", arg2 );
	Q_strncpyz( level.voteStringClean, level.voteString, sizeof( level.voteStringClean ) );

	return qtrue;
}

qboolean G_VoteMap( gentity_t *ent, int numArgs, const char *arg1, const char *arg2 ) {
	char s[MAX_CVAR_VALUE_STRING] = {0}, bspName[MAX_QPATH] = {0}, *mapName = NULL, *mapName2 = NULL;
	fileHandle_t fp = NULL_FILE;
	const char *arenaInfo;

	// didn't specify a map, show available maps
	if ( numArgs < 3 ) {
		Cmd_MapList_f( ent );
		return qfalse;
	}

	if ( strchr( arg2, '\\' ) ) {
		trap->SendServerCommand( ent-g_entities, "print \"Can't have mapnames with a \\\n\"" );
		return qfalse;
	}

	Com_sprintf( bspName, sizeof(bspName), "maps/%s.bsp", arg2 );
	if ( trap->FS_Open( bspName, &fp, FS_READ ) <= 0 ) {
		trap->SendServerCommand( ent-g_entities, va( "print \"Can't find map %s on server\n\"", bspName ) );
		if( fp != NULL_FILE )
			trap->FS_Close( fp );
		return qfalse;
	}
	trap->FS_Close( fp );

	if ( !G_DoesMapSupportGametype( arg2, level.gametype ) ) {
		trap->SendServerCommand( ent-g_entities, va( "print \"%s\n\"", G_GetStringEdString( "MP_SVGAME", "NOVOTE_MAPNOTSUPPORTEDBYGAME" ) ) );
		return qfalse;
	}

	// preserve the map rotation
	trap->Cvar_VariableStringBuffer( "nextmap", s, sizeof( s ) );
	if ( *s )
		Com_sprintf( level.voteString, sizeof( level.voteString ), "%s %s; set nextmap \"%s\"", arg1, arg2, s );
	else
		Com_sprintf( level.voteString, sizeof( level.voteString ), "%s %s", arg1, arg2 );

	arenaInfo = G_GetArenaInfoByMap(arg2);
	if ( arenaInfo ) {
		mapName = Info_ValueForKey( arenaInfo, "longname" );
		mapName2 = Info_ValueForKey( arenaInfo, "map" );
	}

	if ( !mapName || !mapName[0] )
		mapName = "ERROR";

	if ( !mapName2 || !mapName2[0] )
		mapName2 = "ERROR";

	Com_sprintf( level.voteDisplayString, sizeof( level.voteDisplayString ), "map %s (%s)", mapName, mapName2 );
	Q_strncpyz( level.voteStringClean, level.voteString, sizeof( level.voteStringClean ) );
	return qtrue;
}

qboolean G_VoteMapRestart( gentity_t *ent, int numArgs, const char *arg1, const char *arg2 ) {
	int n = Com_Clampi( 0, 60, atoi( arg2 ) );
	if ( numArgs < 3 )
		n = 5;
	Com_sprintf( level.voteString, sizeof( level.voteString ), "%s %i", arg1, n );
	Q_strncpyz( level.voteDisplayString, level.voteString, sizeof( level.voteDisplayString ) );
	Q_strncpyz( level.voteStringClean, level.voteString, sizeof( level.voteStringClean ) );
	return qtrue;
}

qboolean G_VoteNextmap( gentity_t *ent, int numArgs, const char *arg1, const char *arg2 ) {
	char s[MAX_CVAR_VALUE_STRING];

	trap->Cvar_VariableStringBuffer( "nextmap", s, sizeof( s ) );
	if ( !*s ) {
		trap->SendServerCommand( ent-g_entities, "print \"nextmap not set.\n\"" );
		return qfalse;
	}
	SiegeClearSwitchData();
	Com_sprintf( level.voteString, sizeof( level.voteString ), "vstr nextmap");
	Q_strncpyz( level.voteDisplayString, level.voteString, sizeof( level.voteDisplayString ) );
	Q_strncpyz( level.voteStringClean, level.voteString, sizeof( level.voteStringClean ) );
	return qtrue;
}

qboolean G_VoteTimelimit( gentity_t *ent, int numArgs, const char *arg1, const char *arg2 ) {
	float tl = Com_Clamp( 0.0f, 35790.0f, atof( arg2 ) );
	if ( Q_isintegral( tl ) )
		Com_sprintf( level.voteString, sizeof( level.voteString ), "%s %i", arg1, (int)tl );
	else
		Com_sprintf( level.voteString, sizeof( level.voteString ), "%s %.3f", arg1, tl );
	Q_strncpyz( level.voteDisplayString, level.voteString, sizeof( level.voteDisplayString ) );
	Q_strncpyz( level.voteStringClean, level.voteString, sizeof( level.voteStringClean ) );
	return qtrue;
}

qboolean G_VoteWarmup( gentity_t *ent, int numArgs, const char *arg1, const char *arg2 ) {
	int n = Com_Clampi( 0, 1, atoi( arg2 ) );
	Com_sprintf( level.voteString, sizeof( level.voteString ), "%s %i", arg1, n );
	Q_strncpyz( level.voteDisplayString, level.voteString, sizeof( level.voteDisplayString ) );
	Q_strncpyz( level.voteStringClean, level.voteString, sizeof( level.voteStringClean ) );
	return qtrue;
}

typedef struct voteString_s {
	const char	*string;
	const char	*aliases;	// space delimited list of aliases, will always show the real vote string
	qboolean	(*func)(gentity_t *ent, int numArgs, const char *arg1, const char *arg2);
	int			numArgs;	// number of REQUIRED arguments, not total/optional arguments
	uint32_t	validGT;	// bit-flag of valid gametypes
	qboolean	voteDelay;	// if true, will delay executing the vote string after it's accepted by g_voteDelay
	const char	*shortHelp;	// NULL if no arguments needed
} voteString_t;

static voteString_t validVoteStrings[] = {
	//	vote string				aliases										# args	valid gametypes							exec delay		short help
	{	"capturelimit",			"caps",				G_VoteCapturelimit,		1,		GTB_CTF|GTB_CTY,						qtrue,			"<num>" },
	{	"clientkick",			NULL,				G_VoteClientkick,		1,		GTB_ALL,								qfalse,			"<clientnum>" },
	{	"fraglimit",			"frags",			G_VoteFraglimit,		1,		GTB_ALL & ~(GTB_SIEGE|GTB_CTF|GTB_CTY),	qtrue,			"<num>" },
	{	"g_doWarmup",			"dowarmup warmup",	G_VoteWarmup,			1,		GTB_ALL,								qtrue,			"<0-1>" },
	{	"g_gametype",			"gametype gt mode",	G_VoteGametype,			1,		GTB_ALL,								qtrue,			"<num or name>" },
	{	"kick",					NULL,				G_VoteKick,				1,		GTB_ALL,								qfalse,			"<client name>" },
	{	"map",					NULL,				G_VoteMap,				0,		GTB_ALL,								qtrue,			"<name>" },
	{	"map_restart",			"restart",			G_VoteMapRestart,		0,		GTB_ALL,								qtrue,			"<optional delay>" },
	{	"nextmap",				NULL,				G_VoteNextmap,			0,		GTB_ALL,								qtrue,			NULL },
	{	"poll",					NULL,				G_VotePoll,				0,		GTB_ALL,								qtrue,			"<message>" },
	{	"timelimit",			"time",				G_VoteTimelimit,		1,		GTB_ALL,								qtrue,			"<num>" },
};
static const int validVoteStringsSize = ARRAY_LEN( validVoteStrings );

void Cmd_CallVote_f( gentity_t *ent ) {
	int				i=0, numArgs=0;
	char			arg1[MAX_CVAR_VALUE_STRING] = {0};
	char			arg2[MAX_CVAR_VALUE_STRING] = {0};
	voteString_t	*vote = NULL;
	int player_it = 0;
	gentity_t *player_ent = NULL;

	// not allowed to vote at all
	if ( !g_allowVote.integer ) {
		trap->SendServerCommand( ent-g_entities, va( "print \"%s\n\"", G_GetStringEdString( "MP_SVGAME", "NOVOTE" ) ) );
		return;
	}

	// vote in progress
	else if ( level.voteTime ) {
		trap->SendServerCommand( ent-g_entities, va( "print \"%s\n\"", G_GetStringEdString( "MP_SVGAME", "VOTEINPROGRESS" ) ) );
		return;
	}

	// can't vote as a spectator, except in (power)duel
	else if ( level.gametype != GT_DUEL && level.gametype != GT_POWERDUEL && ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
		trap->SendServerCommand( ent-g_entities, va( "print \"%s\n\"", G_GetStringEdString( "MP_SVGAME", "NOSPECVOTE" ) ) );
		return;
	}

	for (player_it = 0; player_it < level.maxclients; player_it++)
	{
		player_ent = &g_entities[player_it];
		if (player_ent && player_ent->client && player_ent->client->sess.amrpgmode == 2 && player_ent->client->pers.guardian_mode > 0)
		{
			trap->SendServerCommand( ent-g_entities, "print \"You cannot vote while someone is in a guardian battle\n\"");
			return;
		}
	}

	// make sure it is a valid command to vote on
	numArgs = trap->Argc();
	trap->Argv( 1, arg1, sizeof( arg1 ) );
	if ( numArgs > 1 )
		Q_strncpyz( arg2, ConcatArgs( 2 ), sizeof( arg2 ) );

	// filter ; \n \r
	if ( Q_strchrs( arg1, ";\r\n" ) || Q_strchrs( arg2, ";\r\n" ) ) {
		trap->SendServerCommand( ent-g_entities, "print \"Invalid vote string.\n\"" );
		return;
	}

	// check for invalid votes
	for ( i=0; i<validVoteStringsSize; i++ ) {
		if ( !(g_allowVote.integer & (1<<i)) )
			continue;

		if ( !Q_stricmp( arg1, validVoteStrings[i].string ) )
			break;

		// see if they're using an alias, and set arg1 to the actual vote string
		if ( validVoteStrings[i].aliases ) {
			char tmp[MAX_TOKEN_CHARS] = {0}, *p = NULL;
			const char *delim = " ";
			Q_strncpyz( tmp, validVoteStrings[i].aliases, sizeof( tmp ) );
			p = strtok( tmp, delim );
			while ( p != NULL ) {
				if ( !Q_stricmp( arg1, p ) ) {
					Q_strncpyz( arg1, validVoteStrings[i].string, sizeof( arg1 ) );
					goto validVote;
				}
				p = strtok( NULL, delim );
			}
		}
	}
	// invalid vote string, abandon ship
	if ( i == validVoteStringsSize ) {
		char buf[1024] = {0};
		int toggle = 0;
		trap->SendServerCommand( ent-g_entities, "print \"Invalid vote string.\n\"" );
		trap->SendServerCommand( ent-g_entities, "print \"Allowed vote strings are: \"" );
		for ( i=0; i<validVoteStringsSize; i++ ) {
			if ( !(g_allowVote.integer & (1<<i)) )
				continue;

			toggle = !toggle;
			if ( validVoteStrings[i].shortHelp ) {
				Q_strcat( buf, sizeof( buf ), va( "^%c%s %s ",
					toggle ? COLOR_GREEN : COLOR_YELLOW,
					validVoteStrings[i].string,
					validVoteStrings[i].shortHelp ) );
			}
			else {
				Q_strcat( buf, sizeof( buf ), va( "^%c%s ",
					toggle ? COLOR_GREEN : COLOR_YELLOW,
					validVoteStrings[i].string ) );
			}
		}

		//FIXME: buffer and send in multiple messages in case of overflow
		trap->SendServerCommand( ent-g_entities, va( "print \"%s\n\"", buf ) );
		return;
	}

validVote:
	vote = &validVoteStrings[i];
	if ( !(vote->validGT & (1<<level.gametype)) ) {
		trap->SendServerCommand( ent-g_entities, va( "print \"%s is not applicable in this gametype.\n\"", arg1 ) );
		return;
	}

	if ( numArgs < vote->numArgs+2 ) {
		trap->SendServerCommand( ent-g_entities, va( "print \"%s requires more arguments: %s\n\"", arg1, vote->shortHelp ) );
		return;
	}

	level.votingGametype = qfalse;

	level.voteExecuteDelay = vote->voteDelay ? g_voteDelay.integer : 0;

	// there is still a vote to be executed, execute it and store the new vote
	if ( level.voteExecuteTime ) {
		level.voteExecuteTime = 0;
		trap->SendConsoleCommand( EXEC_APPEND, va( "%s\n", level.voteString ) );
	}

	// pass the args onto vote-specific handlers for parsing/filtering
	if ( vote->func ) {
		if ( !vote->func( ent, numArgs, arg1, arg2 ) )
			return;
	}
	// otherwise assume it's a command
	else {
		Com_sprintf( level.voteString, sizeof( level.voteString ), "%s \"%s\"", arg1, arg2 );
		Q_strncpyz( level.voteDisplayString, level.voteString, sizeof( level.voteDisplayString ) );
		Q_strncpyz( level.voteStringClean, level.voteString, sizeof( level.voteStringClean ) );
	}
	Q_strstrip( level.voteStringClean, "\"\n\r", NULL );

	trap->SendServerCommand( -1, va( "print \"%s^7 %s (%s)\n\"", ent->client->pers.netname, G_GetStringEdString( "MP_SVGAME", "PLCALLEDVOTE" ), level.voteStringClean ) );

	// start the voting, the caller automatically votes yes
	level.voteTime = level.time;
	level.voteYes = 1;
	level.voteNo = 0;

	for ( i=0; i<level.maxclients; i++ ) {
		level.clients[i].mGameFlags &= ~PSG_VOTED;
		level.clients[i].pers.vote = 0;
	}

	ent->client->mGameFlags |= PSG_VOTED;
	ent->client->pers.vote = 1;

	trap->SetConfigstring( CS_VOTE_TIME,	va( "%i", level.voteTime ) );
	trap->SetConfigstring( CS_VOTE_STRING,	level.voteDisplayString );
	trap->SetConfigstring( CS_VOTE_YES,		va( "%i", level.voteYes ) );
	trap->SetConfigstring( CS_VOTE_NO,		va( "%i", level.voteNo ) );
}

/*
==================
Cmd_Vote_f
==================
*/
void Cmd_Vote_f( gentity_t *ent ) {
	char		msg[64] = {0};

	if ( !level.voteTime ) {
		trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "NOVOTEINPROG")) );
		return;
	}
	if ( ent->client->mGameFlags & PSG_VOTED ) {
		trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "VOTEALREADY")) );
		return;
	}
	if (level.gametype != GT_DUEL && level.gametype != GT_POWERDUEL)
	{
		if ( ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
			trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "NOVOTEASSPEC")) );
			return;
		}
	}

	trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "PLVOTECAST")) );

	ent->client->mGameFlags |= PSG_VOTED;

	trap->Argv( 1, msg, sizeof( msg ) );

	if ( tolower( msg[0] ) == 'y' || msg[0] == '1' ) {
		level.voteYes++;
		ent->client->pers.vote = 1;
		trap->SetConfigstring( CS_VOTE_YES, va("%i", level.voteYes ) );
	} else {
		level.voteNo++;
		ent->client->pers.vote = 2;
		trap->SetConfigstring( CS_VOTE_NO, va("%i", level.voteNo ) );
	}

	// a majority will be determined in CheckVote, which will also account
	// for players entering or leaving
}

qboolean G_TeamVoteLeader( gentity_t *ent, int cs_offset, team_t team, int numArgs, const char *arg1, const char *arg2 ) {
	int clientid = numArgs == 2 ? ent->s.number : ClientNumberFromString( ent, arg2, qfalse );
	gentity_t *target = NULL;

	if ( clientid == -1 )
		return qfalse;

	target = &g_entities[clientid];
	if ( !target || !target->inuse || !target->client )
		return qfalse;

	if ( target->client->sess.sessionTeam != team )
	{
		trap->SendServerCommand( ent-g_entities, va( "print \"User %s is not on your team\n\"", arg2 ) );
		return qfalse;
	}

	Com_sprintf( level.teamVoteString[cs_offset], sizeof( level.teamVoteString[cs_offset] ), "leader %d", clientid );
	Q_strncpyz( level.teamVoteDisplayString[cs_offset], level.teamVoteString[cs_offset], sizeof( level.teamVoteDisplayString[cs_offset] ) );
	Q_strncpyz( level.teamVoteStringClean[cs_offset], level.teamVoteString[cs_offset], sizeof( level.teamVoteStringClean[cs_offset] ) );
	return qtrue;
}

/*
==================
Cmd_CallTeamVote_f
==================
*/
void Cmd_CallTeamVote_f( gentity_t *ent ) {
	team_t	team = ent->client->sess.sessionTeam;
	int		i=0, cs_offset=0, numArgs=0;
	char	arg1[MAX_CVAR_VALUE_STRING] = {0};
	char	arg2[MAX_CVAR_VALUE_STRING] = {0};

	if ( team == TEAM_RED )
		cs_offset = 0;
	else if ( team == TEAM_BLUE )
		cs_offset = 1;
	else
		return;

	// not allowed to vote at all
	if ( !g_allowTeamVote.integer ) {
		trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "NOVOTE")) );
		return;
	}

	// vote in progress
	else if ( level.teamVoteTime[cs_offset] ) {
		trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "TEAMVOTEALREADY")) );
		return;
	}

	// can't vote as a spectator
	else if ( ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
		trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "NOSPECVOTE")) );
		return;
	}

	// make sure it is a valid command to vote on
	numArgs = trap->Argc();
	trap->Argv( 1, arg1, sizeof( arg1 ) );
	if ( numArgs > 1 )
		Q_strncpyz( arg2, ConcatArgs( 2 ), sizeof( arg2 ) );

	// filter ; \n \r
	if ( Q_strchrs( arg1, ";\r\n" ) || Q_strchrs( arg2, ";\r\n" ) ) {
		trap->SendServerCommand( ent-g_entities, "print \"Invalid team vote string.\n\"" );
		return;
	}

	// pass the args onto vote-specific handlers for parsing/filtering
	if ( !Q_stricmp( arg1, "leader" ) ) {
		if ( !G_TeamVoteLeader( ent, cs_offset, team, numArgs, arg1, arg2 ) )
			return;
	}
	else {
		trap->SendServerCommand( ent-g_entities, "print \"Invalid team vote string.\n\"" );
		trap->SendServerCommand( ent-g_entities, va("print \"Allowed team vote strings are: ^%c%s %s\n\"", COLOR_GREEN, "leader", "<optional client name or number>" ));
		return;
	}

	Q_strstrip( level.teamVoteStringClean[cs_offset], "\"\n\r", NULL );

	for ( i=0; i<level.maxclients; i++ ) {
		if ( level.clients[i].pers.connected == CON_DISCONNECTED )
			continue;
		if ( level.clients[i].sess.sessionTeam == team )
			trap->SendServerCommand( i, va("print \"%s^7 called a team vote (%s)\n\"", ent->client->pers.netname, level.teamVoteStringClean[cs_offset] ) );
	}

	// start the voting, the caller autoamtically votes yes
	level.teamVoteTime[cs_offset] = level.time;
	level.teamVoteYes[cs_offset] = 1;
	level.teamVoteNo[cs_offset] = 0;

	for ( i=0; i<level.maxclients; i++ ) {
		if ( level.clients[i].pers.connected == CON_DISCONNECTED )
			continue;
		if ( level.clients[i].sess.sessionTeam == team ) {
			level.clients[i].mGameFlags &= ~PSG_TEAMVOTED;
			level.clients[i].pers.teamvote = 0;
		}
	}
	ent->client->mGameFlags |= PSG_TEAMVOTED;
	ent->client->pers.teamvote = 1;

	trap->SetConfigstring( CS_TEAMVOTE_TIME + cs_offset, va("%i", level.teamVoteTime[cs_offset] ) );
	trap->SetConfigstring( CS_TEAMVOTE_STRING + cs_offset, level.teamVoteDisplayString[cs_offset] );
	trap->SetConfigstring( CS_TEAMVOTE_YES + cs_offset, va("%i", level.teamVoteYes[cs_offset] ) );
	trap->SetConfigstring( CS_TEAMVOTE_NO + cs_offset, va("%i", level.teamVoteNo[cs_offset] ) );
}

/*
==================
Cmd_TeamVote_f
==================
*/
void Cmd_TeamVote_f( gentity_t *ent ) {
	team_t		team = ent->client->sess.sessionTeam;
	int			cs_offset=0;
	char		msg[64] = {0};

	if ( team == TEAM_RED )
		cs_offset = 0;
	else if ( team == TEAM_BLUE )
		cs_offset = 1;
	else
		return;

	if ( !level.teamVoteTime[cs_offset] ) {
		trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "NOTEAMVOTEINPROG")) );
		return;
	}
	if ( ent->client->mGameFlags & PSG_TEAMVOTED ) {
		trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "TEAMVOTEALREADYCAST")) );
		return;
	}
	if ( ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
		trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "NOVOTEASSPEC")) );
		return;
	}

	trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "PLTEAMVOTECAST")) );

	ent->client->mGameFlags |= PSG_TEAMVOTED;

	trap->Argv( 1, msg, sizeof( msg ) );

	if ( tolower( msg[0] ) == 'y' || msg[0] == '1' ) {
		level.teamVoteYes[cs_offset]++;
		ent->client->pers.teamvote = 1;
		trap->SetConfigstring( CS_TEAMVOTE_YES + cs_offset, va("%i", level.teamVoteYes[cs_offset] ) );
	} else {
		level.teamVoteNo[cs_offset]++;
		ent->client->pers.teamvote = 2;
		trap->SetConfigstring( CS_TEAMVOTE_NO + cs_offset, va("%i", level.teamVoteNo[cs_offset] ) );
	}

	// a majority will be determined in TeamCheckVote, which will also account
	// for players entering or leaving
}


/*
=================
Cmd_SetViewpos_f
=================
*/
void Cmd_SetViewpos_f( gentity_t *ent ) {
	vec3_t		origin, angles;
	char		buffer[MAX_TOKEN_CHARS];
	int			i;

	if ( trap->Argc() != 5 ) {
		trap->SendServerCommand( ent-g_entities, va("print \"usage: setviewpos x y z yaw\n\""));
		return;
	}

	VectorClear( angles );
	for ( i = 0 ; i < 3 ; i++ ) {
		trap->Argv( i + 1, buffer, sizeof( buffer ) );
		origin[i] = atof( buffer );
	}

	trap->Argv( 4, buffer, sizeof( buffer ) );
	angles[YAW] = atof( buffer );

	TeleportPlayer( ent, origin, angles );
}

void G_LeaveVehicle( gentity_t* ent, qboolean ConCheck ) {

	if (ent->client->ps.m_iVehicleNum)
	{ //tell it I'm getting off
		gentity_t *veh = &g_entities[ent->client->ps.m_iVehicleNum];

		if (veh->inuse && veh->client && veh->m_pVehicle)
		{
			if ( ConCheck ) { // check connection
				clientConnected_t pCon = ent->client->pers.connected;
				ent->client->pers.connected = CON_DISCONNECTED;
				veh->m_pVehicle->m_pVehicleInfo->Eject(veh->m_pVehicle, (bgEntity_t *)ent, qtrue);
				ent->client->pers.connected = pCon;
			} else { // or not.
				veh->m_pVehicle->m_pVehicleInfo->Eject(veh->m_pVehicle, (bgEntity_t *)ent, qtrue);
			}
		}
	}

	ent->client->ps.m_iVehicleNum = 0;
}

int G_ItemUsable(playerState_t *ps, int forcedUse)
{
	vec3_t fwd, fwdorg, dest, pos;
	vec3_t yawonly;
	vec3_t mins, maxs;
	vec3_t trtest;
	trace_t tr;

	// fix: dead players shouldn't use items
	if (ps->stats[STAT_HEALTH] <= 0) {
		return 0;
	}

	if (ps->m_iVehicleNum)
	{
		return 0;
	}

	if (ps->pm_flags & PMF_USE_ITEM_HELD)
	{ //force to let go first
		return 0;
	}

	if (!forcedUse)
	{
		forcedUse = bg_itemlist[ps->stats[STAT_HOLDABLE_ITEM]].giTag;
	}

	if (!BG_IsItemSelectable(ps, forcedUse))
	{
		return 0;
	}

	switch (forcedUse)
	{
	case HI_MEDPAC:
	case HI_MEDPAC_BIG:
		if (ps->stats[STAT_HEALTH] >= ps->stats[STAT_MAX_HEALTH])
		{
			return 0;
		}

		if (ps->stats[STAT_HEALTH] <= 0)
		{
			return 0;
		}

		return 1;
	case HI_SEEKER:
		if (ps->eFlags & EF_SEEKERDRONE)
		{
			G_AddEvent(&g_entities[ps->clientNum], EV_ITEMUSEFAIL, SEEKER_ALREADYDEPLOYED);
			return 0;
		}

		return 1;
	case HI_SENTRY_GUN:
		if (ps->fd.sentryDeployed)
		{
			G_AddEvent(&g_entities[ps->clientNum], EV_ITEMUSEFAIL, SENTRY_ALREADYPLACED);
			return 0;
		}

		yawonly[ROLL] = 0;
		yawonly[PITCH] = 0;
		yawonly[YAW] = ps->viewangles[YAW];

		VectorSet( mins, -8, -8, 0 );
		VectorSet( maxs, 8, 8, 24 );

		AngleVectors(yawonly, fwd, NULL, NULL);

		fwdorg[0] = ps->origin[0] + fwd[0]*64;
		fwdorg[1] = ps->origin[1] + fwd[1]*64;
		fwdorg[2] = ps->origin[2] + fwd[2]*64;

		trtest[0] = fwdorg[0] + fwd[0]*16;
		trtest[1] = fwdorg[1] + fwd[1]*16;
		trtest[2] = fwdorg[2] + fwd[2]*16;

		trap->Trace(&tr, ps->origin, mins, maxs, trtest, ps->clientNum, MASK_PLAYERSOLID, qfalse, 0, 0);

		if ((tr.fraction != 1 && tr.entityNum != ps->clientNum) || tr.startsolid || tr.allsolid)
		{
			G_AddEvent(&g_entities[ps->clientNum], EV_ITEMUSEFAIL, SENTRY_NOROOM);
			return 0;
		}

		return 1;
	case HI_SHIELD:
		mins[0] = -8;
		mins[1] = -8;
		mins[2] = 0;

		maxs[0] = 8;
		maxs[1] = 8;
		maxs[2] = 8;

		AngleVectors (ps->viewangles, fwd, NULL, NULL);
		fwd[2] = 0;
		VectorMA(ps->origin, 64, fwd, dest);
		trap->Trace(&tr, ps->origin, mins, maxs, dest, ps->clientNum, MASK_SHOT, qfalse, 0, 0 );
		if (tr.fraction > 0.9 && !tr.startsolid && !tr.allsolid)
		{
			VectorCopy(tr.endpos, pos);
			VectorSet( dest, pos[0], pos[1], pos[2] - 4096 );
			trap->Trace( &tr, pos, mins, maxs, dest, ps->clientNum, MASK_SOLID, qfalse, 0, 0 );
			if ( !tr.startsolid && !tr.allsolid )
			{
				return 1;
			}
		}
		G_AddEvent(&g_entities[ps->clientNum], EV_ITEMUSEFAIL, SHIELD_NOROOM);
		return 0;
	case HI_JETPACK: //do something?
		return 1;
	case HI_HEALTHDISP:
		return 1;
	case HI_AMMODISP:
		return 1;
	case HI_EWEB:
		return 1;
	case HI_CLOAK:
		return 1;
	default:
		return 1;
	}
}

void saberKnockDown(gentity_t *saberent, gentity_t *saberOwner, gentity_t *other);

void Cmd_ToggleSaber_f(gentity_t *ent)
{
	if (ent->client->ps.fd.forceGripCripple)
	{ //if they are being gripped, don't let them unholster their saber
		if (ent->client->ps.saberHolstered)
		{
			return;
		}
	}

	if (ent->client->ps.saberInFlight)
	{
		if (ent->client->ps.saberEntityNum)
		{ //turn it off in midair
			saberKnockDown(&g_entities[ent->client->ps.saberEntityNum], ent, ent);
		}
		return;
	}

	if (ent->client->ps.forceHandExtend != HANDEXTEND_NONE)
	{
		return;
	}

	if (ent->client->ps.weapon != WP_SABER)
	{
		return;
	}

//	if (ent->client->ps.duelInProgress && !ent->client->ps.saberHolstered)
//	{
//		return;
//	}

	if (ent->client->ps.duelTime >= level.time)
	{
		return;
	}

	if (ent->client->ps.saberLockTime >= level.time)
	{
		return;
	}

	// zyk: noclip does not allow toggle saber
	if ( ent->client->noclip == qtrue )
	{
		return;
	}

	if (ent->client && ent->client->ps.weaponTime < 1)
	{
		if (ent->client->ps.saberHolstered == 2)
		{
			ent->client->ps.saberHolstered = 0;

			if (ent->client->saber[0].soundOn)
			{
				G_Sound(ent, CHAN_AUTO, ent->client->saber[0].soundOn);
			}
			if (ent->client->saber[1].soundOn)
			{
				G_Sound(ent, CHAN_AUTO, ent->client->saber[1].soundOn);
			}
		}
		else
		{
			ent->client->ps.saberHolstered = 2;
			if (ent->client->saber[0].soundOff)
			{
				G_Sound(ent, CHAN_AUTO, ent->client->saber[0].soundOff);
			}
			if (ent->client->saber[1].soundOff &&
				ent->client->saber[1].model[0])
			{
				G_Sound(ent, CHAN_AUTO, ent->client->saber[1].soundOff);
			}
			//prevent anything from being done for 400ms after holster
			ent->client->ps.weaponTime = 400;
		}
	}
}

extern vmCvar_t		d_saberStanceDebug;

extern qboolean WP_SaberCanTurnOffSomeBlades( saberInfo_t *saber );
void Cmd_SaberAttackCycle_f(gentity_t *ent)
{
	int selectLevel = 0;
	qboolean usingSiegeStyle = qfalse;

	if ( !ent || !ent->client )
	{
		return;
	}
	if ( ent->client->ps.weapon != WP_SABER )
	{
        return;
	}
	/*
	if (ent->client->ps.weaponTime > 0)
	{ //no switching attack level when busy
		return;
	}
	*/

	if (ent->client->saber[0].model[0] && ent->client->saber[1].model[0])
	{ //no cycling for akimbo
		if ( WP_SaberCanTurnOffSomeBlades( &ent->client->saber[1] ) )
		{//can turn second saber off
			if ( ent->client->ps.saberHolstered == 1 )
			{//have one holstered
				//unholster it
				G_Sound(ent, CHAN_AUTO, ent->client->saber[1].soundOn);
				ent->client->ps.saberHolstered = 0;
				//g_active should take care of this, but...
				ent->client->ps.fd.saberAnimLevel = SS_DUAL;
			}
			else if ( ent->client->ps.saberHolstered == 0 )
			{//have none holstered
				if ( (ent->client->saber[1].saberFlags2&SFL2_NO_MANUAL_DEACTIVATE) )
				{//can't turn it off manually
				}
				else if ( ent->client->saber[1].bladeStyle2Start > 0
					&& (ent->client->saber[1].saberFlags2&SFL2_NO_MANUAL_DEACTIVATE2) )
				{//can't turn it off manually
				}
				else
				{
					//turn it off
					G_Sound(ent, CHAN_AUTO, ent->client->saber[1].soundOff);
					ent->client->ps.saberHolstered = 1;
					//g_active should take care of this, but...
					ent->client->ps.fd.saberAnimLevel = SS_FAST;
				}
			}

			if (d_saberStanceDebug.integer)
			{
				trap->SendServerCommand( ent-g_entities, va("print \"SABERSTANCEDEBUG: Attempted to toggle dual saber blade.\n\"") );
			}
			return;
		}
	}
	else if (ent->client->saber[0].numBlades > 1
		&& WP_SaberCanTurnOffSomeBlades( &ent->client->saber[0] ) )
	{ //use staff stance then.
		if ( ent->client->ps.saberHolstered == 1 )
		{//second blade off
			if ( ent->client->ps.saberInFlight )
			{//can't turn second blade back on if it's in the air, you naughty boy!
				if (d_saberStanceDebug.integer)
				{
					trap->SendServerCommand( ent-g_entities, va("print \"SABERSTANCEDEBUG: Attempted to toggle staff blade in air.\n\"") );
				}
				return;
			}
			//turn it on
			G_Sound(ent, CHAN_AUTO, ent->client->saber[0].soundOn);
			ent->client->ps.saberHolstered = 0;
			//g_active should take care of this, but...
			if ( ent->client->saber[0].stylesForbidden )
			{//have a style we have to use
				WP_UseFirstValidSaberStyle( &ent->client->saber[0], &ent->client->saber[1], ent->client->ps.saberHolstered, &selectLevel );
				if ( ent->client->ps.weaponTime <= 0 )
				{ //not busy, set it now
					ent->client->ps.fd.saberAnimLevel = selectLevel;
				}
				else
				{ //can't set it now or we might cause unexpected chaining, so queue it
					ent->client->saberCycleQueue = selectLevel;
				}
			}
		}
		else if ( ent->client->ps.saberHolstered == 0 )
		{//both blades on
			if ( (ent->client->saber[0].saberFlags2&SFL2_NO_MANUAL_DEACTIVATE) )
			{//can't turn it off manually
			}
			else if ( ent->client->saber[0].bladeStyle2Start > 0
				&& (ent->client->saber[0].saberFlags2&SFL2_NO_MANUAL_DEACTIVATE2) )
			{//can't turn it off manually
			}
			else
			{
				//turn second one off
				G_Sound(ent, CHAN_AUTO, ent->client->saber[0].soundOff);
				ent->client->ps.saberHolstered = 1;
				//g_active should take care of this, but...
				if ( ent->client->saber[0].singleBladeStyle != SS_NONE )
				{
					if ( ent->client->ps.weaponTime <= 0 )
					{ //not busy, set it now
						ent->client->ps.fd.saberAnimLevel = ent->client->saber[0].singleBladeStyle;
					}
					else
					{ //can't set it now or we might cause unexpected chaining, so queue it
						ent->client->saberCycleQueue = ent->client->saber[0].singleBladeStyle;
					}
				}
			}
		}
		if (d_saberStanceDebug.integer)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"SABERSTANCEDEBUG: Attempted to toggle staff blade.\n\"") );
		}
		return;
	}

	if (ent->client->saberCycleQueue)
	{ //resume off of the queue if we haven't gotten a chance to update it yet
		selectLevel = ent->client->saberCycleQueue;
	}
	else
	{
		selectLevel = ent->client->ps.fd.saberAnimLevel;
	}

	if (level.gametype == GT_SIEGE &&
		ent->client->siegeClass != -1 &&
		bgSiegeClasses[ent->client->siegeClass].saberStance)
	{ //we have a flag of useable stances so cycle through it instead
		int i = selectLevel+1;

		usingSiegeStyle = qtrue;

		while (i != selectLevel)
		{ //cycle around upward til we hit the next style or end up back on this one
			if (i >= SS_NUM_SABER_STYLES)
			{ //loop back around to the first valid
				i = SS_FAST;
			}

			if (bgSiegeClasses[ent->client->siegeClass].saberStance & (1 << i))
			{ //we can use this one, select it and break out.
				selectLevel = i;
				break;
			}
			i++;
		}

		if (d_saberStanceDebug.integer)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"SABERSTANCEDEBUG: Attempted to cycle given class stance.\n\"") );
		}
	}
	else
	{
		selectLevel++;
		if ( selectLevel > ent->client->ps.fd.forcePowerLevel[FP_SABER_OFFENSE] )
		{
			selectLevel = FORCE_LEVEL_1;
		}
		if (d_saberStanceDebug.integer)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"SABERSTANCEDEBUG: Attempted to cycle stance normally.\n\"") );
		}
	}
/*
#ifndef FINAL_BUILD
	switch ( selectLevel )
	{
	case FORCE_LEVEL_1:
		trap->SendServerCommand( ent-g_entities, va("print \"Lightsaber Combat Style: %sfast\n\"", S_COLOR_BLUE) );
		break;
	case FORCE_LEVEL_2:
		trap->SendServerCommand( ent-g_entities, va("print \"Lightsaber Combat Style: %smedium\n\"", S_COLOR_YELLOW) );
		break;
	case FORCE_LEVEL_3:
		trap->SendServerCommand( ent-g_entities, va("print \"Lightsaber Combat Style: %sstrong\n\"", S_COLOR_RED) );
		break;
	}
#endif
*/
	if ( !usingSiegeStyle )
	{
		//make sure it's valid, change it if not
		WP_UseFirstValidSaberStyle( &ent->client->saber[0], &ent->client->saber[1], ent->client->ps.saberHolstered, &selectLevel );
	}

	if (ent->client->ps.weaponTime <= 0)
	{ //not busy, set it now
		ent->client->ps.fd.saberAnimLevelBase = ent->client->ps.fd.saberAnimLevel = selectLevel;
	}
	else
	{ //can't set it now or we might cause unexpected chaining, so queue it
		ent->client->ps.fd.saberAnimLevelBase = ent->client->saberCycleQueue = selectLevel;
	}
}

qboolean G_OtherPlayersDueling(void)
{
	int i = 0;
	gentity_t *ent;

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->inuse && ent->client && ent->client->ps.duelInProgress)
		{
			return qtrue;
		}
		i++;
	}

	return qfalse;
}

void Cmd_EngageDuel_f(gentity_t *ent)
{
	trace_t tr;
	vec3_t forward, fwdOrg;

	if (!g_privateDuel.integer)
	{
		return;
	}

	if (level.gametype == GT_DUEL || level.gametype == GT_POWERDUEL)
	{ //rather pointless in this mode..
		trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "NODUEL_GAMETYPE")) );
		return;
	}

	//if (level.gametype >= GT_TEAM && level.gametype != GT_SIEGE)
	if (level.gametype >= GT_TEAM)
	{ //no private dueling in team modes
		trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "NODUEL_GAMETYPE")) );
		return;
	}

	if (ent->client->ps.duelTime >= level.time)
	{
		return;
	}

	if (ent->client->ps.weapon != WP_SABER)
	{
		return;
	}

	/*
	if (!ent->client->ps.saberHolstered)
	{ //must have saber holstered at the start of the duel
		return;
	}
	*/
	//NOTE: No longer doing this..

	if (ent->client->ps.saberInFlight)
	{
		return;
	}

	if (ent->client->ps.duelInProgress)
	{
		return;
	}

	//New: Don't let a player duel if he just did and hasn't waited 10 seconds yet (note: If someone challenges him, his duel timer will reset so he can accept)
	/*if (ent->client->ps.fd.privateDuelTime > level.time)
	{
		trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "CANTDUEL_JUSTDID")) );
		return;
	}

	if (G_OtherPlayersDueling())
	{
		trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", G_GetStringEdString("MP_SVGAME", "CANTDUEL_BUSY")) );
		return;
	}*/

	AngleVectors( ent->client->ps.viewangles, forward, NULL, NULL );

	fwdOrg[0] = ent->client->ps.origin[0] + forward[0]*256;
	fwdOrg[1] = ent->client->ps.origin[1] + forward[1]*256;
	fwdOrg[2] = (ent->client->ps.origin[2]+ent->client->ps.viewheight) + forward[2]*256;

	trap->Trace(&tr, ent->client->ps.origin, NULL, NULL, fwdOrg, ent->s.number, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (tr.fraction != 1 && tr.entityNum < MAX_CLIENTS)
	{
		gentity_t *challenged = &g_entities[tr.entityNum];

		if (!challenged || !challenged->client || !challenged->inuse ||
			challenged->health < 1 || challenged->client->ps.stats[STAT_HEALTH] < 1 ||
			challenged->client->ps.weapon != WP_SABER || challenged->client->ps.duelInProgress ||
			challenged->client->ps.saberInFlight)
		{
			return;
		}

		if (level.gametype >= GT_TEAM && OnSameTeam(ent, challenged))
		{
			return;
		}

		if (challenged->client->ps.duelIndex == ent->s.number && challenged->client->ps.duelTime >= level.time)
		{
			trap->SendServerCommand( /*challenged-g_entities*/-1, va("print \"%s %s %s!\n\"", challenged->client->pers.netname, G_GetStringEdString("MP_SVGAME", "PLDUELACCEPT"), ent->client->pers.netname) );

			ent->client->ps.duelInProgress = qtrue;
			challenged->client->ps.duelInProgress = qtrue;

			ent->client->ps.duelTime = level.time + 2000;
			challenged->client->ps.duelTime = level.time + 2000;

			G_AddEvent(ent, EV_PRIVATE_DUEL, 1);
			G_AddEvent(challenged, EV_PRIVATE_DUEL, 1);

			//Holster their sabers now, until the duel starts (then they'll get auto-turned on to look cool)

			if (!ent->client->ps.saberHolstered)
			{
				if (ent->client->saber[0].soundOff)
				{
					G_Sound(ent, CHAN_AUTO, ent->client->saber[0].soundOff);
				}
				if (ent->client->saber[1].soundOff &&
					ent->client->saber[1].model[0])
				{
					G_Sound(ent, CHAN_AUTO, ent->client->saber[1].soundOff);
				}
				ent->client->ps.weaponTime = 400;
				ent->client->ps.saberHolstered = 2;
			}
			if (!challenged->client->ps.saberHolstered)
			{
				if (challenged->client->saber[0].soundOff)
				{
					G_Sound(challenged, CHAN_AUTO, challenged->client->saber[0].soundOff);
				}
				if (challenged->client->saber[1].soundOff &&
					challenged->client->saber[1].model[0])
				{
					G_Sound(challenged, CHAN_AUTO, challenged->client->saber[1].soundOff);
				}
				challenged->client->ps.weaponTime = 400;
				challenged->client->ps.saberHolstered = 2;
			}
		}
		else
		{
			//Print the message that a player has been challenged in private, only announce the actual duel initiation in private
			trap->SendServerCommand( challenged-g_entities, va("cp \"%s %s\n\"", ent->client->pers.netname, G_GetStringEdString("MP_SVGAME", "PLDUELCHALLENGE")) );
			trap->SendServerCommand( ent-g_entities, va("cp \"%s %s\n\"", G_GetStringEdString("MP_SVGAME", "PLDUELCHALLENGED"), challenged->client->pers.netname) );
		}

		challenged->client->ps.fd.privateDuelTime = 0; //reset the timer in case this player just got out of a duel. He should still be able to accept the challenge.

		ent->client->ps.forceHandExtend = HANDEXTEND_DUELCHALLENGE;
		ent->client->ps.forceHandExtendTime = level.time + 1000;

		ent->client->ps.duelIndex = challenged->s.number;
		ent->client->ps.duelTime = level.time + 5000;
	}
}

#ifndef FINAL_BUILD
extern stringID_table_t animTable[MAX_ANIMATIONS+1];

void Cmd_DebugSetSaberMove_f(gentity_t *self)
{
	int argNum = trap->Argc();
	char arg[MAX_STRING_CHARS];

	if (argNum < 2)
	{
		return;
	}

	trap->Argv( 1, arg, sizeof( arg ) );

	if (!arg[0])
	{
		return;
	}

	self->client->ps.saberMove = atoi(arg);
	self->client->ps.saberBlocked = BLOCKED_BOUNCE_MOVE;

	if (self->client->ps.saberMove >= LS_MOVE_MAX)
	{
		self->client->ps.saberMove = LS_MOVE_MAX-1;
	}

	Com_Printf("Anim for move: %s\n", animTable[saberMoveData[self->client->ps.saberMove].animToUse].name);
}

void Cmd_DebugSetBodyAnim_f(gentity_t *self)
{
	int argNum = trap->Argc();
	char arg[MAX_STRING_CHARS];
	int i = 0;

	if (argNum < 2)
	{
		return;
	}

	trap->Argv( 1, arg, sizeof( arg ) );

	if (!arg[0])
	{
		return;
	}

	while (i < MAX_ANIMATIONS)
	{
		if (!Q_stricmp(arg, animTable[i].name))
		{
			break;
		}
		i++;
	}

	if (i == MAX_ANIMATIONS)
	{
		Com_Printf("Animation '%s' does not exist\n", arg);
		return;
	}

	G_SetAnim(self, NULL, SETANIM_BOTH, i, SETANIM_FLAG_OVERRIDE|SETANIM_FLAG_HOLD, 0);

	Com_Printf("Set body anim to %s\n", arg);
}
#endif

void StandardSetBodyAnim(gentity_t *self, int anim, int flags)
{
	G_SetAnim(self, NULL, SETANIM_BOTH, anim, flags, 0);
}

void DismembermentTest(gentity_t *self);

void Bot_SetForcedMovement(int bot, int forward, int right, int up);

#ifndef FINAL_BUILD
extern void DismembermentByNum(gentity_t *self, int num);
extern void G_SetVehDamageFlags( gentity_t *veh, int shipSurf, int damageLevel );
#endif

extern void sleeping_flowers(gentity_t *ent, int stun_time, int distance);
extern void healing_water(gentity_t *ent, int heal_amount);
extern void earthquake(gentity_t *ent, int stun_time, int strength, int distance);
qboolean TryGrapple(gentity_t *ent)
{
	if (ent->client->ps.weaponTime > 0)
	{ //weapon busy
		return qfalse;
	}
	if (ent->client->ps.forceHandExtend != HANDEXTEND_NONE)
	{ //force power or knockdown or something
		return qfalse;
	}
	if (ent->client->grappleState)
	{ //already grappling? but weapontime should be > 0 then..
		return qfalse;
	}

	if (ent->client->ps.weapon != WP_SABER && ent->client->ps.weapon != WP_MELEE)
	{
		return qfalse;
	}

	if (ent->client->ps.weapon == WP_SABER && !ent->client->ps.saberHolstered)
	{
		Cmd_ToggleSaber_f(ent);
		if (!ent->client->ps.saberHolstered)
		{ //must have saber holstered
			return qfalse;
		}
	}

	//G_SetAnim(ent, &ent->client->pers.cmd, SETANIM_BOTH, BOTH_KYLE_PA_1, SETANIM_FLAG_OVERRIDE|SETANIM_FLAG_HOLD, 0);
	G_SetAnim(ent, &ent->client->pers.cmd, SETANIM_BOTH, BOTH_KYLE_GRAB, SETANIM_FLAG_OVERRIDE|SETANIM_FLAG_HOLD, 0);
	if (ent->client->ps.torsoAnim == BOTH_KYLE_GRAB)
	{ //providing the anim set succeeded..
		ent->client->ps.torsoTimer += 500; //make the hand stick out a little longer than it normally would
		if (ent->client->ps.legsAnim == ent->client->ps.torsoAnim)
		{
			ent->client->ps.legsTimer = ent->client->ps.torsoTimer;
		}
		ent->client->ps.weaponTime = ent->client->ps.torsoTimer;

		// zyk: Ultimate Power
		if (ent->client->sess.amrpgmode == 2 && ent->client->pers.universe_quest_progress == NUMBER_OF_UNIVERSE_QUEST_OBJECTIVES && ent->client->pers.ultimate_power_timer < level.time && !(ent->client->pers.player_settings & (1 << 5)))
		{
			if (ent->client->pers.universe_quest_counter & (1 << 0))
			{ // zyk: Sleeping Flowers
				sleeping_flowers(ent,4000,500);
				ent->client->pers.ultimate_power_timer = level.time + 30000;
				trap->SendServerCommand( -1, va("chat \"%s^7: ^7Sleeping Flowers!\"", ent->client->pers.netname));
			}
			else if (ent->client->pers.universe_quest_counter & (1 << 1))
			{ // zyk: uses Elemental Power
				if (ent->client->pers.ultimate_power_user == 0)
				{ // zyk: Healing Water
					healing_water(ent,100);
					ent->client->pers.ultimate_power_user = 1;
					trap->SendServerCommand( -1, va("chat \"%s^7: ^7Healing Water!\"", ent->client->pers.netname));
				}
				else if (ent->client->pers.ultimate_power_user == 1)
				{
					ent->client->pers.flame_thrower = level.time + 5000;
					ent->client->pers.ultimate_power_user = 2;
					trap->SendServerCommand( -1, va("chat \"%s^7: ^7Flame Burst!\"", ent->client->pers.netname));
				}
				else if (ent->client->pers.ultimate_power_user == 2)
				{
					earthquake(ent,2000,700,500);
					ent->client->pers.ultimate_power_user = 3;
					trap->SendServerCommand( -1, va("chat \"%s^7: ^7Earthquake!\"", ent->client->pers.netname));
				}
				else if (ent->client->pers.ultimate_power_user == 3)
				{
					int i = 0;

					for ( i = 0; i < level.num_entities; i++)
					{
						gentity_t *player_ent = &g_entities[i];

						if (ent->s.number != i && player_ent && player_ent->client)
						{
							int distance = (int)Distance(ent->client->ps.origin,player_ent->client->ps.origin);

							if (distance < 1000)
							{
								int found = 0;

								// zyk: allies will not be hit by this power
								if (i < level.maxclients && (ent->client->sess.ally1 == i || ent->client->sess.ally2 == i || ent->client->sess.ally3 == i))
								{
									found = 1;
								}

								if (found == 0)
								{
									player_ent->client->pers.ultimate_power_user = ent->s.number;
									player_ent->client->pers.ultimate_power_target = 10;
									player_ent->client->pers.ultimate_power_target_timer = level.time + 5000;
							
									if (i < level.maxclients)
										G_Sound(player_ent, CHAN_AUTO, G_SoundIndex("sound/effects/vacuum.mp3"));
								}
							}
						}
					}

					ent->client->pers.ultimate_power_user = 0;
					trap->SendServerCommand( -1, va("chat \"%s^7: ^7Blowing Wind!\"", ent->client->pers.netname));
				}

				ent->client->pers.ultimate_power_timer = level.time + 15000;
			}
			else if (ent->client->pers.universe_quest_counter & (1 << 2))
			{ // zyk: uses Chaos Power
				int i = 0;

				for (i = 0; i < level.num_entities; i++)
				{
					gentity_t *player_ent = &g_entities[i];
					
					if (ent->s.number != i && player_ent && player_ent->client && !player_ent->client->ps.duelInProgress)
					{
						int distance = (int)Distance(ent->client->ps.origin,player_ent->client->ps.origin);

						if (distance < 500)
						{
							int found = 0;

							// zyk: allies will not be hit by this power
							if (i < level.maxclients && (ent->client->sess.ally1 == i || ent->client->sess.ally2 == i || ent->client->sess.ally3 == i))
							{
								found = 1;
							}

							if (found == 0)
							{
								player_ent->client->pers.ultimate_power_user = ent->s.number;
								player_ent->client->pers.ultimate_power_target = 4;
								player_ent->client->pers.ultimate_power_target_timer = level.time + 2000;

								if (player_ent->client->jetPackOn)
								{
									Jetpack_Off(player_ent);
								}

								// zyk: First Chaos Power hit
								player_ent->client->ps.forceHandExtend = HANDEXTEND_KNOCKDOWN;
								player_ent->client->ps.forceHandExtendTime = level.time + 5000;
								player_ent->client->ps.velocity[2] += 150;
								player_ent->client->ps.forceDodgeAnim = 0;
								player_ent->client->ps.quickerGetup = qtrue;
								player_ent->client->ps.electrifyTime = level.time + 5000;

								G_Damage(player_ent,ent,ent,NULL,NULL,100,0,MOD_UNKNOWN);
							}
						}
					}
				}

				trap->SendServerCommand( -1, va("chat \"%s^7: ^7Chaos Power!\"", ent->client->pers.netname));

				ent->client->pers.ultimate_power_timer = level.time + 30000;
			}
			else if (ent->client->pers.universe_quest_counter & (1 << 3))
			{ // zyk: uses Time Power
				int i = 0;

				for (i = 0; i < level.num_entities; i++)
				{
					gentity_t *player_ent = &g_entities[i];
					
					if (ent->s.number != i && player_ent && player_ent->client)
					{
						int distance = (int)Distance(ent->client->ps.origin,player_ent->client->ps.origin);
						if (distance < 500)
						{
							int found = 0;

							// zyk: allies will not be hit by this power
							if (i < level.maxclients && (ent->client->sess.ally1 == i || ent->client->sess.ally2 == i || ent->client->sess.ally3 == i))
							{
								found = 1;
							}

							if (found == 0)
							{
								player_ent->client->pers.ultimate_power_target = 3;
								player_ent->client->pers.ultimate_power_target_timer = level.time + 5000;
							}
						}
					}
				}

				trap->SendServerCommand( -1, va("chat \"%s^7: ^7Time Power!\"", ent->client->pers.netname));

				ent->client->pers.ultimate_power_timer = level.time + 30000;
			}
		}

		return qtrue;
	}

	return qfalse;
}

void Cmd_TargetUse_f( gentity_t *ent )
{
	if ( trap->Argc() > 1 )
	{
		char sArg[MAX_STRING_CHARS] = {0};
		gentity_t *targ;

		trap->Argv( 1, sArg, sizeof( sArg ) );
		targ = G_Find( NULL, FOFS( targetname ), sArg );

		while ( targ )
		{
			if ( targ->use )
				targ->use( targ, ent, ent );
			targ = G_Find( targ, FOFS( targetname ), sArg );
		}
	}
}

void Cmd_TheDestroyer_f( gentity_t *ent ) {
	if ( !ent->client->ps.saberHolstered || ent->client->ps.weapon != WP_SABER )
		return;

	Cmd_ToggleSaber_f( ent );
}

void Cmd_BotMoveForward_f( gentity_t *ent ) {
	int arg = 4000;
	int bCl = 0;
	char sarg[MAX_STRING_CHARS];

	assert( trap->Argc() > 1 );
	trap->Argv( 1, sarg, sizeof( sarg ) );

	assert( sarg[0] );
	bCl = atoi( sarg );
	Bot_SetForcedMovement( bCl, arg, -1, -1 );
}

void Cmd_BotMoveBack_f( gentity_t *ent ) {
	int arg = -4000;
	int bCl = 0;
	char sarg[MAX_STRING_CHARS];

	assert( trap->Argc() > 1 );
	trap->Argv( 1, sarg, sizeof( sarg ) );

	assert( sarg[0] );
	bCl = atoi( sarg );
	Bot_SetForcedMovement( bCl, arg, -1, -1 );
}

void Cmd_BotMoveRight_f( gentity_t *ent ) {
	int arg = 4000;
	int bCl = 0;
	char sarg[MAX_STRING_CHARS];

	assert( trap->Argc() > 1 );
	trap->Argv( 1, sarg, sizeof( sarg ) );

	assert( sarg[0] );
	bCl = atoi( sarg );
	Bot_SetForcedMovement( bCl, -1, arg, -1 );
}

void Cmd_BotMoveLeft_f( gentity_t *ent ) {
	int arg = -4000;
	int bCl = 0;
	char sarg[MAX_STRING_CHARS];

	assert( trap->Argc() > 1 );
	trap->Argv( 1, sarg, sizeof( sarg ) );

	assert( sarg[0] );
	bCl = atoi( sarg );
	Bot_SetForcedMovement( bCl, -1, arg, -1 );
}

void Cmd_BotMoveUp_f( gentity_t *ent ) {
	int arg = 4000;
	int bCl = 0;
	char sarg[MAX_STRING_CHARS];

	assert( trap->Argc() > 1 );
	trap->Argv( 1, sarg, sizeof( sarg ) );

	assert( sarg[0] );
	bCl = atoi( sarg );
	Bot_SetForcedMovement( bCl, -1, -1, arg );
}

void Cmd_AddBot_f( gentity_t *ent ) {
	//because addbot isn't a recognized command unless you're the server, but it is in the menus regardless
	trap->SendServerCommand( ent-g_entities, va( "print \"%s.\n\"", G_GetStringEdString( "MP_SVGAME", "ONLY_ADD_BOTS_AS_SERVER" ) ) );
}

// zyk: new functions

// zyk: sets the Max HP a player can have in RPG Mode
void set_max_health(gentity_t *ent)
{
	// zyk: Universe Power
	if (ent->client->pers.universe_quest_progress > 7 && !(ent->client->pers.player_settings & (1 << 4)))
		ent->client->pers.max_rpg_health = 150 + (ent->client->pers.level * 2);
	else
		ent->client->pers.max_rpg_health = 100 + (ent->client->pers.level * 2);

	ent->client->ps.stats[STAT_MAX_HEALTH] = ent->client->pers.max_rpg_health;
}

// zyk: sets the Max Shield a player can have in RPG Mode
void set_max_shield(gentity_t *ent)
{
	ent->client->pers.max_rpg_shield = (int)ceil(((ent->client->pers.starting_shield_level * 1.0)/5) * ent->client->pers.max_rpg_health);
}

// zyk: gives credits to the player
void add_credits(gentity_t *ent, int credits)
{
	ent->client->pers.credits += credits;
	if (ent->client->pers.credits > 500000)
		ent->client->pers.credits = 500000;
}

// zyk: removes credits from the player
void remove_credits(gentity_t *ent, int credits)
{
	ent->client->pers.credits -= credits;
	if (ent->client->pers.credits < 0)
		ent->client->pers.credits = 0;
}

// zyk: loads the player account
void load_account(gentity_t *ent, qboolean change_mode)
{
	FILE *account_file;
	char content[128];
	char file_content[1024];
	int value = 0;
	int i = 0;
	strcpy(content,"");
	strcpy(file_content,"");
	account_file = fopen(va("accounts/%s.txt",ent->client->sess.filename),"r");
	if (account_file != NULL)
	{
		// zyk: loading the amrpgmode value
		fscanf(account_file,"%s",content);
		value = atoi(content);
		ent->client->sess.amrpgmode = value;

		if (change_mode == qtrue)
		{
			if (ent->client->sess.amrpgmode == 2)
				ent->client->sess.amrpgmode = 1;
			else
				ent->client->sess.amrpgmode = 2;
		}

		ent->client->pers.vehicle_cloak_timer = 0;

		// zyk: initializing mind control attributes used in RPG mode
		ent->client->pers.being_mind_controlled = -1;
		ent->client->pers.mind_control = 0;
		ent->client->pers.mind_controlled1_id = -1;

		// zyk: loading player_settings value
		fscanf(account_file,"%s",content);
		value = atoi(content);
		ent->client->pers.player_settings = value;

		// zyk: loading the admin command bit value
		fscanf(account_file,"%s",content);
		value = atoi(content);
		ent->client->pers.bitvalue = value;

		if (ent->client->sess.amrpgmode == 2)
		{
			// zyk: loading level up score value
			fscanf(account_file,"%s",content);
			ent->client->pers.level_up_score = atoi(content);

			// zyk: loading Level value
			fscanf(account_file,"%s",content);
			ent->client->pers.level = atoi(content);

			// zyk: loading Skillpoints value
			fscanf(account_file,"%s",content);
			ent->client->pers.skillpoints = atoi(content);

			// zyk: loading Jump value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.force_powers_levels[0] = value;

			// zyk: loading Push value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.force_powers_levels[1] = value;

			// zyk: loading Pull value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.force_powers_levels[2] = value;

			// zyk: loading Speed value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.force_powers_levels[3] = value;

			// zyk: loading Sense value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.force_powers_levels[4] = value;

			// zyk: loading Saber Offense value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.force_powers_levels[5] = value;

			// zyk: loading Saber Defense value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.force_powers_levels[6] = value;

			// zyk: loading Saber Throw value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.force_powers_levels[7] = value;

			// zyk: loading Absorb value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.force_powers_levels[8] = value;

			// zyk: loading Heal value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.force_powers_levels[9] = value;

			// zyk: loading Protect value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.force_powers_levels[10] = value;

			// zyk: loading Mind Trick value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.force_powers_levels[11] = value;

			// zyk: loading Team Heal value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.force_powers_levels[12] = value;

			// zyk: loading Lightning value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.force_powers_levels[13] = value;

			// zyk: loading Grip value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.force_powers_levels[14] = value;

			// zyk: loading Drain value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.force_powers_levels[15] = value;

			// zyk: loading Rage value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.force_powers_levels[16] = value;

			// zyk: loading Team Energize value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.force_powers_levels[17] = value;

			// zyk: loading Stun Baton value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.stun_baton_level = value;

			// zyk: loading Blaster Pistol value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.weapons_levels[0] = value;

			// zyk: loading E11 Blaster Rifle value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.weapons_levels[1] = value;

			// zyk: loading Sniper value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.weapons_levels[2] = value;

			// zyk: loading Bowcaster value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.weapons_levels[3] = value;

			// zyk: loading Repeater value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.weapons_levels[4] = value;

			// zyk: loading Demp2 value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.weapons_levels[5] = value;

			// zyk: loading Flechette value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.weapons_levels[6] = value;

			// zyk: loading Rocket Launcher value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.weapons_levels[7] = value;

			// zyk: loading Concussion value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.weapons_levels[8] = value;

			// zyk: loading Bryar Old value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.weapons_levels[9] = value;

			// zyk: loading Melee value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.melee_level = value;

			// zyk: loading Max Shield value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.starting_shield_level = value;

			// zyk: loading Shield Strength value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.shield_strength = value;

			// zyk: loading Health Strength value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.health_strength = value;

			// zyk: loading grapple hook value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.drain_shield = value;

			// zyk: loading jetpack value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.jetpack_level = value;

			// zyk: loading Sense Health value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.playerhealth = value;

			// zyk: loading Shield value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.shield = value;

			// zyk: loading Team Shield value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.teamshield = value;

			// zyk: loading Mind Control value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.mind_control = value;

			// zyk: loading blaster pack ammo value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.ammo_levels[0] = value;

			// zyk: loading power cell ammo value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.ammo_levels[1] = value;

			// zyk: loading metallic bolts ammo value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.ammo_levels[2] = value;

			// zyk: loading rocket ammo value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.ammo_levels[3] = value;

			// zyk: loading thermal ammo value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.ammo_levels[4] = value;

			// zyk: loading trip mine ammo value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.ammo_levels[5] = value;

			// zyk: loading det pack ammo value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.ammo_levels[6] = value;

			// zyk: loading Binoculars value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.holdable_items_levels[0] = value;

			// zyk: loading Bacta Canister value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.holdable_items_levels[1] = value;

			// zyk: loading Sentry Gun value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.holdable_items_levels[2] = value;

			// zyk: loading Seeker Drone value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.holdable_items_levels[3] = value;

			// zyk: loading E-Web value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.holdable_items_levels[4] = value;

			// zyk: loading Big Bacta value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.holdable_items_levels[5] = value;

			// zyk: loading Force Field value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.holdable_items_levels[6] = value;

			// zyk: loading Cloak Item value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.holdable_items_levels[7] = value;

			// zyk: loading Force Power level skill value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.max_force_power_level = value;

			// zyk: loading Improvements level skill value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.improvements_level = value;

			// zyk: Other RPG attributes
			// zyk: loading Light Quest Defeated Guardians number value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.defeated_guardians = value;

			// zyk: loading Dark Quest completed objectives value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.hunter_quest_progress = value;

			// zyk: loading Eternity Quest progress value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.eternity_quest_progress = value;

			// zyk: loading secrets found value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.secrets_found = value;

			// zyk: loading Universe Quest Progress value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.universe_quest_progress = value;

			// zyk: loading Universe Quest Counter value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.universe_quest_counter = value;

			// zyk: loading credits value
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.credits = value;

			// zyk: loading RPG class
			fscanf(account_file,"%s",content);
			value = atoi(content);
			ent->client->pers.rpg_class = value;
		}
		else if (ent->client->sess.amrpgmode == 1)
		{
			// zyk: loading the holster state of the saber based in the player settings
			if (ent->client->pers.player_settings & (1 << 11))
			{
				ent->client->ps.saberHolstered = 2;
			}
		
			// zyk: player starts with jetpack if it is enabled in player settings
			if (!(ent->client->pers.player_settings & (1 << 12)))
			{
				ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_JETPACK);
			}
			else
			{
				ent->client->ps.stats[STAT_HOLDABLE_ITEMS] &= ~(1 << HI_JETPACK);
				if (ent->client->jetPackOn)
				{
					Jetpack_Off(ent);
				}
			}

			ent->client->ps.fd.forcePowerMax = zyk_max_force_power.integer;

			// zyk: setting default max hp and shield
			ent->client->ps.stats[STAT_MAX_HEALTH] = 100;

			if (ent->health > 100)
				ent->health = 100;

			if (ent->client->ps.stats[STAT_ARMOR] > 100)
				ent->client->ps.stats[STAT_ARMOR] = 100;

			// zyk: reset the force powers of this player
			WP_InitForcePowers( ent );
		}

		fclose(account_file);
	}
	else
	{
		trap->SendServerCommand( ent-g_entities, "print \"There is no account with this login or password.\n\"" ); 
	}
}

// zyk: saves info into the player account file
void save_account(gentity_t *ent)
{
	// zyk: used to prevent account save in map change time or before loading account after changing map
	if (level.voteExecuteTime < level.time && ent->client->pers.connected == CON_CONNECTED)
	{
		if (ent->client->sess.amrpgmode == 2)
		{
			FILE *account_file;
			gclient_t *client;
			client = ent->client;
			account_file = fopen(va("accounts/%s.txt",ent->client->sess.filename),"w");
			fprintf(account_file,"%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n",
			client->sess.amrpgmode,client->pers.player_settings,client->pers.bitvalue,client->pers.level_up_score,client->pers.level,client->pers.skillpoints,client->pers.force_powers_levels[0],client->pers.force_powers_levels[1],client->pers.force_powers_levels[2]
			,client->pers.force_powers_levels[3],client->pers.force_powers_levels[4],client->pers.force_powers_levels[5],client->pers.force_powers_levels[6],client->pers.force_powers_levels[7],client->pers.force_powers_levels[8]
			,client->pers.force_powers_levels[9],client->pers.force_powers_levels[10],client->pers.force_powers_levels[11],client->pers.force_powers_levels[12],client->pers.force_powers_levels[13],client->pers.force_powers_levels[14]
			,client->pers.force_powers_levels[15],client->pers.force_powers_levels[16],client->pers.force_powers_levels[17],client->pers.stun_baton_level,client->pers.weapons_levels[0],client->pers.weapons_levels[1],client->pers.weapons_levels[2],client->pers.weapons_levels[3]
			,client->pers.weapons_levels[4],client->pers.weapons_levels[5],client->pers.weapons_levels[6],client->pers.weapons_levels[7],client->pers.weapons_levels[8],client->pers.weapons_levels[9],client->pers.melee_level,client->pers.starting_shield_level,client->pers.shield_strength
			,client->pers.health_strength,client->pers.drain_shield,client->pers.jetpack_level,client->pers.playerhealth,client->pers.shield,client->pers.teamshield,client->pers.mind_control,client->pers.ammo_levels[0],client->pers.ammo_levels[1],client->pers.ammo_levels[2]
			,client->pers.ammo_levels[3],client->pers.ammo_levels[4],client->pers.ammo_levels[5],client->pers.ammo_levels[6],client->pers.holdable_items_levels[0],client->pers.holdable_items_levels[1],client->pers.holdable_items_levels[2],client->pers.holdable_items_levels[3]
			,client->pers.holdable_items_levels[4],client->pers.holdable_items_levels[5],client->pers.holdable_items_levels[6],client->pers.holdable_items_levels[7],client->pers.max_force_power_level,client->pers.improvements_level,client->pers.defeated_guardians,client->pers.hunter_quest_progress
			,client->pers.eternity_quest_progress,client->pers.secrets_found,client->pers.universe_quest_progress,client->pers.universe_quest_counter,client->pers.credits,client->pers.rpg_class);
			fclose(account_file);
		}
		else if (ent->client->sess.amrpgmode == 1)
		{
			FILE *account_file;
			FILE *updated_account_file;
			char content[128];
			char file_content[1024];
			int i = 0;
			strcpy(content,"");
			strcpy(file_content,"");

			account_file = fopen(va("accounts/%s.txt",ent->client->sess.filename),"r");
			fscanf(account_file,"%s",content); // zyk: reads the player mode
			fscanf(account_file,"%s",content); // zyk: reads the player settings
			fscanf(account_file,"%s",content); // zyk: reads the bitvalue
			while (i < (NUMBER_OF_LINES - 3))
			{
				i++;
				fscanf(account_file,"%s",content);
				sprintf(file_content,"%s%s\n",file_content,content);
			}
			fclose(account_file);

			updated_account_file = fopen(va("accounts/%s.txt",ent->client->sess.filename),"w");
			fprintf(account_file,"%d\n%d\n%d\n%s",ent->client->sess.amrpgmode,ent->client->pers.player_settings,ent->client->pers.bitvalue,file_content);
			fclose(updated_account_file);
		}
		else if (ent->client->sess.amrpgmode == 0)
		{ // zyk: if this is a new account file, creates it with default values
			FILE *new_file;
			new_file = fopen(va("accounts/%s.txt",ent->client->sess.filename),"w");
			if (new_file != NULL)
			{
				fprintf(new_file,"2\n0\n0\n0\n1\n1\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n0\n");
				fclose(new_file);

				// zyk: removing the pistol when creating account because player starts without any skill
				ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_BRYAR_PISTOL);

				trap->SendServerCommand( ent-g_entities, "print \"Account created succesfully. Now use ^3/list^7\n\"" ); 
			}
			else
			{
				trap->SendServerCommand( ent-g_entities, "print \"Error in account creation.\n\"" ); 
			}
		}
	}
}

// zyk: gives rpg score to the player
void rpg_score(gentity_t *ent)
{
	int send_message = 0; // zyk: if its 1, sends the message in player console
	char message[1024];

	strcpy(message,"");

	add_credits(ent, (10 + ent->client->pers.credits_modifier));

	if (ent->client->pers.level < MAX_RPG_LEVEL)
	{
		ent->client->pers.level_up_score += (1 + ent->client->pers.score_modifier); // zyk: add score to the RPG mode score

		if (ent->client->pers.level_up_score >= ent->client->pers.level)
		{ // zyk: player got a new level
			ent->client->pers.level_up_score -= ent->client->pers.level;
			ent->client->pers.level++;

			if (ent->client->pers.level % 10 == 0) // zyk: every level divisible by 10 the player will get bonus skillpoints
			{
				ent->client->pers.skillpoints+=(ent->client->pers.level/10) + 1;
				sprintf(message,"%sYou reached ^3level %d ^7and got 1 + %d bonus skillpoints!\n", message, ent->client->pers.level, (ent->client->pers.level/10));
			}
			else
			{
				ent->client->pers.skillpoints++;
				sprintf(message,"%sYou reached ^3level %d ^7and have %d skillpoints.\n", message, ent->client->pers.level, ent->client->pers.skillpoints);
			}

			// zyk: got a new level, so change the max health and max shield
			set_max_health(ent);
			set_max_shield(ent);
			send_message = 1;
		}
	}
	save_account(ent); // zyk: saves new score and credits in the account file

	// zyk: cleaning the modifiers after they are applied
	ent->client->pers.credits_modifier = 0;
	ent->client->pers.score_modifier = 0;

	if (send_message == 1)
	{
		if (ent->client->pers.level == MAX_RPG_LEVEL)
		{ // zyk: if this is the max level, show this message
			trap->SendServerCommand( ent-g_entities, va("chat \"^7Congratulations, %s^7! You reached the max level %d!\n\"", ent->client->pers.netname, MAX_RPG_LEVEL) );
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, va("chat \"%s\"", message) );
		}
	}
}

// zyk: increases the RPG skill counter by this amount
void rpg_skill_counter(gentity_t *ent, int amount)
{
	if (ent && ent->client && ent->client->sess.amrpgmode == 2)
	{ // zyk: now RPG mode increases level up score after a certain amount of attacks
		ent->client->pers.skill_counter += amount;
		if (ent->client->pers.skill_counter >= MAX_SKILL_COUNTER)
		{
			ent->client->pers.skill_counter = 0;
			rpg_score(ent);
		}
	}
}

// zyk: initialize RPG skills of this player
void initialize_rpg_skills(gentity_t *ent)
{
	if (ent->client->sess.amrpgmode == 2)
	{
		// zyk: loading Jump value
		if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_LEVITATION)) && ent->client->pers.force_powers_levels[0] > 0)
			ent->client->ps.fd.forcePowersKnown |= (1 << FP_LEVITATION);
		if (ent->client->pers.force_powers_levels[0] == 0)
			ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_LEVITATION);
		ent->client->ps.fd.forcePowerLevel[FP_LEVITATION] = ent->client->pers.force_powers_levels[0];

		// zyk: loading Push value
		if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_PUSH)) && ent->client->pers.force_powers_levels[1] > 0)
			ent->client->ps.fd.forcePowersKnown |= (1 << FP_PUSH);
		if (ent->client->pers.force_powers_levels[1] == 0)
			ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_PUSH);
		ent->client->ps.fd.forcePowerLevel[FP_PUSH] = ent->client->pers.force_powers_levels[1];

		// zyk: loading Pull value
		if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_PULL)) && ent->client->pers.force_powers_levels[2] > 0)
			ent->client->ps.fd.forcePowersKnown |= (1 << FP_PULL);
		if (ent->client->pers.force_powers_levels[2] == 0)
			ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_PULL);
		ent->client->ps.fd.forcePowerLevel[FP_PULL] = ent->client->pers.force_powers_levels[2];

		// zyk: loading Speed value
		if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_SPEED)) && ent->client->pers.force_powers_levels[3] > 0)
			ent->client->ps.fd.forcePowersKnown |= (1 << FP_SPEED);
		if (ent->client->pers.force_powers_levels[3] == 0)
			ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_SPEED);
		ent->client->ps.fd.forcePowerLevel[FP_SPEED] = ent->client->pers.force_powers_levels[3];

		// zyk: loading Sense value
		if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_SEE)) && ent->client->pers.force_powers_levels[4] > 0)
			ent->client->ps.fd.forcePowersKnown |= (1 << FP_SEE);
		if (ent->client->pers.force_powers_levels[4] == 0)
			ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_SEE);
		ent->client->ps.fd.forcePowerLevel[FP_SEE] = ent->client->pers.force_powers_levels[4];

		// zyk: loading Saber Offense value
		if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_SABER_OFFENSE)) && ent->client->pers.force_powers_levels[5] > 0)
		{
			ent->client->ps.fd.forcePowersKnown |= (1 << FP_SABER_OFFENSE);
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_SABER);
		}
		if (ent->client->pers.force_powers_levels[5] == 0)
		{
			ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_SABER_OFFENSE);
			ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_SABER);
			ent->client->ps.weapon = WP_MELEE;
		}
		ent->client->ps.fd.forcePowerLevel[FP_SABER_OFFENSE] = ent->client->pers.force_powers_levels[5];

		// zyk: loading the saber style based in the player settings
		if (ent->client->saber[0].model[0] && ent->client->saber[1].model[0])
		{ // zyk: Duals
			ent->client->ps.fd.saberAnimLevelBase = ent->client->ps.fd.saberAnimLevel = ent->client->ps.fd.saberDrawAnimLevel = SS_DUAL;
				
			if (ent->client->pers.player_settings & (1 << 9) && ent->client->pers.force_powers_levels[5] >= 1)
			{
				ent->client->ps.saberHolstered = 1;
			}
		}
		else if (ent->client->saber[0].saberFlags&SFL_TWO_HANDED)
		{ // zyk: Staff
			ent->client->ps.fd.saberAnimLevel = ent->client->ps.fd.saberDrawAnimLevel = SS_STAFF;

			if (ent->client->pers.player_settings & (1 << 10) && ent->client->pers.force_powers_levels[5] >= 1)
			{
				ent->client->ps.saberHolstered = 1;
			}
		}
		else
		{ // zyk: Single Saber
			ent->client->ps.fd.saberAnimLevelBase = ent->client->ps.fd.saberAnimLevel = ent->client->ps.fd.saberDrawAnimLevel = ent->client->sess.saberLevel = ent->client->ps.fd.forcePowerLevel[FP_SABER_OFFENSE];

			if (ent->client->pers.player_settings & (1 << 26) && ent->client->pers.force_powers_levels[5] >= 2)
			{
				// ent->client->ps.fd.saberAnimLevelBase = ent->client->ps.fd.saberAnimLevel = ent->client->ps.fd.saberDrawAnimLevel = ent->client->sess.saberLevel = SS_MEDIUM;
				// ent->client->saberCycleQueue = ent->client->ps.fd.saberAnimLevel;
				ent->client->ps.fd.saberAnimLevel = SS_MEDIUM;
			}
			else if (ent->client->pers.player_settings & (1 << 27) && ent->client->pers.force_powers_levels[5] >= 3)
			{
				ent->client->ps.fd.saberAnimLevel = SS_STRONG;
			}
			else if (ent->client->pers.player_settings & (1 << 28) && ent->client->pers.force_powers_levels[5] >= 4)
			{
				ent->client->ps.fd.saberAnimLevel = SS_DESANN;
			}
			else if (ent->client->pers.player_settings & (1 << 29) && ent->client->pers.force_powers_levels[5] == 5)
			{
				ent->client->ps.fd.saberAnimLevel = SS_TAVION;
			}
			else if (ent->client->pers.force_powers_levels[5] >= 1)
			{
				ent->client->ps.fd.saberAnimLevel = SS_FAST;
			}
		}

		// zyk: loading the holster state of the saber based in the player settings
		if (ent->client->pers.player_settings & (1 << 11))
		{
			ent->client->ps.saberHolstered = 2;
		}

		// zyk: loading Saber Defense value
		if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_SABER_DEFENSE)) && ent->client->pers.force_powers_levels[6] > 0)
			ent->client->ps.fd.forcePowersKnown |= (1 << FP_SABER_DEFENSE);
		if (ent->client->pers.force_powers_levels[6] == 0)
			ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_SABER_DEFENSE);
		ent->client->ps.fd.forcePowerLevel[FP_SABER_DEFENSE] = ent->client->pers.force_powers_levels[6];

		// zyk: loading Saber Throw value
		if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_SABERTHROW)) && ent->client->pers.force_powers_levels[7] > 0)
			ent->client->ps.fd.forcePowersKnown |= (1 << FP_SABERTHROW);
		if (ent->client->pers.force_powers_levels[7] == 0)
			ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_SABERTHROW);
		ent->client->ps.fd.forcePowerLevel[FP_SABERTHROW] = ent->client->pers.force_powers_levels[7];

		// zyk: loading Absorb value
		if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_ABSORB)) && ent->client->pers.force_powers_levels[8] > 0)
			ent->client->ps.fd.forcePowersKnown |= (1 << FP_ABSORB);
		if (ent->client->pers.force_powers_levels[8] == 0)
			ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_ABSORB);

		if (ent->client->pers.force_powers_levels[8] < 4)
			ent->client->ps.fd.forcePowerLevel[FP_ABSORB] = ent->client->pers.force_powers_levels[8];
		else
			ent->client->ps.fd.forcePowerLevel[FP_ABSORB] = FORCE_LEVEL_3;

		// zyk: loading Heal value
		if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_HEAL)) && ent->client->pers.force_powers_levels[9] > 0)
			ent->client->ps.fd.forcePowersKnown |= (1 << FP_HEAL);
		if (ent->client->pers.force_powers_levels[9] == 0)
			ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_HEAL);
		ent->client->ps.fd.forcePowerLevel[FP_HEAL] = ent->client->pers.force_powers_levels[9];

		// zyk: loading Protect value
		if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_PROTECT)) && ent->client->pers.force_powers_levels[10] > 0)
			ent->client->ps.fd.forcePowersKnown |= (1 << FP_PROTECT);
		if (ent->client->pers.force_powers_levels[10] == 0)
			ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_PROTECT);
		ent->client->ps.fd.forcePowerLevel[FP_PROTECT] = ent->client->pers.force_powers_levels[10];

		// zyk: loading Mind Trick value
		if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_TELEPATHY)) && ent->client->pers.force_powers_levels[11] > 0)
			ent->client->ps.fd.forcePowersKnown |= (1 << FP_TELEPATHY);
		if (ent->client->pers.force_powers_levels[11] == 0)
			ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_TELEPATHY);
		ent->client->ps.fd.forcePowerLevel[FP_TELEPATHY] = ent->client->pers.force_powers_levels[11];

		// zyk: loading Team Heal value
		if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_TEAM_HEAL)) && ent->client->pers.force_powers_levels[12] > 0)
			ent->client->ps.fd.forcePowersKnown |= (1 << FP_TEAM_HEAL);
		if (ent->client->pers.force_powers_levels[12] == 0)
			ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_TEAM_HEAL);
		ent->client->ps.fd.forcePowerLevel[FP_TEAM_HEAL] = ent->client->pers.force_powers_levels[12];

		// zyk: loading Lightning value
		if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_LIGHTNING)) && ent->client->pers.force_powers_levels[13] > 0)
			ent->client->ps.fd.forcePowersKnown |= (1 << FP_LIGHTNING);
		if (ent->client->pers.force_powers_levels[13] == 0)
			ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_LIGHTNING);
		ent->client->ps.fd.forcePowerLevel[FP_LIGHTNING] = ent->client->pers.force_powers_levels[13];

		// zyk: loading Grip value
		if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_GRIP)) && ent->client->pers.force_powers_levels[14] > 0)
			ent->client->ps.fd.forcePowersKnown |= (1 << FP_GRIP);
		if (ent->client->pers.force_powers_levels[14] == 0)
			ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_GRIP);
		ent->client->ps.fd.forcePowerLevel[FP_GRIP] = ent->client->pers.force_powers_levels[14];

		// zyk: loading Drain value
		if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_DRAIN)) && ent->client->pers.force_powers_levels[15] > 0)
			ent->client->ps.fd.forcePowersKnown |= (1 << FP_DRAIN);
		if (ent->client->pers.force_powers_levels[15] == 0)
			ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_DRAIN);
		ent->client->ps.fd.forcePowerLevel[FP_DRAIN] = ent->client->pers.force_powers_levels[15];

		// zyk: loading Rage value
		if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_RAGE)) && ent->client->pers.force_powers_levels[16] > 0)
			ent->client->ps.fd.forcePowersKnown |= (1 << FP_RAGE);
		if (ent->client->pers.force_powers_levels[16] == 0)
			ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_RAGE);

		if (ent->client->pers.force_powers_levels[16] < 4)
			ent->client->ps.fd.forcePowerLevel[FP_RAGE] = ent->client->pers.force_powers_levels[16];
		else
			ent->client->ps.fd.forcePowerLevel[FP_RAGE] = FORCE_LEVEL_3;

		// zyk: loading Team Energize value
		if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_TEAM_FORCE)) && ent->client->pers.force_powers_levels[17] > 0)
			ent->client->ps.fd.forcePowersKnown |= (1 << FP_TEAM_FORCE);
		if (ent->client->pers.force_powers_levels[17] == 0)
			ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_TEAM_FORCE);
		ent->client->ps.fd.forcePowerLevel[FP_TEAM_FORCE] = ent->client->pers.force_powers_levels[17];

		// zyk: loading Melee
		if (!(ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_MELEE)))
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_MELEE);

		// zyk: player starts with jetpack if he has the skill and it is enabled in settings
		if (ent->client->pers.jetpack_level > 0 && !(ent->client->pers.player_settings & (1 << 12)))
		{
			ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_JETPACK);
		}
		else
		{
			ent->client->ps.stats[STAT_HOLDABLE_ITEMS] &= ~(1 << HI_JETPACK);
			if (ent->client->jetPackOn)
			{
				Jetpack_Off(ent);
			}
		}

		ent->client->pers.sense_health_timer = 0;

		ent->client->pers.max_force_power = (int)ceil((zyk_max_force_power.value/4.0) * ent->client->pers.max_force_power_level);
		ent->client->ps.fd.forcePowerMax = ent->client->pers.max_force_power;
		ent->client->ps.fd.forcePower = ent->client->ps.fd.forcePowerMax;

		if (ent->client->pers.rpg_class == 3)
		{ // zyk: setting the shot deflect of the Armored Soldier
			ent->flags |= FL_SHIELDED;
		}
		else
		{
			ent->flags &= ~FL_SHIELDED;
		}

		// zyk: setting rpg control attributes
		ent->client->pers.print_products_timer = 0;

		ent->client->pers.credits_modifier = 0;
		ent->client->pers.score_modifier = 0;

		// zyk: setting default value of can_play_quest
		ent->client->pers.can_play_quest = 0;

		ent->client->pers.guardian_mode = 0;
		ent->client->pers.guardian_timer = 0;
		ent->client->pers.guardian_invoked_by_id = -1;

		ent->client->pers.eternity_quest_timer = 0;

		ent->client->pers.universe_quest_artifact_holder_id = -1;
		ent->client->pers.universe_quest_messages = 0;
		ent->client->pers.universe_quest_timer = 0;

		ent->client->pers.light_quest_timer = 0;
		ent->client->pers.light_quest_messages = 0;

		ent->client->pers.hunter_quest_timer = 0;
		ent->client->pers.hunter_quest_messages = 0;

		// zyk: loading initial RPG weapons
		if (!(ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_STUN_BATON)) && ent->client->pers.stun_baton_level > 0)
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_STUN_BATON);
		if (ent->client->pers.stun_baton_level == 0)
			ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_STUN_BATON);

		if (!(ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_BRYAR_PISTOL)) && ent->client->pers.weapons_levels[0] > 0)
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_BRYAR_PISTOL);
		if (ent->client->pers.weapons_levels[0] == 0)
			ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_BRYAR_PISTOL);

		if (!(ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_BLASTER)) && ent->client->pers.weapons_levels[1] > 0)
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_BLASTER);
		if (ent->client->pers.weapons_levels[1] == 0)
			ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_BLASTER);

		if (!(ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_DISRUPTOR)) && ent->client->pers.weapons_levels[2] > 0)
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_DISRUPTOR);
		if (ent->client->pers.weapons_levels[2] == 0)
			ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_DISRUPTOR);

		if (!(ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_BOWCASTER)) && ent->client->pers.weapons_levels[3] > 0)
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_BOWCASTER);
		if (ent->client->pers.weapons_levels[3] == 0)
			ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_BOWCASTER);

		if (!(ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_REPEATER)) && ent->client->pers.weapons_levels[4] > 0)
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_REPEATER);
		if (ent->client->pers.weapons_levels[4] == 0)
			ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_REPEATER);

		if (!(ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_DEMP2)) && ent->client->pers.weapons_levels[5] > 0)
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_DEMP2);
		if (ent->client->pers.weapons_levels[5] == 0)
			ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_DEMP2);

		if (!(ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_FLECHETTE)) && ent->client->pers.weapons_levels[6] > 0)
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_FLECHETTE);
		if (ent->client->pers.weapons_levels[6] == 0)
			ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_FLECHETTE);

		if (!(ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_ROCKET_LAUNCHER)) && ent->client->pers.weapons_levels[7] > 0)
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_ROCKET_LAUNCHER);
		if (ent->client->pers.weapons_levels[7] == 0)
			ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_ROCKET_LAUNCHER);

		if (!(ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_CONCUSSION)) && ent->client->pers.weapons_levels[8] > 0)
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_CONCUSSION);
		if (ent->client->pers.weapons_levels[8] == 0)
			ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_CONCUSSION);

		if (!(ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_BRYAR_OLD)) && ent->client->pers.weapons_levels[9] > 0)
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_BRYAR_OLD);
		if (ent->client->pers.weapons_levels[9] == 0)
			ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_BRYAR_OLD);

		if (!(ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_THERMAL)) && ent->client->pers.ammo_levels[4] > 0)
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_THERMAL);
		if (ent->client->pers.ammo_levels[4] == 0)
			ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_THERMAL);

		if (!(ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_TRIP_MINE)) && ent->client->pers.ammo_levels[5] > 0)
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_TRIP_MINE);
		if (ent->client->pers.ammo_levels[5] == 0)
			ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_TRIP_MINE);

		if (!(ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_DET_PACK)) && ent->client->pers.ammo_levels[6] > 0)
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_DET_PACK);
		if (ent->client->pers.ammo_levels[6] == 0)
			ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_DET_PACK);

		// zyk: loading initial RPG ammo at spawn
		ent->client->ps.ammo[AMMO_BLASTER] = ((int)ceil(zyk_max_blaster_pack_ammo.value/3.0) * ent->client->pers.ammo_levels[0]);
		ent->client->ps.ammo[AMMO_POWERCELL] = ((int)ceil(zyk_max_power_cell_ammo.value/3.0) * ent->client->pers.ammo_levels[1]);
		ent->client->ps.ammo[AMMO_METAL_BOLTS] = ((int)ceil(zyk_max_metal_bolt_ammo.value/3.0) * ent->client->pers.ammo_levels[2]);
		ent->client->ps.ammo[AMMO_ROCKETS] = ((int)ceil(zyk_max_rocket_ammo.value/3.0) * ent->client->pers.ammo_levels[3]);
		ent->client->ps.ammo[AMMO_THERMAL] = ((int)ceil(zyk_max_thermal_ammo.value/3.0) * ent->client->pers.ammo_levels[4]);
		ent->client->ps.ammo[AMMO_TRIPMINE] = ((int)ceil(zyk_max_tripmine_ammo.value/3.0) * ent->client->pers.ammo_levels[5]);
		ent->client->ps.ammo[AMMO_DETPACK] = ((int)ceil(zyk_max_detpack_ammo.value/3.0) * ent->client->pers.ammo_levels[6]);
		
		if (ent->client->pers.rpg_class == 2)
		{ // zyk: modifying max ammo if the player is a Bounty Hunter
			ent->client->ps.ammo[AMMO_BLASTER] += ent->client->ps.ammo[AMMO_BLASTER]/6 * ent->client->pers.improvements_level;
			ent->client->ps.ammo[AMMO_POWERCELL] += ent->client->ps.ammo[AMMO_POWERCELL]/6 * ent->client->pers.improvements_level;
			ent->client->ps.ammo[AMMO_METAL_BOLTS] += ent->client->ps.ammo[AMMO_METAL_BOLTS]/6 * ent->client->pers.improvements_level;
			ent->client->ps.ammo[AMMO_ROCKETS] += ent->client->ps.ammo[AMMO_ROCKETS]/6 * ent->client->pers.improvements_level;
			ent->client->ps.ammo[AMMO_THERMAL] += ent->client->ps.ammo[AMMO_THERMAL]/6 * ent->client->pers.improvements_level;
			ent->client->ps.ammo[AMMO_TRIPMINE] += ent->client->ps.ammo[AMMO_TRIPMINE]/6 * ent->client->pers.improvements_level;
			ent->client->ps.ammo[AMMO_DETPACK] += ent->client->ps.ammo[AMMO_DETPACK]/6 * ent->client->pers.improvements_level;
		}

		// zyk: reseting initial holdable items of the player
		ent->client->ps.stats[STAT_HOLDABLE_ITEMS] &= ~(1 << HI_SEEKER) & ~(1 << HI_BINOCULARS) & ~(1 << HI_SENTRY_GUN) & ~(1 << HI_EWEB) & ~(1 << HI_CLOAK) & ~(1 << HI_SHIELD) & ~(1 << HI_MEDPAC) & ~(1 << HI_MEDPAC_BIG);

		if (ent->client->pers.holdable_items_levels[0] > 0)
			ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_BINOCULARS);

		if (ent->client->pers.holdable_items_levels[1] > 0)
			ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_MEDPAC);

		if (ent->client->pers.holdable_items_levels[2] > 0)
			ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_SENTRY_GUN);

		if (ent->client->pers.holdable_items_levels[3] > 0)
			ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_SEEKER);

		if (ent->client->pers.holdable_items_levels[4] > 0)
			ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_EWEB);

		if (ent->client->pers.holdable_items_levels[5] > 0)
			ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_MEDPAC_BIG);

		if (ent->client->pers.holdable_items_levels[6] > 0)
			ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_SHIELD);

		if (ent->client->pers.holdable_items_levels[7] > 0)
			ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_CLOAK);

		// zyk: loading initial health of the player
		set_max_health(ent);
		ent->health = ent->client->pers.max_rpg_health;
		ent->client->ps.stats[STAT_HEALTH] = ent->health;

		// zyk: loading initial shield of the player
		set_max_shield(ent);
		ent->client->ps.stats[STAT_ARMOR] = ent->client->pers.max_rpg_shield;
	}
}

/*
==================
Cmd_NewAccount_f
==================
*/
void Cmd_NewAccount_f( gentity_t *ent ) {
	FILE *logins_file;
	char arg1[MAX_STRING_CHARS];
	char arg2[MAX_STRING_CHARS];
	char content[1024];
	int i = 0;
	strcpy(content,"");
	if ( trap->Argc() != 3) 
	{ 
		trap->SendServerCommand( ent-g_entities, "print \"You must write a login and a password of your choice. Example: ^3/new test_login test_pass^7.\n\"" ); 
		return;
	}
	trap->Argv(1, arg1, sizeof( arg1 ));
	trap->Argv(2, arg2, sizeof( arg2 ));

	// zyk: creates the account if player is not logged in
	if (ent->client->sess.amrpgmode != 0)
	{
		trap->SendServerCommand( ent-g_entities, "print \"You are already logged in.\n\"" ); 
		return;
	}

	if (strlen(arg1) > 30)
	{
		trap->SendServerCommand( ent-g_entities, "print \"Login has a maximum of 30 characters.\n\"" ); 
		return;
	}
	if (strlen(arg2) > 30)
	{
		trap->SendServerCommand( ent-g_entities, "print \"Password has a maximum of 30 characters.\n\"" ); 
		return;
	}

	// zyk: validating if this login already exists
	system("dir /B accounts > accounts/accounts.txt");

	logins_file = fopen("accounts/accounts.txt","r");
	if (logins_file != NULL)
	{
		i = fscanf(logins_file, "%s", content);
		while (i != -1)
		{
			if (strstr(content,"_S_") != NULL)
			{ // zyk: validation must occur on the lines that contains the account file names and in files that the login has the same size as the arg1
				int j = 0;
				char login_name[64];
				strcpy(login_name,"");

				for (j = 0; j < strlen(content); j++)
				{
					if (content[j] == '_' && content[j+1] == 'S' && content[j+2] == '_')
					{
						break;
					}
					strcpy(login_name,va("%s%c",login_name,content[j]));
				}

				// zyk: 0 means it is the same login as login_name, which is not a valid one
				if (Q_stricmp( login_name, arg1 ) == 0)
				{ // zyk: login already exists
					fclose(logins_file);
					trap->SendServerCommand( ent-g_entities, "print \"Login is used by another player.\n\"" );
					return;
				}
			}
			i = fscanf(logins_file, "%s", content);
		}
		fclose(logins_file);
	}

	strcpy(ent->client->sess.filename, va("%s_S_%s",arg1,arg2));

	save_account(ent);

	// zyk: login the account file already
	load_account(ent, qfalse);

	initialize_rpg_skills(ent);
}

/*
==================
Cmd_LoginAccount_f
==================
*/
void Cmd_LoginAccount_f( gentity_t *ent ) {
	if (ent->client->sess.amrpgmode == 0)
	{
		char   arg1[MAX_STRING_CHARS];
		char   arg2[MAX_STRING_CHARS];

		if ( trap->Argc() != 3)
		{ 
			trap->SendServerCommand( ent-g_entities, "print \"You must write your login and password.\n\"" ); 
			return;
		}

		if (ent->client->sess.sessionTeam != TEAM_SPECTATOR)
		{
			trap->SendServerCommand( ent-g_entities, "print \"You must be at Spectator Mode to login.\n\"" );
			return;
		}

		trap->Argv(1, arg1, sizeof( arg1 ));
		trap->Argv(2, arg2, sizeof( arg2 ));

		strcpy(ent->client->sess.filename, va("%s_S_%s",arg1,arg2));

		load_account(ent, qfalse);

		if (ent->client->sess.amrpgmode == 1)
			trap->SendServerCommand( ent-g_entities, "print \"^7Account loaded succesfully in ^2Admin-Only Mode^7. Now use command ^3/list^7.\n\"" );
		else if (ent->client->sess.amrpgmode == 2)
		{
			initialize_rpg_skills(ent);
			trap->SendServerCommand( ent-g_entities, "print \"^7Account loaded succesfully in ^2RPG Mode^7. Now use command ^3/list^7.\n\"" );
		}
	}
	else
	{
		trap->SendServerCommand( ent-g_entities, "print \"You are already logged in.\n\"" );
	}
}

// zyk: clean the guardian npcs
void clean_guardians(gentity_t *ent)
{
	int i = 0;
	gentity_t *this_ent = NULL;

	for (i = MAX_CLIENTS; i < level.num_entities; i++)
	{
		this_ent = &g_entities[i];
		if (this_ent && this_ent->client && this_ent->NPC && this_ent->client->pers.guardian_invoked_by_id != -1 && this_ent->client->pers.guardian_invoked_by_id == (ent-g_entities))
		{
			G_FreeEntity(this_ent);
		}
	}
}

// zyk: number of artifacts collected by the player in Universe Quest
int number_of_artifacts(gentity_t *ent)
{
	int i = 0, collected_artifacts = 0;

	if (ent->client->pers.universe_quest_progress == 2)
	{
		for (i = 0; i < 10; i++)
		{
			if (ent->client->pers.universe_quest_counter & (1 << i))
			{
				collected_artifacts++;
			}
		}
	}

	return collected_artifacts;
}

// zyk: tests if the player has beaten the guardians before the Guardian of Light in Light Quest
qboolean light_quest_defeated_guardians(gentity_t *ent)
{
	int j = 0, number_of_guardians_defeated = 0;

	for (j = 4; j < 12; j++)
	{
		if (ent->client->pers.defeated_guardians & (1 << j))
		{
			number_of_guardians_defeated++;
		}
	}

	if (number_of_guardians_defeated == (NUMBER_OF_GUARDIANS - 1))
		return qtrue;
	else
		return qfalse;
}

// zyk: tests if the player has collected the 9 notes before the Guardian of Darkness in Dark Quest
qboolean dark_quest_collected_notes(gentity_t *ent)
{
	if (ent->client->pers.hunter_quest_progress & (1 << 4) && ent->client->pers.hunter_quest_progress & (1 << 5) && ent->client->pers.hunter_quest_progress & (1 << 6) && ent->client->pers.hunter_quest_progress & (1 << 7) && ent->client->pers.hunter_quest_progress & (1 << 8) && ent->client->pers.hunter_quest_progress & (1 << 9) && ent->client->pers.hunter_quest_progress & (1 << 10) && ent->client->pers.hunter_quest_progress & (1 << 11) && ent->client->pers.hunter_quest_progress & (1 << 12))
	{
		return qtrue;
	}
	else
	{
		return qfalse;
	}
}

extern void zyk_set_entity_field(gentity_t *ent, char *key, char *value);
extern void zyk_spawn_entity(gentity_t *ent);

// zyk: loads the datapad md3 model for the Dark Quest notes
void load_note_model(int x,int y,int z)
{
	gentity_t *new_ent = G_Spawn();

	zyk_set_entity_field(new_ent,"classname","misc_model_breakable");
	zyk_set_entity_field(new_ent,"origin",va("%d %d %d",x,y,z));
	zyk_set_entity_field(new_ent,"angles","0 90 0");
	zyk_set_entity_field(new_ent,"model","models/items/datapad.md3");

	zyk_spawn_entity(new_ent);

	level.quest_note_id = new_ent->s.number;
}

// zyk: loads the crystal md3 model for the Universe Quest crystals
gentity_t *load_crystal_model(int x,int y,int z, int yaw, int crystal_number)
{
	gentity_t *ent = G_Spawn();

	zyk_set_entity_field(ent,"classname","misc_model_breakable");
	zyk_set_entity_field(ent,"origin",va("%d %d %d",x,y,z));
	zyk_set_entity_field(ent,"angles",va("0 %d 0",yaw));

	if (crystal_number == 0)
		zyk_set_entity_field(ent,"model","models/map_objects/mp/crystal_blue.md3");
	else if (crystal_number == 1)
		zyk_set_entity_field(ent,"model","models/map_objects/mp/crystal_red.md3");
	else
		zyk_set_entity_field(ent,"model","models/map_objects/mp/crystal_green.md3");

	zyk_spawn_entity(ent);

	level.quest_model_id = ent->s.number;

	return ent;
}

// zyk: load an effect used in quests
gentity_t *load_effect(int x,int y,int z, int spawnflags, char *fxFile)
{
	gentity_t *ent = G_Spawn();

	zyk_set_entity_field(ent,"classname","fx_runner");
	zyk_set_entity_field(ent,"spawnflags",va("%d",spawnflags));
	zyk_set_entity_field(ent,"origin",va("%d %d %d",x,y,z));

	ent->s.modelindex = G_EffectIndex( fxFile );

	zyk_spawn_entity(ent);

	level.quest_effect_id = ent->s.number;

	return ent;
}

// zyk: cleans the note model if player gets it
void clean_note_model()
{
	if (level.quest_note_id != -1)
	{
		G_FreeEntity(&g_entities[level.quest_note_id]);
		level.quest_note_id = -1;
	}
}

// zyk: cleans the crystal model if player gets it
void clean_crystal_model()
{
	if (level.quest_model_id != -1)
	{
		G_FreeEntity(&g_entities[level.quest_model_id]);
		level.quest_model_id = -1;
	}
}

// zyk: cleans the effect if player reaches it
void clean_effect()
{
	if (level.quest_effect_id != -1)
	{
		G_FreeEntity(&g_entities[level.quest_effect_id]);
		level.quest_effect_id = -1;
	}
}

// zyk: amount of amulets got by the player in Amulets Mission of Universe Quest
int number_of_amulets(gentity_t *ent)
{
	int i = 0, number_of_amulets = 0;
	for (i = 0; i < 3; i++)
	{
		if (ent->client->pers.universe_quest_counter & (1 << i))
		{
			number_of_amulets++;
		}
	}
	return number_of_amulets;
}

// zyk: tests if player got all 3 amulets in Amulets Mission of Universe Quest
void got_all_amulets(gentity_t *ent)
{
	if (number_of_amulets(ent) == 3)
	{
		ent->client->pers.universe_quest_timer = level.time + 3000;
		ent->client->pers.universe_quest_messages = 199;
	}
}

// zyk: used by the quest_get_new_player function to actually get the new player based on his quest settings
void choose_new_player(gentity_t *next_player)
{
	int found = 0;
	if (next_player && next_player->client && next_player->client->sess.amrpgmode == 2 && !(next_player->client->pers.player_settings & (1 << 0)) && next_player->client->pers.can_play_quest == 0 && next_player->client->pers.connected == CON_CONNECTED && next_player->client->sess.sessionTeam != TEAM_SPECTATOR && next_player->inuse == qtrue)
	{
		if (level.quest_map == 1 && ((next_player->client->pers.defeated_guardians != NUMBER_OF_GUARDIANS && !(next_player->client->pers.defeated_guardians & (1 << 4))) || (next_player->client->pers.hunter_quest_progress != NUMBER_OF_OBJECTIVES && !(next_player->client->pers.hunter_quest_progress & (1 << 4))) || (next_player->client->pers.universe_quest_progress == 2 && (!(next_player->client->pers.universe_quest_counter & (1 << 0)) || !(next_player->client->pers.universe_quest_counter & (1 << 1)) || !(next_player->client->pers.universe_quest_counter & (1 << 2)) || !(next_player->client->pers.universe_quest_counter & (1 << 3)))) || next_player->client->pers.universe_quest_progress == 3 || (next_player->client->pers.universe_quest_progress == 8 && !(next_player->client->pers.universe_quest_counter & (1 << 0)))))
			found = 1;
		else if (level.quest_map == 2 && next_player->client->pers.hunter_quest_progress != NUMBER_OF_OBJECTIVES && !(next_player->client->pers.hunter_quest_progress & (1 << 5)))
			found = 1;
		else if (level.quest_map == 3 && next_player->client->pers.hunter_quest_progress != NUMBER_OF_OBJECTIVES && !(next_player->client->pers.hunter_quest_progress & (1 << 6)))
			found = 1;
		else if (level.quest_map == 4 && ((next_player->client->pers.hunter_quest_progress != NUMBER_OF_OBJECTIVES && !(next_player->client->pers.hunter_quest_progress & (1 << 7))) || (next_player->client->pers.universe_quest_progress == 9 && !(next_player->client->pers.universe_quest_counter & (1 << 2)))))
			found = 1;
		else if (level.quest_map == 5 && next_player->client->pers.hunter_quest_progress != NUMBER_OF_OBJECTIVES && !(next_player->client->pers.hunter_quest_progress & (1 << 8)))
			found = 1;
		else if (level.quest_map == 6 && ((next_player->client->pers.hunter_quest_progress != NUMBER_OF_OBJECTIVES && !(next_player->client->pers.hunter_quest_progress & (1 << 9))) || (next_player->client->pers.universe_quest_progress == 2 && !(next_player->client->pers.universe_quest_counter & (1 << 6)))))
			found = 1;
		else if (level.quest_map == 7 && ((next_player->client->pers.defeated_guardians != NUMBER_OF_GUARDIANS && !(next_player->client->pers.defeated_guardians & (1 << 7))) || (next_player->client->pers.hunter_quest_progress != NUMBER_OF_OBJECTIVES && !(next_player->client->pers.hunter_quest_progress & (1 << 10)))))
			found = 1;
		else if (level.quest_map == 8 && next_player->client->pers.universe_quest_progress == 4)
			found = 1;
		else if (level.quest_map == 9 && (next_player->client->pers.universe_quest_progress < 2 || (next_player->client->pers.hunter_quest_progress != NUMBER_OF_OBJECTIVES && !(next_player->client->pers.hunter_quest_progress & (1 << 12)))))
			found = 1;
		else if (level.quest_map == 10 && ((next_player->client->pers.defeated_guardians != NUMBER_OF_GUARDIANS && !(next_player->client->pers.defeated_guardians & (1 << 6))) || light_quest_defeated_guardians(next_player) == qtrue || dark_quest_collected_notes(next_player) == qtrue || next_player->client->pers.eternity_quest_progress < NUMBER_OF_ETERNITY_QUEST_OBJECTIVES || (next_player->client->pers.universe_quest_progress == 2 && !(next_player->client->pers.universe_quest_counter & (1 << 8)))))
			found = 1;
		else if (level.quest_map == 11 && next_player->client->pers.defeated_guardians != NUMBER_OF_GUARDIANS && !(next_player->client->pers.defeated_guardians & (1 << 9)))
			found = 1;
		else if (level.quest_map == 12 && (next_player->client->pers.universe_quest_progress == 7 || (next_player->client->pers.universe_quest_progress == 8 && !(next_player->client->pers.universe_quest_counter & (1 << 1)))))
			found = 1;
		else if (level.quest_map == 13 && ((next_player->client->pers.defeated_guardians != NUMBER_OF_GUARDIANS && !(next_player->client->pers.defeated_guardians & (1 << 5))) || (next_player->client->pers.universe_quest_progress == 2 && !(next_player->client->pers.universe_quest_counter & (1 << 5)))))
			found = 1;
		else if (level.quest_map == 14 && next_player->client->pers.defeated_guardians != NUMBER_OF_GUARDIANS && !(next_player->client->pers.defeated_guardians & (1 << 11)))
			found = 1;
		else if (level.quest_map == 15 && next_player->client->pers.defeated_guardians != NUMBER_OF_GUARDIANS && !(next_player->client->pers.defeated_guardians & (1 << 10)))
			found = 1;
		else if (level.quest_map == 17 && ((next_player->client->pers.universe_quest_progress == 8 && !(next_player->client->pers.universe_quest_counter & (1 << 2))) || (next_player->client->pers.universe_quest_progress >= 10 && next_player->client->pers.universe_quest_progress < NUMBER_OF_UNIVERSE_QUEST_OBJECTIVES)))
			found = 1;
		else if (level.quest_map == 18 && ((next_player->client->pers.hunter_quest_progress != NUMBER_OF_OBJECTIVES && !(next_player->client->pers.hunter_quest_progress & (1 << 11))) || (next_player->client->pers.universe_quest_progress == 2 && !(next_player->client->pers.universe_quest_counter & (1 << 4))) || (next_player->client->pers.universe_quest_progress == 9 && !(next_player->client->pers.universe_quest_counter & (1 << 0)))))
			found = 1;
		else if (level.quest_map == 20 && ((next_player->client->pers.defeated_guardians != NUMBER_OF_GUARDIANS && !(next_player->client->pers.defeated_guardians & (1 << 8))) || (next_player->client->pers.universe_quest_progress == 2 && !(next_player->client->pers.universe_quest_counter & (1 << 7)))))
			found = 1;
		else if (level.quest_map == 22 && next_player->client->pers.universe_quest_progress == 2 && !(next_player->client->pers.universe_quest_counter & (1 << 9)))
			found = 1;
		else if (level.quest_map == 24 && next_player->client->pers.universe_quest_progress == 5)
			found = 1;
		else if (level.quest_map == 25 && next_player->client->pers.universe_quest_progress == 6)
			found = 1;
		else if (level.quest_map == 26 && next_player->client->pers.universe_quest_progress == 9 && !(next_player->client->pers.universe_quest_counter & (1 << 1)))
			found = 1;
	}

	if (found == 1)
	{ // zyk: clean quest npcs of this map
		int j = 0;
		for (j = MAX_CLIENTS; j < level.num_entities; j++)
		{
			if (&g_entities[j] && g_entities[j].NPC && g_entities[j].health > 0 && (Q_stricmp( g_entities[j].NPC_type, "quest_ragnos" ) == 0 || Q_stricmp( g_entities[j].NPC_type, "quest_jawa" ) == 0 || Q_stricmp( g_entities[j].NPC_type, "quest_protocol_imp" ) == 0 || Q_stricmp( g_entities[j].NPC_type, "quest_sand_raider_green" ) == 0 || Q_stricmp( g_entities[j].NPC_type, "quest_sand_raider_brown" ) == 0 || Q_stricmp( g_entities[j].NPC_type, "quest_sand_raider_blue" ) == 0 || Q_stricmp( g_entities[j].NPC_type, "quest_sand_raider_red" ) == 0 || Q_stricmp( g_entities[j].NPC_type, "quest_reborn" ) == 0 || Q_stricmp( g_entities[j].NPC_type, "quest_reborn_blue" ) == 0 || Q_stricmp( g_entities[j].NPC_type, "quest_reborn_boss" ) == 0 || Q_stricmp( g_entities[j].NPC_type, "quest_reborn_red" ) == 0 || Q_stricmp( g_entities[j].NPC_type, "sage_of_light" ) == 0 || Q_stricmp( g_entities[j].NPC_type, "sage_of_darkness" ) == 0 || Q_stricmp( g_entities[j].NPC_type, "sage_of_eternity" ) == 0 || Q_stricmp( g_entities[j].NPC_type, "sage_of_universe" ) == 0 || Q_stricmp( g_entities[j].NPC_type, "quest_super_soldier" ) == 0 || Q_stricmp( g_entities[j].NPC_type, "guardian_of_time" ) == 0 || Q_stricmp( g_entities[j].NPC_type, "guardian_boss_9" ) == 0 || Q_stricmp( g_entities[j].NPC_type, "guardian_of_darkness" ) == 0 || Q_stricmp( g_entities[j].NPC_type, "guardian_of_eternity" ) == 0 || Q_stricmp( g_entities[j].NPC_type, "guardian_of_universe" ) == 0 || Q_stricmp( g_entities[j].NPC_type, "master_of_evil" ) == 0))
			{
				G_FreeEntity(&g_entities[j]);
			}
		}

		// zyk: setting the attributes depending on the quests this player must complete in this map
		next_player->client->pers.guardian_mode = 0;
		next_player->client->pers.guardian_timer = 0;

		next_player->client->pers.universe_quest_artifact_holder_id = -1;
		next_player->client->pers.universe_quest_messages = 0;

		// zyk: give some seconds to the new player to start his quest
		next_player->client->pers.universe_quest_timer = level.time + 3000;

		next_player->client->pers.hunter_quest_timer = level.time + 3000;
		next_player->client->pers.hunter_quest_messages = 0;

		next_player->client->pers.light_quest_timer = level.time + 3000;
		next_player->client->pers.light_quest_messages = 0;

		next_player->client->pers.eternity_quest_timer = 0;

		// zyk: must clean here too so in yavin1b the correct note is spawned for this player
		if (level.quest_map == 1)
			clean_note_model();

		if (level.quest_map == 9 && next_player->client->pers.universe_quest_progress == 0) 
		{ // zyk: first Universe Quest objective
			next_player->client->pers.universe_quest_objective_control = 18; // zyk: player must kill quest reborn npcs to complete the first objective
		}
		else if (level.quest_map == 9 && next_player->client->pers.universe_quest_progress == 1)
		{ // zyk: second Universe Quest objective
			next_player->client->pers.universe_quest_objective_control = 2; // zyk: sets this player as playing the second objective of Universe Quest
		}
		else if (next_player->client->pers.universe_quest_progress == 2)
		{ // zyk: third Universe Quest objective
			next_player->client->pers.universe_quest_objective_control = 3; // zyk: sets this player as playing the third objective of Universe Quest
		}
		else if (level.quest_map == 1 && next_player->client->pers.universe_quest_progress == 3)
		{
			if (level.quest_note_id == -1)
				load_note_model(2780,3966,1411);

			next_player->client->pers.universe_quest_objective_control = 4; // zyk: fourth Universe Quest objective
		}
		else if (level.quest_map == 8 && next_player->client->pers.universe_quest_progress == 4)
		{ // zyk: fifth Universe Quest objective
			next_player->client->pers.universe_quest_objective_control = 5;
			next_player->client->pers.universe_quest_timer = level.time + 2000;
		}
		else if (level.quest_map == 24 && next_player->client->pers.universe_quest_progress == 5)
		{
			next_player->client->pers.universe_quest_objective_control = -6;
			got_all_amulets(next_player);
		}
		else if (level.quest_map == 25 && next_player->client->pers.universe_quest_progress == 6)
		{ // zyk: seventh Universe Quest objective
			next_player->client->pers.universe_quest_timer = level.time + 3000;
			next_player->client->pers.universe_quest_objective_control = -7;
		}
		else if (level.quest_map == 17 && next_player->client->pers.universe_quest_progress == 11)
		{ // zyk: Universe Quest. Player must defeat this quantity of quest_super_soldier npcs in this mission
			next_player->client->pers.universe_quest_timer = level.time + 3000;
			next_player->client->pers.universe_quest_objective_control = 28;
		}

		// zyk: loading note models if player must find a Dark Quest note
		if (level.quest_note_id == -1 && next_player->client->pers.hunter_quest_progress != NUMBER_OF_OBJECTIVES)
		{
			if (level.quest_map == 1 && !(next_player->client->pers.hunter_quest_progress & (1 << 4)))
			{
				load_note_model(2375,4600,1810);
			}
			else if (level.quest_map == 2 && !(next_player->client->pers.hunter_quest_progress & (1 << 5)))
			{
				load_note_model(7500,-755,2);
			}
			else if (level.quest_map == 3 && !(next_player->client->pers.hunter_quest_progress & (1 << 6)))
			{
				load_note_model(-765,4790,196);
			}
			else if (level.quest_map == 4 && !(next_player->client->pers.hunter_quest_progress & (1 << 7)))
			{
				load_note_model(2400,2990,-2093);
			}
			else if (level.quest_map == 5 && !(next_player->client->pers.hunter_quest_progress & (1 << 8)))
			{
				load_note_model(-500,-4690,928);
			}
			else if (level.quest_map == 6 && !(next_player->client->pers.hunter_quest_progress & (1 << 9)))
			{
				load_note_model(-9838,-1547,2);
			}
			else if (level.quest_map == 7 && !(next_player->client->pers.hunter_quest_progress & (1 << 10)))
			{
				load_note_model(1905,1180,706);
			}
			else if (level.quest_map == 18 && !(next_player->client->pers.hunter_quest_progress & (1 << 11)))
			{
				load_note_model(-1148,-1458,593);
			}
			else if (level.quest_map == 9 && !(next_player->client->pers.hunter_quest_progress & (1 << 12)))
			{
				load_note_model(14100,-1580,-3165);
			}
		}

		// zyk: loading effects in guardian area
		if (level.quest_effect_id == -1 && next_player->client->pers.defeated_guardians != NUMBER_OF_GUARDIANS)
		{
			if (level.quest_map == 1 && !(next_player->client->pers.defeated_guardians & (1 << 4)))
			{ // zyk: Guardian of Water
				load_effect(2062,4089,351,0,"env/btend");
			}
			else if (level.quest_map == 13 && !(next_player->client->pers.defeated_guardians & (1 << 5)))
			{ // zyk: Guardian of Earth
				load_effect(-380,1578,4751,0,"env/btend");
			}
			else if (level.quest_map == 10 && !(next_player->client->pers.defeated_guardians & (1 << 6)))
			{ // zyk: Guardian of Forest
				load_effect(512,4829,62,0,"env/btend");
			}
			else if (level.quest_map == 7 && !(next_player->client->pers.defeated_guardians & (1 << 7)))
			{ // zyk: Guardian of Intelligence
				load_effect(2500,2140,-551,0,"env/btend");
			}
			else if (level.quest_map == 20 && !(next_player->client->pers.defeated_guardians & (1 << 8)))
			{ // zyk: Guardian of Agility
				load_effect(8474,-1322,-159,0,"env/btend");
			}
			else if (level.quest_map == 11 && !(next_player->client->pers.defeated_guardians & (1 << 9)))
			{ // zyk: Guardian of Fire
				load_effect(2742,-25,-3808,0,"env/btend");
			}
			else if (level.quest_map == 15 && !(next_player->client->pers.defeated_guardians & (1 << 10)))
			{ // zyk: Guardian of Wind
				load_effect(5225,462,665,0,"env/btend");
			}
			else if (level.quest_map == 14 && !(next_player->client->pers.defeated_guardians & (1 << 11)))
			{ // zyk: Guardian of Resistance
				load_effect(0,1135,9,0,"env/btend");
			}
		}

		next_player->client->pers.can_play_quest = 1;

		trap->SendServerCommand( -1, va("chat \"^3Quest System: ^7%s ^7turn.\"",next_player->client->pers.netname));
	}
}

// zyk: searches for a new player to play a quest if he died or failed
void quest_get_new_player(gentity_t *ent)
{
	int i = 0;
	gentity_t *next_player = NULL;

	ent->client->pers.can_play_quest = 0;

	for (i = 0; i < level.maxclients; i++)
	{ // zyk: verify if there is someone who is already playing a quest and is not in spectator mode
		next_player = &g_entities[i];
		if (next_player && next_player->client && next_player->client->sess.amrpgmode == 2 && next_player->client->pers.can_play_quest == 1 && next_player->client->sess.sessionTeam != TEAM_SPECTATOR)
			return;
	}

	for (i = 0; i < level.maxclients; i++)
	{ // zyk: remove guardian_mode from all players that were fighting a boss battle
		next_player = &g_entities[i];
		if (next_player && next_player->client && next_player->client->sess.amrpgmode == 2 && next_player->client->pers.guardian_mode != 0)
			next_player->client->pers.guardian_mode = 0;
	}

	// zyk: no one is already playing the quest, so choose a new player

	for (i = ((ent-g_entities) + 1); i < level.maxclients; i++)
	{
		next_player = &g_entities[i];

		choose_new_player(next_player);

		if (next_player && next_player->client && next_player->client->pers.can_play_quest == 1) // zyk: found the player
			return;
	}

	for (i = 0; i < (ent-g_entities); i++)
	{
		next_player = &g_entities[i];

		choose_new_player(next_player);

		if (next_player && next_player->client && next_player->client->pers.can_play_quest == 1) // zyk: found the player
			return;
	}

	// zyk: didnt find anyone to play this quest, so choose the same player again if he still needs to play a quest in this map
	choose_new_player(ent);
}

// zyk: tests if the race must be finished
void try_finishing_race()
{
	int j = 0, has_someone_racing = 0;
	gentity_t *this_ent = NULL;

	for (j = 0; j < MAX_CLIENTS; j++)
	{ 
		this_ent = &g_entities[j];
		if (this_ent && this_ent->client && this_ent->inuse && this_ent->health > 0 && this_ent->client->sess.sessionTeam != TEAM_SPECTATOR && this_ent->client->pers.race_position > 0)
		{ // zyk: searches for the players who are still racing to see if we must finish the race
			has_someone_racing = 1;
		}
	}

	if (has_someone_racing == 0)
	{ // zyk: no one is racing, so finish the race
		level.race_mode = 0;
		trap->SendServerCommand( -1, va("chat \"^3Race System: ^7The race is over!\""));
	}
}

/*
==================
Cmd_LogoutAccount_f
==================
*/
void Cmd_LogoutAccount_f( gentity_t *ent ) {
	int i = 0;

	if (ent->client->pers.being_mind_controlled != -1)
	{
		trap->SendServerCommand( ent-g_entities, "print \"You cant logout while being mind-controlled.\n\"" );
		return;
	}

	if (ent->client->sess.amrpgmode == 2 && ent->client->pers.mind_controlled1_id != -1)
	{
		trap->SendServerCommand( ent-g_entities, "print \"You cant logout while using Mind Control on someone.\n\"" );
		return;
	}

	// zyk: if player was fighting a guardian, allow other players to fight the guardian now
	if (ent->client->sess.amrpgmode == 2 && ent->client->pers.guardian_mode > 0)
	{
		clean_guardians(ent);
		ent->client->pers.guardian_mode = 0;
	}

	// zyk: saving the not logged player mode in session
	ent->client->sess.amrpgmode = 0;

	// zyk: if this player was playing a quest, find a new one to play quests in this map
	if (ent->client->pers.can_play_quest == 1)
	{
		quest_get_new_player(ent);
	}

	ent->client->pers.bitvalue = 0;

	// zyk: initializing mind control attributes used in RPG mode
	ent->client->pers.being_mind_controlled = -1;
	ent->client->pers.mind_control = 0;
	ent->client->pers.mind_controlled1_id = -1;

	// zyk: resetting the forcePowerMax to the cvar value
	ent->client->ps.fd.forcePowerMax = zyk_max_force_power.integer;

	// zyk: resetting max hp and shield to 100 in case the player has the Universe Power
	ent->client->ps.stats[STAT_MAX_HEALTH] = 100;

	if (ent->health > 100)
		ent->health = 100;

	if (ent->client->ps.stats[STAT_ARMOR] > 100)
		ent->client->ps.stats[STAT_ARMOR] = 100;

	// zyk: resetting force powers
	WP_InitForcePowers( ent );
			
	trap->SendServerCommand( ent-g_entities, "print \"Account logout finished succesfully.\n\"" );
}

/*
==================
Cmd_UpSkill_f
==================
*/
void Cmd_UpSkill_f( gentity_t *ent ) {
	char arg1[MAX_STRING_CHARS]; // zyk: value the user sends as an arg which is the skill to be upgraded
	int upgrade_value; // zyk: the integer value of arg1
		    
	if ( trap->Argc() != 2) 
	{ 
		trap->SendServerCommand( ent-g_entities, "print \"You must specify the number of the skill to be upgraded.\n\"" ); 
		return;
	}

	trap->Argv( 1, arg1, sizeof( arg1 ) );
	upgrade_value = atoi(arg1);

	// zyk: validation on the upgrade level, which must be in the range of valid skills.
	if (upgrade_value < 1 || upgrade_value > NUMBER_OF_SKILLS)
	{
		trap->SendServerCommand( ent-g_entities, "print \"Invalid skill number.\n\"" );
		return;
	}

	// zyk: the user must have skillpoints to get a new skill level
	if (ent->client->pers.skillpoints == 0)
	{
		trap->SendServerCommand( ent-g_entities, "print \"You dont have enough skillpoints.\n\"" );
		return;
	}

	// zyk: validation on skills that are allowed to specific RPG classes
	if (ent->client->pers.rpg_class == 1 && ((upgrade_value >= 20 && upgrade_value <= 29) || upgrade_value == 35 || (upgrade_value >= 40 && upgrade_value <= 54)))
	{
		trap->SendServerCommand( ent-g_entities, "print \"Force User class doesn't allow this skill.\n\"" );
		return;
	}

	if (ent->client->pers.rpg_class == 2 && ((upgrade_value >= 1 && upgrade_value <= 4) || (upgrade_value >= 6 && upgrade_value <= 18) || upgrade_value == 34 || (upgrade_value >= 36 && upgrade_value <= 39) || upgrade_value == 55))
	{
		trap->SendServerCommand( ent-g_entities, "print \"Bounty Hunter class doesn't allow this skill.\n\"" );
		return;
	}

	if (ent->client->pers.rpg_class == 3 && ((upgrade_value >= 1 && upgrade_value <= 4) || (upgrade_value >= 6 && upgrade_value <= 18) || upgrade_value == 34 || (upgrade_value >= 36 && upgrade_value <= 39) || upgrade_value == 49 || (upgrade_value >= 52 && upgrade_value <= 55)))
	{
		trap->SendServerCommand( ent-g_entities, "print \"Armored Soldier class doesn't allow this skill.\n\"" );
		return;
	}

	if (ent->client->pers.rpg_class == 4 && (upgrade_value == 4 || (upgrade_value >= 6 && upgrade_value <= 9) || upgrade_value == 11 || upgrade_value == 14 || upgrade_value == 17 || (upgrade_value >= 20 && upgrade_value <= 29) || upgrade_value == 35 || (upgrade_value >= 39 && upgrade_value <= 54)))
	{
		trap->SendServerCommand( ent-g_entities, "print \"Monk class doesn't allow this skill.\n\"" );
		return;
	}

	if (ent->client->pers.rpg_class == 5 && ((upgrade_value >= 1 && upgrade_value <= 4) || (upgrade_value >= 6 && upgrade_value <= 18) || (upgrade_value >= 20 && upgrade_value <= 21) || upgrade_value == 23 || (upgrade_value >= 26 && upgrade_value <= 27) || upgrade_value == 29 || upgrade_value == 34 || (upgrade_value >= 36 && upgrade_value <= 40) || (upgrade_value >= 43 && upgrade_value <= 44) || (upgrade_value >= 48 && upgrade_value <= 53) || upgrade_value == 55))
	{
		trap->SendServerCommand( ent-g_entities, "print \"Stealth Attacker class doesn't allow this skill.\n\"" );
		return;
	}

	if (ent->client->pers.rpg_class == 6 && ((upgrade_value >= 12 && upgrade_value <= 13) || (upgrade_value >= 17 && upgrade_value <= 18) || (upgrade_value >= 20 && upgrade_value <= 29) || upgrade_value == 35 || (upgrade_value >= 38 && upgrade_value <= 54)))
	{
		trap->SendServerCommand( ent-g_entities, "print \"Duelist class doesn't allow this skill.\n\"" );
		return;
	}

	// zyk: validation on skills that require certain conditions to be upgraded
	if (upgrade_value == 20 && ent->client->pers.weapons_levels[0] == 1 && !(ent->client->pers.secrets_found & (1 << 12)))
	{
		trap->SendServerCommand( ent-g_entities, "print \"You must buy the Blaster Pack Weapons Upgrade to get 2/2 in Blaster Pistol.\n\"" );
		return;
	}

	if (upgrade_value == 21 && ent->client->pers.weapons_levels[1] == 1 && !(ent->client->pers.secrets_found & (1 << 12)))
	{
		trap->SendServerCommand( ent-g_entities, "print \"You must buy the Blaster Pack Weapons Upgrade to get 2/2 in E11 Blaster Rifle.\n\"" );
		return;
	}

	if (upgrade_value == 22 && ent->client->pers.weapons_levels[2] == 1 && !(ent->client->pers.secrets_found & (1 << 11)))
	{
		trap->SendServerCommand( ent-g_entities, "print \"You must buy the Power Cell Weapons Upgrade to get 2/2 in Disruptor.\n\"" );
		return;
	}

	if (upgrade_value == 23 && ent->client->pers.weapons_levels[3] == 1 && !(ent->client->pers.secrets_found & (1 << 11)))
	{
		trap->SendServerCommand( ent-g_entities, "print \"You must buy the Power Cell Weapons Upgrade to get 2/2 in Bowcaster.\n\"" );
		return;
	}

	if (upgrade_value == 24 && ent->client->pers.weapons_levels[4] == 1 && !(ent->client->pers.secrets_found & (1 << 13)))
	{
		trap->SendServerCommand( ent-g_entities, "print \"You must buy the Metal Bolts Weapons Upgrade to get 2/2 in Repeater.\n\"" );
		return;
	}

	if (upgrade_value == 25 && ent->client->pers.weapons_levels[5] == 1 && !(ent->client->pers.secrets_found & (1 << 11)))
	{
		trap->SendServerCommand( ent-g_entities, "print \"You must buy the Power Cell Weapons Upgrade to get 2/2 in DEMP2.\n\"" );
		return;
	}

	if (upgrade_value == 26 && ent->client->pers.weapons_levels[6] == 1 && !(ent->client->pers.secrets_found & (1 << 13)))
	{
		trap->SendServerCommand( ent-g_entities, "print \"You must buy the Metal Bolts Weapons Upgrade to get 2/2 in Flechette.\n\"" );
		return;
	}

	if (upgrade_value == 27 && ent->client->pers.weapons_levels[7] == 1 && !(ent->client->pers.secrets_found & (1 << 14)))
	{
		trap->SendServerCommand( ent-g_entities, "print \"You must buy the Rocket Upgrade to get 2/2 in Rocket Launcher.\n\"" );
		return;
	}

	if (upgrade_value == 28 && ent->client->pers.weapons_levels[8] == 1 && !(ent->client->pers.secrets_found & (1 << 13)))
	{
		trap->SendServerCommand( ent-g_entities, "print \"You must buy the Metal Bolts Weapons Upgrade to get 2/2 in Concussion Rifle.\n\"" );
		return;
	}

	if (upgrade_value == 29 && ent->client->pers.weapons_levels[9] == 1 && !(ent->client->pers.secrets_found & (1 << 12)))
	{
		trap->SendServerCommand( ent-g_entities, "print \"You must buy the Blaster Pack Weapons Upgrade to get 2/2 in Bryar Pistol.\n\"" );
		return;
	}

	// zyk: the upgrade is done if it doesnt go above the maximum level of the skill
	if (upgrade_value == 1)
	{
		if (ent->client->pers.force_powers_levels[0] < 5)
		{
			if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_LEVITATION)))
				ent->client->ps.fd.forcePowersKnown |= (1 << FP_LEVITATION);
			ent->client->pers.force_powers_levels[0]++;
			ent->client->ps.fd.forcePowerLevel[FP_LEVITATION] = ent->client->pers.force_powers_levels[0];
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Jump ^7skill.\n\"" );
			return;
		}
	}
			
	if (upgrade_value == 2)
	{
		if (ent->client->pers.force_powers_levels[1] < 3)
		{
			if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_PUSH)))
				ent->client->ps.fd.forcePowersKnown |= (1 << FP_PUSH);
			ent->client->pers.force_powers_levels[1]++;
			ent->client->ps.fd.forcePowerLevel[FP_PUSH] = ent->client->pers.force_powers_levels[1];
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Push ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 3)
	{
		if (ent->client->pers.force_powers_levels[2] < 3)
		{
			if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_PULL)))
				ent->client->ps.fd.forcePowersKnown |= (1 << FP_PULL);
			ent->client->pers.force_powers_levels[2]++;
			ent->client->ps.fd.forcePowerLevel[FP_PULL] = ent->client->pers.force_powers_levels[2];
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Pull ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 4)
	{
		if (ent->client->pers.force_powers_levels[3] < 4)
		{
			if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_SPEED)))
				ent->client->ps.fd.forcePowersKnown |= (1 << FP_SPEED);
			ent->client->pers.force_powers_levels[3]++;
			ent->client->ps.fd.forcePowerLevel[FP_SPEED] = ent->client->pers.force_powers_levels[3];
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Speed ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 5)
	{
		if (ent->client->pers.force_powers_levels[4] < 3)
		{
			if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_SEE)))
				ent->client->ps.fd.forcePowersKnown |= (1 << FP_SEE);
			ent->client->pers.force_powers_levels[4]++;
			ent->client->ps.fd.forcePowerLevel[FP_SEE] = ent->client->pers.force_powers_levels[4];
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Sense ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 6)
	{
		if (ent->client->pers.force_powers_levels[5] < 5)
		{
			if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_SABER_OFFENSE)))
			{
				ent->client->ps.fd.forcePowersKnown |= (1 << FP_SABER_OFFENSE);
				ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_SABER);
			}
			ent->client->pers.force_powers_levels[5]++;
			ent->client->ps.fd.forcePowerLevel[FP_SABER_OFFENSE] = ent->client->pers.force_powers_levels[5];
			if (ent->client->saber[0].type == SABER_SINGLE && ent->client->saber[1].type == SABER_NONE)
			{
				ent->client->ps.fd.saberAnimLevel = ent->client->pers.force_powers_levels[5];
			}
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Saber Attack ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 7)
	{
		if (ent->client->pers.force_powers_levels[6] < 3)
		{
			if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_SABER_DEFENSE)))
				ent->client->ps.fd.forcePowersKnown |= (1 << FP_SABER_DEFENSE);
			ent->client->pers.force_powers_levels[6]++;
			ent->client->ps.fd.forcePowerLevel[FP_SABER_DEFENSE] = ent->client->pers.force_powers_levels[6];
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Saber Defense ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 8)
	{
		if (ent->client->pers.force_powers_levels[7] < 3)
		{
			if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_SABERTHROW)))
				ent->client->ps.fd.forcePowersKnown |= (1 << FP_SABERTHROW);
			ent->client->pers.force_powers_levels[7]++;
			ent->client->ps.fd.forcePowerLevel[FP_SABERTHROW] = ent->client->pers.force_powers_levels[7];
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Saber Throw ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 9)
	{
		if (ent->client->pers.force_powers_levels[8] < 4)
		{
			if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_ABSORB)))
				ent->client->ps.fd.forcePowersKnown |= (1 << FP_ABSORB);
			ent->client->pers.force_powers_levels[8]++;

			if (ent->client->pers.force_powers_levels[8] < 4)
				ent->client->ps.fd.forcePowerLevel[FP_ABSORB] = ent->client->pers.force_powers_levels[8];
			else
				ent->client->ps.fd.forcePowerLevel[FP_ABSORB] = FORCE_LEVEL_3;

			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Absorb ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 10)
	{
		if (ent->client->pers.force_powers_levels[9] < 3)
		{
			if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_HEAL)))
				ent->client->ps.fd.forcePowersKnown |= (1 << FP_HEAL);
			ent->client->pers.force_powers_levels[9]++;
			ent->client->ps.fd.forcePowerLevel[FP_HEAL] = ent->client->pers.force_powers_levels[9];
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Heal ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 11)
	{
		if (ent->client->pers.force_powers_levels[10] < 3)
		{
			if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_PROTECT)))
				ent->client->ps.fd.forcePowersKnown |= (1 << FP_PROTECT);
			ent->client->pers.force_powers_levels[10]++;
			ent->client->ps.fd.forcePowerLevel[FP_PROTECT] = ent->client->pers.force_powers_levels[10];
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Protect ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 12)
	{
		if (ent->client->pers.force_powers_levels[11] < 3)
		{
			if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_TELEPATHY)))
				ent->client->ps.fd.forcePowersKnown |= (1 << FP_TELEPATHY);
			ent->client->pers.force_powers_levels[11]++;
			ent->client->ps.fd.forcePowerLevel[FP_TELEPATHY] = ent->client->pers.force_powers_levels[11];
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Mind Trick ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 13)
	{
		if (ent->client->pers.force_powers_levels[12] < 3)
		{
			if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_TEAM_HEAL)))
				ent->client->ps.fd.forcePowersKnown |= (1 << FP_TEAM_HEAL);
			ent->client->pers.force_powers_levels[12]++;
			ent->client->ps.fd.forcePowerLevel[FP_TEAM_HEAL] = ent->client->pers.force_powers_levels[12];
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Team Heal ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 14)
	{
		if (ent->client->pers.force_powers_levels[13] < 3)
		{
			if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_LIGHTNING)))
				ent->client->ps.fd.forcePowersKnown |= (1 << FP_LIGHTNING);
			ent->client->pers.force_powers_levels[13]++;
			ent->client->ps.fd.forcePowerLevel[FP_LIGHTNING] = ent->client->pers.force_powers_levels[13];
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Lightning ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 15)
	{
		if (ent->client->pers.force_powers_levels[14] < 3)
		{
			if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_GRIP)))
				ent->client->ps.fd.forcePowersKnown |= (1 << FP_GRIP);
			ent->client->pers.force_powers_levels[14]++;
			ent->client->ps.fd.forcePowerLevel[FP_GRIP] = ent->client->pers.force_powers_levels[14];
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Grip ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 16)
	{
		if (ent->client->pers.force_powers_levels[15] < 3)
		{
			if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_DRAIN)))
				ent->client->ps.fd.forcePowersKnown |= (1 << FP_DRAIN);
			ent->client->pers.force_powers_levels[15]++;
			ent->client->ps.fd.forcePowerLevel[FP_DRAIN] = ent->client->pers.force_powers_levels[15];
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Drain ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 17)
	{
		if (ent->client->pers.force_powers_levels[16] < 4)
		{
			if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_RAGE)))
				ent->client->ps.fd.forcePowersKnown |= (1 << FP_RAGE);
			ent->client->pers.force_powers_levels[16]++;

			if (ent->client->pers.force_powers_levels[16] < 4)
				ent->client->ps.fd.forcePowerLevel[FP_RAGE] = ent->client->pers.force_powers_levels[16];
			else
				ent->client->ps.fd.forcePowerLevel[FP_RAGE] = FORCE_LEVEL_3;

			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Rage ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 18)
	{
		if (ent->client->pers.force_powers_levels[17] < 3)
		{
			if (!(ent->client->ps.fd.forcePowersKnown & (1 << FP_TEAM_FORCE)))
				ent->client->ps.fd.forcePowersKnown |= (1 << FP_TEAM_FORCE);
			ent->client->pers.force_powers_levels[17]++;
			ent->client->ps.fd.forcePowerLevel[FP_TEAM_FORCE] = ent->client->pers.force_powers_levels[17];
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Team Energize ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 19)
	{
		if (ent->client->pers.stun_baton_level < 3)
		{
			ent->client->pers.stun_baton_level++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Stun Baton ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 20)
	{
		if (ent->client->pers.weapons_levels[0] < 2)
		{
			ent->client->pers.weapons_levels[0]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Blaster Pistol ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 21)
	{
		if (ent->client->pers.weapons_levels[1] < 2)
		{
			ent->client->pers.weapons_levels[1]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3E11 Blaster Rifle ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 22)
	{
		if (ent->client->pers.weapons_levels[2] < 2)
		{
			ent->client->pers.weapons_levels[2]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Disruptor ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 23)
	{
		if (ent->client->pers.weapons_levels[3] < 2)
		{
			ent->client->pers.weapons_levels[3]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Bowcaster ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 24)
	{
		if (ent->client->pers.weapons_levels[4] < 2)
		{
			ent->client->pers.weapons_levels[4]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Repeater ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 25)
	{
		if (ent->client->pers.weapons_levels[5] < 2)
		{
			ent->client->pers.weapons_levels[5]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3DEMP2 ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 26)
	{
		if (ent->client->pers.weapons_levels[6] < 2)
		{
			ent->client->pers.weapons_levels[6]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Flechette ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 27)
	{
		if (ent->client->pers.weapons_levels[7] < 2)
		{
			ent->client->pers.weapons_levels[7]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Rocket Launcher ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 28)
	{
		if (ent->client->pers.weapons_levels[8] < 2)
		{
			ent->client->pers.weapons_levels[8]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Concussion Rifle ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 29)
	{
		if (ent->client->pers.weapons_levels[9] < 2)
		{
			ent->client->pers.weapons_levels[9]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Bryar Pistol ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 30)
	{
		if (ent->client->pers.melee_level < 3)
		{
			ent->client->pers.melee_level++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Melee ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 31)
	{
		if (ent->client->pers.starting_shield_level < 5)
		{
			ent->client->pers.starting_shield_level++;
			set_max_shield(ent);
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Max Shield ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 32)
	{
		if (ent->client->pers.shield_strength < 4)
		{
			ent->client->pers.shield_strength++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Shield Strength ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 33)
	{
		if (ent->client->pers.health_strength < 4)
		{
			ent->client->pers.health_strength++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Health Strength ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 34)
	{
		if (ent->client->pers.drain_shield < 1)
		{
			ent->client->pers.drain_shield++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Drain Shield ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 35)
	{
		if (ent->client->pers.jetpack_level < 3)
		{
			ent->client->pers.jetpack_level++;
			if (!(ent->client->ps.stats[STAT_HOLDABLE_ITEMS] & (1 << HI_JETPACK)))
				ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_JETPACK);
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Jetpack ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 36)
	{
		if (ent->client->pers.playerhealth < 3)
		{
			ent->client->pers.playerhealth++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Sense Health ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 37)
	{
		if (ent->client->pers.shield < 3)
		{
			ent->client->pers.shield++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Shield Heal ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 38)
	{
		if (ent->client->pers.teamshield < 3)
		{
			ent->client->pers.teamshield++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Team Shield Heal ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 39)
	{
		if (ent->client->pers.mind_control < 1)
		{
			ent->client->pers.mind_control++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Mind Control ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 40)
	{
		if (ent->client->pers.ammo_levels[0] < 3)
		{
			ent->client->pers.ammo_levels[0]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Blaster Pack ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 41)
	{
		if (ent->client->pers.ammo_levels[1] < 3)
		{
			ent->client->pers.ammo_levels[1]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Power Cell ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 42)
	{
		if (ent->client->pers.ammo_levels[2] < 3)
		{
			ent->client->pers.ammo_levels[2]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Metallic Bolts ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 43)
	{
		if (ent->client->pers.ammo_levels[3] < 3)
		{
			ent->client->pers.ammo_levels[3]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Rockets ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 44)
	{
		if (ent->client->pers.ammo_levels[4] < 3)
		{
			ent->client->pers.ammo_levels[4]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Thermals ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 45)
	{
		if (ent->client->pers.ammo_levels[5] < 3)
		{
			ent->client->pers.ammo_levels[5]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Trip Mines ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 46)
	{
		if (ent->client->pers.ammo_levels[6] < 3)
		{
			ent->client->pers.ammo_levels[6]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Det Packs ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 47)
	{
		if (ent->client->pers.holdable_items_levels[0] < 1)
		{
			ent->client->pers.holdable_items_levels[0]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Binoculars ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 48)
	{
		if (ent->client->pers.holdable_items_levels[1] < 2)
		{
			ent->client->pers.holdable_items_levels[1]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Bacta Canister ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 49)
	{
		if (ent->client->pers.holdable_items_levels[2] < 1)
		{
			ent->client->pers.holdable_items_levels[2]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Sentry Gun ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 50)
	{
		if (ent->client->pers.holdable_items_levels[3] < 1)
		{
			ent->client->pers.holdable_items_levels[3]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Seeker Drone ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 51)
	{
		if (ent->client->pers.holdable_items_levels[4] < 1)
		{
			ent->client->pers.holdable_items_levels[4]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3E-Web ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 52)
	{
		if (ent->client->pers.holdable_items_levels[5] < 2)
		{
			ent->client->pers.holdable_items_levels[5]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Big Bacta ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 53)
	{
		if (ent->client->pers.holdable_items_levels[6] < 2)
		{
			ent->client->pers.holdable_items_levels[6]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Force Field ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 54)
	{
		if (ent->client->pers.holdable_items_levels[7] < 2)
		{
			ent->client->pers.holdable_items_levels[7]++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Cloak Item ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 55)
	{
		if (ent->client->pers.max_force_power_level < 5)
		{
			ent->client->pers.max_force_power_level++;
			ent->client->pers.max_force_power = (int)ceil((zyk_max_force_power.value/4.0) * ent->client->pers.max_force_power_level);
			ent->client->ps.fd.forcePowerMax = ent->client->pers.max_force_power;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Force Power ^7skill.\n\"" );
			return;
		}
	}

	if (upgrade_value == 56)
	{
		if (ent->client->pers.improvements_level < 3)
		{
			ent->client->pers.improvements_level++;
			ent->client->pers.skillpoints--;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the maximum level of ^3Improvements ^7skill.\n\"" );
			return;
		}
	}

	// zyk: saving the account file with the upgraded skill
	save_account(ent);

	trap->SendServerCommand( ent-g_entities, "print \"Skill upgraded successfully.\n\"" );
}

/*
==================
Cmd_DownSkill_f
==================
*/
void Cmd_DownSkill_f( gentity_t *ent ) {
	char arg1[MAX_STRING_CHARS]; // zyk: value the user sends as an arg which is the skill to be downgraded
	int downgrade_value; // zyk: the integer value of arg1

	if ( trap->Argc() != 2) 
	{ 
		trap->SendServerCommand( ent-g_entities, "print \"You must specify the number of the skill to be downgraded.\n\"" ); 
		return;
	}

	trap->Argv( 1, arg1, sizeof( arg1 ) );
	downgrade_value = atoi(arg1);

	// zyk: validation on the downgrade level, which must be in the range of valid skills.
	if (downgrade_value < 1 || downgrade_value > NUMBER_OF_SKILLS)
	{
		trap->SendServerCommand( ent-g_entities, "print \"Invalid skill number.\n\"" );
		return;
	}

	// zyk: the downgrade is done if it doesnt go below the minimum level of the skill
	if (downgrade_value == 1)
	{
		if (ent->client->pers.force_powers_levels[0] > 0)
		{
			ent->client->pers.force_powers_levels[0]--;
			ent->client->ps.fd.forcePowerLevel[FP_LEVITATION] = ent->client->pers.force_powers_levels[0];
			if (ent->client->ps.fd.forcePowerLevel[FP_LEVITATION] == 0)
				ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_LEVITATION);
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Jump ^7skill.\n\"" );
			return;
		}
	}
			
	if (downgrade_value == 2)
	{
		if (ent->client->pers.force_powers_levels[1] > 0)
		{
			ent->client->pers.force_powers_levels[1]--;
			ent->client->ps.fd.forcePowerLevel[FP_PUSH] = ent->client->pers.force_powers_levels[1];
			if (ent->client->ps.fd.forcePowerLevel[FP_PUSH] == 0)
				ent->client->ps.fd.forcePowersKnown  &= ~(1 << FP_PUSH);
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Push ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 3)
	{
		if (ent->client->pers.force_powers_levels[2] > 0)
		{
			ent->client->pers.force_powers_levels[2]--;
			ent->client->ps.fd.forcePowerLevel[FP_PULL] = ent->client->pers.force_powers_levels[2];
			if (ent->client->ps.fd.forcePowerLevel[FP_PULL] == 0)
				ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_PULL);
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Pull ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 4)
	{
		if (ent->client->pers.force_powers_levels[3] > 0)
		{
			ent->client->pers.force_powers_levels[3]--;
			ent->client->ps.fd.forcePowerLevel[FP_SPEED] = ent->client->pers.force_powers_levels[3];
			if (ent->client->ps.fd.forcePowerLevel[FP_SPEED] == 0)
				ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_SPEED);
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Speed ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 5)
	{
		if (ent->client->pers.force_powers_levels[4] > 0)
		{
			ent->client->pers.force_powers_levels[4]--;
			ent->client->ps.fd.forcePowerLevel[FP_SEE] = ent->client->pers.force_powers_levels[4];
			if (ent->client->ps.fd.forcePowerLevel[FP_SEE] == 0)
				ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_SEE);
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Sense ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 6)
	{
		if (ent->client->pers.force_powers_levels[5] > 0)
		{
			ent->client->pers.force_powers_levels[5]--;
			ent->client->ps.fd.forcePowerLevel[FP_SABER_OFFENSE] = ent->client->pers.force_powers_levels[5];
			if (ent->client->saber[0].model[0] && !ent->client->saber[1].model[0])
			{
				ent->client->ps.fd.saberAnimLevelBase = ent->client->ps.fd.saberAnimLevel = ent->client->ps.fd.saberDrawAnimLevel = ent->client->sess.saberLevel = ent->client->pers.force_powers_levels[5];
				ent->client->ps.fd.saberAnimLevel = ent->client->pers.force_powers_levels[5];
			}
			if (ent->client->ps.fd.forcePowerLevel[FP_SABER_OFFENSE] == 0)
			{
				ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_SABER_OFFENSE);
				ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_SABER);
				ent->client->ps.weapon = WP_MELEE;
			}
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Saber Attack ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 7)
	{
		if (ent->client->pers.force_powers_levels[6] > 0)
		{
			ent->client->pers.force_powers_levels[6]--;
			ent->client->ps.fd.forcePowerLevel[FP_SABER_DEFENSE] = ent->client->pers.force_powers_levels[6];
			if (ent->client->ps.fd.forcePowerLevel[FP_SABER_DEFENSE] == 0)
				ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_SABER_DEFENSE);
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Saber Defense ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 8)
	{
		if (ent->client->pers.force_powers_levels[7] > 0)
		{
			ent->client->pers.force_powers_levels[7]--;
			ent->client->ps.fd.forcePowerLevel[FP_SABERTHROW] = ent->client->pers.force_powers_levels[7];
			if (ent->client->ps.fd.forcePowerLevel[FP_SABERTHROW] == 0)
				ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_SABERTHROW);
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Saber Throw ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 9)
	{
		if (ent->client->pers.force_powers_levels[8] > 0)
		{
			ent->client->pers.force_powers_levels[8]--;
			ent->client->ps.fd.forcePowerLevel[FP_ABSORB] = ent->client->pers.force_powers_levels[8];
			if (ent->client->ps.fd.forcePowerLevel[FP_ABSORB] == 0)
				ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_ABSORB);
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Absorb ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 10)
	{
		if (ent->client->pers.force_powers_levels[9] > 0)
		{
			ent->client->pers.force_powers_levels[9]--;
			ent->client->ps.fd.forcePowerLevel[FP_HEAL] = ent->client->pers.force_powers_levels[9];
			if (ent->client->ps.fd.forcePowerLevel[FP_HEAL] == 0)
				ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_HEAL);
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Heal ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 11)
	{
		if (ent->client->pers.force_powers_levels[10] > 0)
		{
			ent->client->pers.force_powers_levels[10]--;
			ent->client->ps.fd.forcePowerLevel[FP_PROTECT] = ent->client->pers.force_powers_levels[10];
			if (ent->client->ps.fd.forcePowerLevel[FP_PROTECT] == 0)
				ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_PROTECT);
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Protect ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 12)
	{
		if (ent->client->pers.force_powers_levels[11] > 0)
		{
			ent->client->pers.force_powers_levels[11]--;
			ent->client->ps.fd.forcePowerLevel[FP_TELEPATHY] = ent->client->pers.force_powers_levels[11];
			if (ent->client->ps.fd.forcePowerLevel[FP_TELEPATHY] == 0)
				ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_TELEPATHY);
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Mind Trick ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 13)
	{
		if (ent->client->pers.force_powers_levels[12] > 0)
		{
			ent->client->pers.force_powers_levels[12]--;
			ent->client->ps.fd.forcePowerLevel[FP_TEAM_HEAL] = ent->client->pers.force_powers_levels[12];
			if (ent->client->ps.fd.forcePowerLevel[FP_TEAM_HEAL] == 0)
				ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_TEAM_HEAL);
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Team Heal ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 14)
	{
		if (ent->client->pers.force_powers_levels[13] > 0)
		{
			ent->client->pers.force_powers_levels[13]--;
			ent->client->ps.fd.forcePowerLevel[FP_LIGHTNING] = ent->client->pers.force_powers_levels[13];
			if (ent->client->ps.fd.forcePowerLevel[FP_LIGHTNING] == 0)
				ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_LIGHTNING);
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Lightning ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 15)
	{
		if (ent->client->pers.force_powers_levels[14] > 0)
		{
			ent->client->pers.force_powers_levels[14]--;
			ent->client->ps.fd.forcePowerLevel[FP_GRIP] = ent->client->pers.force_powers_levels[14];
			if (ent->client->ps.fd.forcePowerLevel[FP_GRIP] == 0)
				ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_GRIP);
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Grip ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 16)
	{
		if (ent->client->pers.force_powers_levels[15] > 0)
		{
			ent->client->pers.force_powers_levels[15]--;
			ent->client->ps.fd.forcePowerLevel[FP_DRAIN] = ent->client->pers.force_powers_levels[15];
			if (ent->client->ps.fd.forcePowerLevel[FP_DRAIN] == 0)
				ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_DRAIN);
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Drain ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 17)
	{
		if (ent->client->pers.force_powers_levels[16] > 0)
		{
			ent->client->pers.force_powers_levels[16]--;
			ent->client->ps.fd.forcePowerLevel[FP_RAGE] = ent->client->pers.force_powers_levels[16];
			if (ent->client->ps.fd.forcePowerLevel[FP_RAGE] == 0)
				ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_RAGE);
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Rage ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 18)
	{
		if (ent->client->pers.force_powers_levels[17] > 0)
		{
			ent->client->pers.force_powers_levels[17]--;
			ent->client->ps.fd.forcePowerLevel[FP_TEAM_FORCE] = ent->client->pers.force_powers_levels[17];
			if (ent->client->ps.fd.forcePowerLevel[FP_TEAM_FORCE] == 0)
				ent->client->ps.fd.forcePowersKnown &= ~(1 << FP_TEAM_FORCE);
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Team Energize ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 19)
	{
		if (ent->client->pers.stun_baton_level > 0)
		{
			ent->client->pers.stun_baton_level--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Stun Baton ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 20)
	{
		if (ent->client->pers.weapons_levels[0] > 0)
		{
			ent->client->pers.weapons_levels[0]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Blaster Pistol ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 21)
	{
		if (ent->client->pers.weapons_levels[1] > 0)
		{
			ent->client->pers.weapons_levels[1]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3E11 Blaster Rifle ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 22)
	{
		if (ent->client->pers.weapons_levels[2] > 0)
		{
			ent->client->pers.weapons_levels[2]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Disruptor ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 23)
	{
		if (ent->client->pers.weapons_levels[3] > 0)
		{
			ent->client->pers.weapons_levels[3]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Bowcaster ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 24)
	{
		if (ent->client->pers.weapons_levels[4] > 0)
		{
			ent->client->pers.weapons_levels[4]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Repeater ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 25)
	{
		if (ent->client->pers.weapons_levels[5] > 0)
		{
			ent->client->pers.weapons_levels[5]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3DEMP2 ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 26)
	{
		if (ent->client->pers.weapons_levels[6] > 0)
		{
			ent->client->pers.weapons_levels[6]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Flechette ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 27)
	{
		if (ent->client->pers.weapons_levels[7] > 0)
		{
			ent->client->pers.weapons_levels[7]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Rocket Launcher ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 28)
	{
		if (ent->client->pers.weapons_levels[8] > 0)
		{
			ent->client->pers.weapons_levels[8]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Concussion Rifle ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 29)
	{
		if (ent->client->pers.weapons_levels[9] > 0)
		{
			ent->client->pers.weapons_levels[9]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Bryar Pistol ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 30)
	{
		if (ent->client->pers.melee_level > 0)
		{
			ent->client->pers.melee_level--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Melee ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 31)
	{
		if (ent->client->pers.starting_shield_level > 0)
		{
			ent->client->pers.starting_shield_level--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Max Shield ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 32)
	{
		if (ent->client->pers.shield_strength > 0)
		{
			ent->client->pers.shield_strength--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Shield Strength ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 33)
	{
		if (ent->client->pers.health_strength > 0)
		{
			ent->client->pers.health_strength--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Health Strength ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 34)
	{
		if (ent->client->pers.drain_shield > 0)
		{
			ent->client->pers.drain_shield--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Drain Shield ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 35)
	{
		if (ent->client->pers.jetpack_level > 0)
		{
			ent->client->pers.jetpack_level--;
			if (ent->client->pers.jetpack_level == 0)
			{
				ent->client->ps.stats[STAT_HOLDABLE_ITEMS] &= ~(1 << HI_JETPACK);
				if (ent->client->jetPackOn)
				{
					Jetpack_Off(ent);
				}
			}
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Jetpack ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 36)
	{
		if (ent->client->pers.playerhealth > 0)
		{
			ent->client->pers.playerhealth--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Sense Health ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 37)
	{
		if (ent->client->pers.shield > 0)
		{
			ent->client->pers.shield--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Shield Heal ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 38)
	{
		if (ent->client->pers.teamshield > 0)
		{
			ent->client->pers.teamshield--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Team Shield Heal ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 39)
	{
		if (ent->client->pers.mind_control > 0)
		{
			ent->client->pers.mind_control--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Mind Control ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 40)
	{
		if (ent->client->pers.ammo_levels[0] > 0)
		{
			ent->client->pers.ammo_levels[0]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Blaster Pack ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 41)
	{
		if (ent->client->pers.ammo_levels[1] > 0)
		{
			ent->client->pers.ammo_levels[1]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Power Cell ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 42)
	{
		if (ent->client->pers.ammo_levels[2] > 0)
		{
			ent->client->pers.ammo_levels[2]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Metallic Bolts ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 43)
	{
		if (ent->client->pers.ammo_levels[3] > 0)
		{
			ent->client->pers.ammo_levels[3]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Rockets ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 44)
	{
		if (ent->client->pers.ammo_levels[4] > 0)
		{
			ent->client->pers.ammo_levels[4]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Thermals ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 45)
	{
		if (ent->client->pers.ammo_levels[5] > 0)
		{
			ent->client->pers.ammo_levels[5]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Trip Mines ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 46)
	{
		if (ent->client->pers.ammo_levels[6] > 0)
		{
			ent->client->pers.ammo_levels[6]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Det Packs ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 47)
	{
		if (ent->client->pers.holdable_items_levels[0] > 0)
		{
			ent->client->pers.holdable_items_levels[0]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Binoculars ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 48)
	{
		if (ent->client->pers.holdable_items_levels[1] > 0)
		{
			ent->client->pers.holdable_items_levels[1]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Bacta Canister ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 49)
	{
		if (ent->client->pers.holdable_items_levels[2] > 0)
		{
			ent->client->pers.holdable_items_levels[2]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Sentry Gun ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 50)
	{
		if (ent->client->pers.holdable_items_levels[3] > 0)
		{
			ent->client->pers.holdable_items_levels[3]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Seeker Drone ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 51)
	{
		if (ent->client->pers.holdable_items_levels[4] > 0)
		{
			ent->client->pers.holdable_items_levels[4]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3E-Web ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 52)
	{
		if (ent->client->pers.holdable_items_levels[5] > 0)
		{
			ent->client->pers.holdable_items_levels[5]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Big Bacta ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 53)
	{
		if (ent->client->pers.holdable_items_levels[6] > 0)
		{
			ent->client->pers.holdable_items_levels[6]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Force Field ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 54)
	{
		if (ent->client->pers.holdable_items_levels[7] > 0)
		{
			ent->client->pers.holdable_items_levels[7]--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Cloak Item ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 55)
	{
		if (ent->client->pers.max_force_power_level > 0)
		{
			ent->client->pers.max_force_power_level--;
			ent->client->pers.max_force_power = (int)ceil((zyk_max_force_power.value/4.0) * ent->client->pers.max_force_power_level);
			ent->client->ps.fd.forcePowerMax = ent->client->pers.max_force_power;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Force Power ^7skill.\n\"" );
			return;
		}
	}

	if (downgrade_value == 56)
	{
		if (ent->client->pers.improvements_level > 0)
		{
			ent->client->pers.improvements_level--;
			ent->client->pers.skillpoints++;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"You reached the minimum level of ^3Improvements ^7skill.\n\"" );
			return;
		}
	}

	// zyk: saving the account file with the downgraded skill
	save_account(ent);

	trap->SendServerCommand( ent-g_entities, "print \"Skill downgraded successfully.\n\"" );
}

/*
==================
Cmd_ListAccount_f
==================
*/
void Cmd_ListAccount_f( gentity_t *ent ) {
	if (ent->client->sess.amrpgmode == 2)
	{
		if (trap->Argc() == 1)
		{ // zyk: if player didnt pass arguments, lists general info
			char rpg_class[32];
			strcpy(rpg_class,"");

			if (ent->client->pers.rpg_class == 0)
				strcpy(rpg_class,"Free Warrior");
			else if (ent->client->pers.rpg_class == 1)
				strcpy(rpg_class,"Force User");
			else if (ent->client->pers.rpg_class == 2)
				strcpy(rpg_class,"Bounty Hunter");
			else if (ent->client->pers.rpg_class == 3)
				strcpy(rpg_class,"Armored Soldier");
			else if (ent->client->pers.rpg_class == 4)
				strcpy(rpg_class,"Monk");
			else if (ent->client->pers.rpg_class == 5)
				strcpy(rpg_class,"Stealth Attacker");
			else if (ent->client->pers.rpg_class == 6)
				strcpy(rpg_class,"Duelist");

			trap->SendServerCommand(ent-g_entities, va("print \"\n^3Level: ^7%d/%d\n^3Level Up Score: ^7%d/%d\n^3Skill Points: ^7%d\n^3Skill Counter: ^7%d/%d\n^3Credits: ^7%d\n^3RPG Class: ^7%s\n\n^7Use ^2/list rpg ^7to see console commands\n\n\"", ent->client->pers.level, MAX_RPG_LEVEL, ent->client->pers.level_up_score, ent->client->pers.level, ent->client->pers.skillpoints, ent->client->pers.skill_counter, MAX_SKILL_COUNTER, ent->client->pers.credits, rpg_class));
		}
		else if (trap->Argc() == 2)
		{
			char message[1024];
			char message_content[10][100];
			char arg1[MAX_STRING_CHARS];
			int i = 0;
			strcpy(message,"");
				
			while (i < 9)
			{
				strcpy(message_content[i],"");
				i++;
			}
			message_content[9][0] = '\0';
			i = 0;

			trap->Argv(1, arg1, sizeof( arg1 ));

			if (Q_stricmp( arg1, "rpg" ) == 0)
			{
				trap->SendServerCommand(ent-g_entities, "print \"\n^2/list force: ^7lists force power skills\n^2/list weapons: ^7lists weapon skills\n^2/list other: ^7lists miscellaneous skills\n^2/list ammo: ^7lists ammo skills\n^2/list items: ^7lists the holdable items skills\n^2/list [skill number]: ^7lists info about a skill\n^2/list quests: ^7lists info about RPG mode quests\n^2/list commands: ^7lists the RPG Mode console commands\n^2/list classes: ^7lists the RPG classes\n^2/list stuff: ^7lists stuff bought from the seller\n\n\"");
			}
			else if (Q_stricmp( arg1, "force" ) == 0)
			{
				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 5)
					sprintf(message_content[0],"^0 1 - Jump: %d/5          ",ent->client->pers.force_powers_levels[0]);
				else
					sprintf(message_content[0],"^7 1 - Jump: %d/5          ",ent->client->pers.force_powers_levels[0]);
				
				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 5)
					sprintf(message_content[1],"^0 2 - Push: %d/3          ",ent->client->pers.force_powers_levels[1]);
				else
					sprintf(message_content[1],"^7 2 - Push: %d/3          ",ent->client->pers.force_powers_levels[1]);
				
				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 5)
					sprintf(message_content[2],"^0 3 - Pull: %d/3          ",ent->client->pers.force_powers_levels[2]);
				else
					sprintf(message_content[2],"^7 3 - Pull: %d/3          ",ent->client->pers.force_powers_levels[2]);
				
				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5)
					sprintf(message_content[3],"^0 4 - Speed: %d/4         ",ent->client->pers.force_powers_levels[3]);
				else
					sprintf(message_content[3],"^7 4 - Speed: %d/4         ",ent->client->pers.force_powers_levels[3]);
				
				sprintf(message_content[4],"^7 5 - Sense: %d/3         ",ent->client->pers.force_powers_levels[4]);
				
				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5)
					sprintf(message_content[5],"^0 6 - Saber Attack: %d/5  ",ent->client->pers.force_powers_levels[5]);
				else
					sprintf(message_content[5],"^3 6 - Saber Attack: %d/5  ",ent->client->pers.force_powers_levels[5]);
				
				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5)
					sprintf(message_content[6],"^0 7 - Saber Defense: %d/3 ",ent->client->pers.force_powers_levels[6]);
				else
					sprintf(message_content[6],"^3 7 - Saber Defense: %d/3 ",ent->client->pers.force_powers_levels[6]);
				
				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5)
					sprintf(message_content[7],"^0 8 - Saber Throw: %d/3   ",ent->client->pers.force_powers_levels[7]);
				else
					sprintf(message_content[7],"^3 8 - Saber Throw: %d/3   ",ent->client->pers.force_powers_levels[7]);
				
				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5)
					sprintf(message_content[8],"^0 9 - Absorb: %d/4        ",ent->client->pers.force_powers_levels[8]);
				else
					sprintf(message_content[8],"^5 9 - Absorb: %d/4        ",ent->client->pers.force_powers_levels[8]);

				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 5)
					sprintf(message_content[0],"%s^010 - Heal: %d/3\n",message_content[0],ent->client->pers.force_powers_levels[9]);
				else
					sprintf(message_content[0],"%s^510 - Heal: %d/3\n",message_content[0],ent->client->pers.force_powers_levels[9]);

				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5)
					sprintf(message_content[1],"%s^011 - Protect: %d/3\n",message_content[1],ent->client->pers.force_powers_levels[10]);
				else
					sprintf(message_content[1],"%s^511 - Protect: %d/3\n",message_content[1],ent->client->pers.force_powers_levels[10]);
				
				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 5 || ent->client->pers.rpg_class == 6)
					sprintf(message_content[2],"%s^012 - Mind Trick: %d/3\n",message_content[2],ent->client->pers.force_powers_levels[11]);
				else
					sprintf(message_content[2],"%s^512 - Mind Trick: %d/3\n",message_content[2],ent->client->pers.force_powers_levels[11]);
				
				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 5 || ent->client->pers.rpg_class == 6)
					sprintf(message_content[3],"%s^013 - Team Heal: %d/3\n",message_content[3],ent->client->pers.force_powers_levels[12]);
				else
					sprintf(message_content[3],"%s^513 - Team Heal: %d/3\n",message_content[3],ent->client->pers.force_powers_levels[12]);
				
				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5)
					sprintf(message_content[4],"%s^014 - Lightning: %d/3\n",message_content[4],ent->client->pers.force_powers_levels[13]);
				else
					sprintf(message_content[4],"%s^114 - Lightning: %d/3\n",message_content[4],ent->client->pers.force_powers_levels[13]);
				
				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 5)
					sprintf(message_content[5],"%s^015 - Grip: %d/3\n",message_content[5],ent->client->pers.force_powers_levels[14]);
				else
					sprintf(message_content[5],"%s^115 - Grip: %d/3\n",message_content[5],ent->client->pers.force_powers_levels[14]);

				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 5)
					sprintf(message_content[6],"%s^016 - Drain: %d/3\n",message_content[6],ent->client->pers.force_powers_levels[15]);
				else
					sprintf(message_content[6],"%s^116 - Drain: %d/3\n",message_content[6],ent->client->pers.force_powers_levels[15]);

				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5 || ent->client->pers.rpg_class == 6)
					sprintf(message_content[7],"%s^017 - Rage: %d/4\n",message_content[7],ent->client->pers.force_powers_levels[16]);
				else
					sprintf(message_content[7],"%s^117 - Rage: %d/4\n",message_content[7],ent->client->pers.force_powers_levels[16]);

				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 5 || ent->client->pers.rpg_class == 6)
					sprintf(message_content[8],"%s^018 - Team Energize: %d/3\n",message_content[8],ent->client->pers.force_powers_levels[17]);
				else
					sprintf(message_content[8],"%s^118 - Team Energize: %d/3\n",message_content[8],ent->client->pers.force_powers_levels[17]);

				for (i = 0; i < 9; i++)
				{
					sprintf(message,"%s%s",message,message_content[i]);
				}

				trap->SendServerCommand( ent-g_entities, va("print \"%s\"", message) );
			}
			else if (Q_stricmp( arg1, "weapons" ) == 0)
			{
				sprintf(message_content[0],"^319 - Stun Baton: %d/3        ",ent->client->pers.stun_baton_level);
					
				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5 || ent->client->pers.rpg_class == 6)
					sprintf(message_content[1],"^020 - Blaster Pistol: %d/2    ",ent->client->pers.weapons_levels[0]);
				else
					sprintf(message_content[1],"^320 - Blaster Pistol: %d/2    ",ent->client->pers.weapons_levels[0]);
							
				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5 || ent->client->pers.rpg_class == 6)
					sprintf(message_content[2],"^021 - E11 Blaster Rifle: %d/2 ",ent->client->pers.weapons_levels[1]);
				else
					sprintf(message_content[2],"^321 - E11 Blaster Rifle: %d/2 ",ent->client->pers.weapons_levels[1]);
							
				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 6)
					sprintf(message_content[3],"^022 - Disruptor: %d/2         ",ent->client->pers.weapons_levels[2]);
				else
					sprintf(message_content[3],"^322 - Disruptor: %d/2         ",ent->client->pers.weapons_levels[2]);
					
				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5 || ent->client->pers.rpg_class == 6)
					sprintf(message_content[4],"^023 - Bowcaster: %d/2         ",ent->client->pers.weapons_levels[3]);
				else
					sprintf(message_content[4],"^323 - Bowcaster: %d/2         ",ent->client->pers.weapons_levels[3]);
					
				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 6)
					sprintf(message_content[5],"^024 - Repeater: %d/2          ",ent->client->pers.weapons_levels[4]);
				else
					sprintf(message_content[5],"^324 - Repeater: %d/2          ",ent->client->pers.weapons_levels[4]);
					
				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 6)
					sprintf(message_content[0],"%s^025 - DEMP2: %d/2\n",message_content[0],ent->client->pers.weapons_levels[5]);
				else
					sprintf(message_content[0],"%s^325 - DEMP2: %d/2\n",message_content[0],ent->client->pers.weapons_levels[5]);
					
				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5 || ent->client->pers.rpg_class == 6)
					sprintf(message_content[1],"%s^026 - Flechette: %d/2\n",message_content[1],ent->client->pers.weapons_levels[6]);
				else
					sprintf(message_content[1],"%s^326 - Flechette: %d/2\n",message_content[1],ent->client->pers.weapons_levels[6]);
					
				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5 || ent->client->pers.rpg_class == 6)
					sprintf(message_content[2],"%s^027 - Rocket Launcher: %d/2\n",message_content[2],ent->client->pers.weapons_levels[7]);
				else
					sprintf(message_content[2],"%s^327 - Rocket Launcher: %d/2\n",message_content[2],ent->client->pers.weapons_levels[7]);
					
				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 6)
					sprintf(message_content[3],"%s^028 - Concussion Rifle: %d/2\n",message_content[3],ent->client->pers.weapons_levels[8]);
				else
					sprintf(message_content[3],"%s^328 - Concussion Rifle: %d/2\n",message_content[3],ent->client->pers.weapons_levels[8]);
					
				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5 || ent->client->pers.rpg_class == 6)
					sprintf(message_content[4],"%s^029 - Bryar Pistol: %d/2\n",message_content[4],ent->client->pers.weapons_levels[9]);
				else
					sprintf(message_content[4],"%s^329 - Bryar Pistol: %d/2\n",message_content[4],ent->client->pers.weapons_levels[9]);

				sprintf(message_content[5],"%s^330 - Melee: %d/3\n",message_content[5],ent->client->pers.melee_level);

				for (i = 0; i < 6; i++)
				{
					sprintf(message,"%s%s",message,message_content[i]);
				}

				trap->SendServerCommand( ent-g_entities, va("print \"%s\"", message) );
			}
			else if (Q_stricmp( arg1, "other" ) == 0)
			{
				sprintf(message_content[0],"^231 - Max Shield: %d/5       ", ent->client->pers.starting_shield_level);

				sprintf(message_content[1],"^232 - Shield Strength: %d/4  ", ent->client->pers.shield_strength);

				sprintf(message_content[2],"^133 - Health Strength: %d/4  ", ent->client->pers.health_strength);

				sprintf(message_content[3],"^334 - Drain Shield: %d/1     ", ent->client->pers.drain_shield);

				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 6)
					sprintf(message_content[4],"^035 - Jetpack: %d/3          ", ent->client->pers.jetpack_level);
				else
					sprintf(message_content[4],"^335 - Jetpack: %d/3          ", ent->client->pers.jetpack_level);

				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 5)
					sprintf(message_content[5],"^036 - Sense Health: %d/3     ", ent->client->pers.playerhealth);
				else
					sprintf(message_content[5],"^636 - Sense Health: %d/3     ", ent->client->pers.playerhealth);

				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 5)
					sprintf(message_content[6],"^037 - Shield Heal: %d/3      ", ent->client->pers.shield);
				else
					sprintf(message_content[6],"^637 - Shield Heal: %d/3      ", ent->client->pers.shield);

				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 5 || ent->client->pers.rpg_class == 6)
					sprintf(message_content[7],"^038 - Team Shield Heal: %d/3\n", ent->client->pers.teamshield);
				else
					sprintf(message_content[7],"^638 - Team Shield Heal: %d/3\n", ent->client->pers.teamshield);

				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5 || ent->client->pers.rpg_class == 6)
					sprintf(message_content[8],"^039 - Mind Control: %d/1\n", ent->client->pers.mind_control);
				else
					sprintf(message_content[8],"^639 - Mind Control: %d/1\n", ent->client->pers.mind_control);

				if (ent->client->pers.rpg_class == 2 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 5)
					sprintf(message_content[0],"%s^055 - Force Power: %d/5\n",message_content[0], ent->client->pers.max_force_power_level);
				else
					sprintf(message_content[0],"%s^555 - Force Power: %d/5\n",message_content[0], ent->client->pers.max_force_power_level);

				sprintf(message_content[1],"%s^356 - Improvements: %d/3\n",message_content[1], ent->client->pers.improvements_level);

				if (ent->client->pers.defeated_guardians == NUMBER_OF_GUARDIANS)
					sprintf(message_content[2],"%s^3+  ^7- Light Power: ^2yes\n",message_content[2]);
				else
					sprintf(message_content[2],"%s^3+  ^7- Light Power: ^1no\n",message_content[2]);

				if (ent->client->pers.hunter_quest_progress == NUMBER_OF_OBJECTIVES)
					sprintf(message_content[3],"%s^3x  ^1- Dark Power: ^2yes\n",message_content[3]);
				else
					sprintf(message_content[3],"%s^3x  ^1- Dark Power: ^1no\n",message_content[3]);

				if (ent->client->pers.eternity_quest_progress == NUMBER_OF_ETERNITY_QUEST_OBJECTIVES)
					sprintf(message_content[4],"%s^3*  - Eternity Power: ^2yes\n",message_content[4]);
				else
					sprintf(message_content[4],"%s^3*  - Eternity Power: ^1no\n",message_content[4]);

				if (ent->client->pers.universe_quest_progress >= 8)
					sprintf(message_content[5],"%s^3#  ^2- Universe Power: ^2yes\n",message_content[5]);
				else
					sprintf(message_content[5],"%s^3#  ^2- Universe Power: ^1no\n",message_content[5]);

				if (ent->client->pers.universe_quest_progress == NUMBER_OF_UNIVERSE_QUEST_OBJECTIVES)
					sprintf(message_content[6],"%s^3!  ^5- Ultimate Power: ^2yes\n",message_content[6]);
				else
					sprintf(message_content[6],"%s^3!  ^5- Ultimate Power: ^1no\n",message_content[6]);

				for (i = 0; i < 9; i++)
				{
					sprintf(message,"%s%s",message,message_content[i]);
				}

				trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", message) );
			}
			else if (Q_stricmp( arg1, "ammo" ) == 0)
			{
				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5 || ent->client->pers.rpg_class == 6)
					sprintf(message,"%s^040 - Blaster Pack: %d/3\n",message, ent->client->pers.ammo_levels[0]);
				else
					sprintf(message,"%s^340 - Blaster Pack: %d/3\n",message, ent->client->pers.ammo_levels[0]);

				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 6)
					sprintf(message,"%s^041 - Power Cell: %d/3\n",message, ent->client->pers.ammo_levels[1]);
				else
					sprintf(message,"%s^341 - Power Cell: %d/3\n",message, ent->client->pers.ammo_levels[1]);

				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 6)
					sprintf(message,"%s^042 - Metallic Bolt: %d/3\n",message, ent->client->pers.ammo_levels[2]);
				else
					sprintf(message,"%s^342 - Metallic Bolt: %d/3\n",message, ent->client->pers.ammo_levels[2]);

				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5 || ent->client->pers.rpg_class == 6)
					sprintf(message,"%s^043 - Rockets: %d/3\n",message, ent->client->pers.ammo_levels[3]);
				else
					sprintf(message,"%s^343 - Rockets: %d/3\n",message, ent->client->pers.ammo_levels[3]);
					
				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5 || ent->client->pers.rpg_class == 6)
					sprintf(message,"%s^044 - Thermals: %d/3\n",message, ent->client->pers.ammo_levels[4]);
				else
					sprintf(message,"%s^344 - Thermals: %d/3\n",message, ent->client->pers.ammo_levels[4]);

				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 6)
					sprintf(message,"%s^045 - Trip Mines: %d/3\n",message, ent->client->pers.ammo_levels[5]);
				else
					sprintf(message,"%s^345 - Trip Mines: %d/3\n",message, ent->client->pers.ammo_levels[5]);

				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 6)
					sprintf(message,"%s^046 - Det Packs: %d/3\n",message, ent->client->pers.ammo_levels[6]);
				else
					sprintf(message,"%s^346 - Det Packs: %d/3\n",message, ent->client->pers.ammo_levels[6]);

				trap->SendServerCommand( ent-g_entities, va("print \"%s\"", message) );
			}
			else if (Q_stricmp( arg1, "items" ) == 0)
			{
				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 6)
					sprintf(message,"%s^047 - Binoculars: %d/1\n",message, ent->client->pers.holdable_items_levels[0]);
				else
					sprintf(message,"%s^347 - Binoculars: %d/1\n",message, ent->client->pers.holdable_items_levels[0]);
					
				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5 || ent->client->pers.rpg_class == 6)
					sprintf(message,"%s^048 - Bacta Canister: %d/2\n",message, ent->client->pers.holdable_items_levels[1]);
				else
					sprintf(message,"%s^348 - Bacta Canister: %d/2\n",message, ent->client->pers.holdable_items_levels[1]);

				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5 || ent->client->pers.rpg_class == 6)
					sprintf(message,"%s^049 - Sentry Gun: %d/1\n",message, ent->client->pers.holdable_items_levels[2]);
				else
					sprintf(message,"%s^349 - Sentry Gun: %d/1\n",message, ent->client->pers.holdable_items_levels[2]);

				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5 || ent->client->pers.rpg_class == 6)
					sprintf(message,"%s^050 - Seeker Drone: %d/1\n",message, ent->client->pers.holdable_items_levels[3]);
				else
					sprintf(message,"%s^350 - Seeker Drone: %d/1\n",message, ent->client->pers.holdable_items_levels[3]);

				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5 || ent->client->pers.rpg_class == 6)
					sprintf(message,"%s^051 - E-Web: %d/1\n",message, ent->client->pers.holdable_items_levels[4]);
				else
					sprintf(message,"%s^351 - E-Web: %d/1\n",message, ent->client->pers.holdable_items_levels[4]);

				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5 || ent->client->pers.rpg_class == 6)
					sprintf(message,"%s^052 - Big Bacta: %d/2\n",message, ent->client->pers.holdable_items_levels[5]);
				else
					sprintf(message,"%s^352 - Big Bacta: %d/2\n",message, ent->client->pers.holdable_items_levels[5]);

				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 5 || ent->client->pers.rpg_class == 6)
					sprintf(message,"%s^053 - Force Field: %d/2\n",message, ent->client->pers.holdable_items_levels[6]);
				else
					sprintf(message,"%s^353 - Force Field: %d/2\n",message, ent->client->pers.holdable_items_levels[6]);

				if (ent->client->pers.rpg_class == 1 || ent->client->pers.rpg_class == 3 || ent->client->pers.rpg_class == 4 || ent->client->pers.rpg_class == 6)
					sprintf(message,"%s^054 - Cloak Item: %d/2\n",message, ent->client->pers.holdable_items_levels[7]);
				else
					sprintf(message,"%s^354 - Cloak Item: %d/2\n",message, ent->client->pers.holdable_items_levels[7]);

				trap->SendServerCommand( ent-g_entities, va("print \"%s\"", message) );
			}
			else if (Q_stricmp( arg1, "quests" ) == 0)
			{
				trap->SendServerCommand(ent-g_entities, "print \"\n^3RPG Mode Quests\n\n^2/list universe: ^7Universe Quest (Main Quest)\n\n^2/list light: ^7Light Quest\n^2/list dark: ^7Dark Quest\n^2/list eternity: ^7Eternity Quest\n\n^3/list bounty: ^7The Bounty Hunter quest\n^3/list guardian: ^7The Guardian Quest\n\n\"");
			}
			else if (Q_stricmp( arg1, "light" ) == 0)
			{
				char guardian_message[MAX_STRING_CHARS];
				strcpy(guardian_message, "");

				if (ent->client->pers.defeated_guardians != NUMBER_OF_GUARDIANS)
				{
					if (light_quest_defeated_guardians(ent) == qtrue)
					{
						strcpy(guardian_message, "^3\n2 - Guardian of Light\n\n^7Go to the sacred monument in ^3yavin2^7 and defeat the Guardian of Light.");
					}
					else
					{
						strcpy(guardian_message, "^3\n1 - The Guardians\n\n^7Defeat the guardians in their respective maps\n");

						if (ent->client->pers.defeated_guardians & (1 << 4))
							strcpy(guardian_message, va("%s\n^4Guardian of Water ^7(yavin1b) - ^2yes",guardian_message));
						else
							strcpy(guardian_message, va("%s\n^4Guardian of Water ^7(yavin1b) - ^1no",guardian_message));

						if (ent->client->pers.defeated_guardians & (1 << 5))
							strcpy(guardian_message, va("%s\n^3Guardian of Earth ^7(t1_fatal) - ^2yes",guardian_message));
						else
							strcpy(guardian_message, va("%s\n^3Guardian of Earth ^7(t1_fatal) - ^1no",guardian_message));

						if (ent->client->pers.defeated_guardians & (1 << 6))
							strcpy(guardian_message, va("%s\n^2Guardian of Forest ^7(yavin2) - ^2yes",guardian_message));
						else
							strcpy(guardian_message, va("%s\n^2Guardian of Forest ^7(yavin2) - ^1no",guardian_message));

						if (ent->client->pers.defeated_guardians & (1 << 7))
							strcpy(guardian_message, va("%s\n^5Guardian of Intelligence ^7(t2_rogue) - ^2yes",guardian_message));
						else
							strcpy(guardian_message, va("%s\n^5Guardian of Intelligence ^7(t2_rogue) - ^1no",guardian_message));

						if (ent->client->pers.defeated_guardians & (1 << 8))
							strcpy(guardian_message, va("%s\n^6Guardian of Agility ^7(hoth3) - ^2yes",guardian_message));
						else
							strcpy(guardian_message, va("%s\n^6Guardian of Agility ^7(hoth3) - ^1no",guardian_message));

						if (ent->client->pers.defeated_guardians & (1 << 9))
							strcpy(guardian_message, va("%s\n^1Guardian of Fire ^7(kor1) - ^2yes",guardian_message));
						else
							strcpy(guardian_message, va("%s\n^1Guardian of Fire ^7(kor1) - ^1no",guardian_message));

						if (ent->client->pers.defeated_guardians & (1 << 10))
							strcpy(guardian_message, va("%s\n^7Guardian of Wind ^7(t2_wedge) - ^2yes",guardian_message));
						else
							strcpy(guardian_message, va("%s\n^7Guardian of Wind ^7(t2_wedge) - ^1no",guardian_message));

						if (ent->client->pers.defeated_guardians & (1 << 11))
							strcpy(guardian_message, va("%s\n^3Guardian of Resistance ^7(mp/duel8) - ^2yes",guardian_message));
						else
							strcpy(guardian_message, va("%s\n^3Guardian of Resistance ^7(mp/duel8) - ^1no",guardian_message));
					}
				}
				else
				{
					strcpy(guardian_message, "^7Completed!");
				}

				sprintf(message,"\n^5Light Quest\n%s^7\n\n",guardian_message);

				trap->SendServerCommand( ent-g_entities, va("print \"%s\"", message) );
			}
			else if (Q_stricmp( arg1, "dark" ) == 0)
			{
				char dark_quest_message[MAX_STRING_CHARS];
				strcpy(dark_quest_message, "");

				if (ent->client->pers.hunter_quest_progress != NUMBER_OF_OBJECTIVES)
				{
					if (dark_quest_collected_notes(ent) == qtrue)
					{
						strcpy(dark_quest_message, "^3\n2 - Guardian of Darkness\n\n^7Defeat the Guardian of Darkness in the dark room in ^3yavin2^7.");
					}
					else
					{
						strcpy(dark_quest_message, "^3\n1 - The Notes\n\n^7Find the notes in their respective maps\n");

						if (ent->client->pers.hunter_quest_progress & (1 << 4))
							strcpy(dark_quest_message, va("%s\n^7Note ^3in the temple of the forest - ^2yes",dark_quest_message));
						else
							strcpy(dark_quest_message, va("%s\n^7Note ^3in the temple of the forest - ^1no",dark_quest_message));

						if (ent->client->pers.hunter_quest_progress & (1 << 5))
							strcpy(dark_quest_message, va("%s\n^7Note ^3in a spaceport of a desert planet - ^2yes",dark_quest_message));
						else
							strcpy(dark_quest_message, va("%s\n^7Note ^3in a spaceport of a desert planet - ^1no",dark_quest_message));

						if (ent->client->pers.hunter_quest_progress & (1 << 6))
							strcpy(dark_quest_message, va("%s\n^7Note ^3in the desert with the people of the sands - ^2yes",dark_quest_message));
						else
							strcpy(dark_quest_message, va("%s\n^7Note ^3in the desert with the people of the sands - ^1no",dark_quest_message));

						if (ent->client->pers.hunter_quest_progress & (1 << 7))
							strcpy(dark_quest_message, va("%s\n^7Note ^3in a very deep burial location - ^2yes",dark_quest_message));
						else
							strcpy(dark_quest_message, va("%s\n^7Note ^3in a very deep burial location - ^1no",dark_quest_message));

						if (ent->client->pers.hunter_quest_progress & (1 << 8))
							strcpy(dark_quest_message, va("%s\n^7Note ^3in a very cold place - ^2yes",dark_quest_message));
						else
							strcpy(dark_quest_message, va("%s\n^7Note ^3in a very cold place - ^1no",dark_quest_message));

						if (ent->client->pers.hunter_quest_progress & (1 << 9))
							strcpy(dark_quest_message, va("%s\n^7Note ^3in an abandoned and forgotten city - ^2yes",dark_quest_message));
						else
							strcpy(dark_quest_message, va("%s\n^7Note ^3in an abandoned and forgotten city - ^1no",dark_quest_message));

						if (ent->client->pers.hunter_quest_progress & (1 << 10))
							strcpy(dark_quest_message, va("%s\n^7Note ^3in the office of a crime lord - ^2yes",dark_quest_message));
						else
							strcpy(dark_quest_message, va("%s\n^7Note ^3in the office of a crime lord - ^1no",dark_quest_message));

						if (ent->client->pers.hunter_quest_progress & (1 << 11))
							strcpy(dark_quest_message, va("%s\n^7Note ^3in the sand worm desert - ^2yes",dark_quest_message));
						else
							strcpy(dark_quest_message, va("%s\n^7Note ^3in the sand worm desert - ^1no",dark_quest_message));

						if (ent->client->pers.hunter_quest_progress & (1 << 12))
							strcpy(dark_quest_message, va("%s\n^7Note ^3in the tomb of a powerful sith - ^2yes",dark_quest_message));
						else
							strcpy(dark_quest_message, va("%s\n^7Note ^3in the tomb of a powerful sith - ^1no",dark_quest_message));
					}
				}
				else
				{
					strcpy(dark_quest_message, "^7Completed!");
				}

				sprintf(message,"\n^1Dark Quest\n^7%s\n\n",dark_quest_message);

				trap->SendServerCommand( ent-g_entities, va("print \"%s\"", message) );
			}
			else if (Q_stricmp( arg1, "eternity" ) == 0)
			{
				char eternity_message[512];
				strcpy(eternity_message, "");

				if (ent->client->pers.eternity_quest_progress < NUMBER_OF_ETERNITY_QUEST_OBJECTIVES)
				{
					if (ent->client->pers.eternity_quest_progress < 10)
						strcpy(eternity_message, va("^3\n1 - Riddles\n\n^7Find the ^3riddles ^7near the waterfall in ^3yavin2^7. Use ^2/answer ^7to answer the riddle.\nAnswered riddles: ^3%d^7.",ent->client->pers.eternity_quest_progress));
					else if (ent->client->pers.eternity_quest_progress == 10)
						strcpy(eternity_message, "^3\n2 - Guardian of Eternity\n\n^7Defeat the ^3Guardian of Eternity ^7near the waterfall in ^3yavin2^7.");
				}
				else
				{
					strcpy(eternity_message, "^7Completed!");
				}

				sprintf(message,"\n^3Eternity Quest\n^7%s\n\n",eternity_message);

				trap->SendServerCommand( ent-g_entities, va("print \"%s\"", message) );
			}
			else if (Q_stricmp( arg1, "universe" ) == 0)
			{
				char universe_message[MAX_STRING_CHARS];
				strcpy(universe_message, "");

				if (ent->client->pers.universe_quest_progress < NUMBER_OF_UNIVERSE_QUEST_OBJECTIVES)
				{
					if (ent->client->pers.universe_quest_progress == 0)
						strcpy(universe_message, va("^3\n1. The Hero's Quest Begins\n\n^7Defeat the reborn attack at ^3kor2 ^7to save the Guardian Sages. Quest reborns remaining: ^3%d^7.",ent->client->pers.universe_quest_objective_control));
					else if (ent->client->pers.universe_quest_progress == 1)
						strcpy(universe_message, "^3\n2. The Rise of an Evil Force\n\n^7Talk to the sages at ^3kor2 ^7to know more about your quest.");
					else if (ent->client->pers.universe_quest_progress == 2)
						strcpy(universe_message, va("^3\n3. The Life-Force Artifacts\n\n^7Find the 10 artifacts in SP maps and talk to the sages at ^3yavin1b^7.\nCollected artifacts: ^3%d^7",number_of_artifacts(ent)));
					else if (ent->client->pers.universe_quest_progress == 3)
						strcpy(universe_message, "^3\n4. In Search for Answers\n\n^7Go to ^3yavin1b ^7to talk to the sages about the mysterious voice you heard at the beginning of the quest.");
					else if (ent->client->pers.universe_quest_progress == 4)
						strcpy(universe_message, "^3\n5. The Hidden Sage\n\n^7Find the Sage of Universe at ^3t3_hevil^7.");
					else if (ent->client->pers.universe_quest_progress == 5)
						strcpy(universe_message, va("^3\n6. The Guardian Amulets\n\n^7Find the three guardian amulets at the City of the Merchants in ^3mp/siege_desert^7. Amulets collected: ^3%d^7.", number_of_amulets(ent)));
					else if (ent->client->pers.universe_quest_progress == 6)
						strcpy(universe_message, "^3\n7. The Decisive Battle\n\n^7Defeat the Master of Evil at ^3taspir1^7.");
					else if (ent->client->pers.universe_quest_progress == 7)
						strcpy(universe_message, "^3\n8. The Guardian of Universe\n\n^7Defeat the ^2Guardian of Universe ^7at ^3mp/siege_korriban^7.");
					else if (ent->client->pers.universe_quest_progress == 8)
					{
						strcpy(universe_message, "^3\n9. Revelations\n\n^7You had a strange dream of someone calling you from a sacred place. You must go there, talk to the sages and to the guardians about it.\n");
						if (ent->client->pers.universe_quest_counter & (1 << 0))
							strcpy(universe_message, va("%s\n^3Sages - ^7Go to ^3yavin1b ^7at the temple and speak to the sages - ^2yes",universe_message));
						else
							strcpy(universe_message, va("%s\n^3Sages - ^7Go to ^3yavin1b ^7at the temple and speak to the sages - ^1no",universe_message));
						
						if (ent->client->pers.universe_quest_counter & (1 << 1))
							strcpy(universe_message, va("%s\n^3Guardians - ^7Go to ^3mp/siege_korriban ^7at the ragnos tomb area and speak to the guardians - ^2yes",universe_message));
						else
							strcpy(universe_message, va("%s\n^3Guardians - ^7Go to ^3mp/siege_korriban ^7at the ragnos tomb area and speak to the guardians - ^1no",universe_message));

						if (ent->client->pers.universe_quest_counter & (1 << 2))
							strcpy(universe_message, va("%s\n^3Sacred Monument - ^7Go to ^3t2_trip ^7at the sacred obelisk to find out about the dream - ^2yes",universe_message));
						else
							strcpy(universe_message, va("%s\n^3Sacred Monument - ^7Go to ^3t2_trip ^7at the sacred obelisk to find out about the dream - ^1no",universe_message));
					}
					else if (ent->client->pers.universe_quest_progress == 9)
					{
						strcpy(universe_message, "^3\n10. The Sacred Crystals\n\n^7Find the sacred crystals in SP maps. You need them to free the Guardian of Time.\n");
						if (ent->client->pers.universe_quest_counter & (1 << 0))
							strcpy(universe_message, va("%s\n^4Crystal of Destiny ^3- ^2yes",universe_message));
						else
							strcpy(universe_message, va("%s\n^4Crystal of Destiny ^3- ^1no",universe_message));
						
						if (ent->client->pers.universe_quest_counter & (1 << 1))
							strcpy(universe_message, va("%s\n^1Crystal of Truth ^3- ^2yes",universe_message));
						else
							strcpy(universe_message, va("%s\n^1Crystal of Truth ^3- ^1no",universe_message));

						if (ent->client->pers.universe_quest_counter & (1 << 2))
							strcpy(universe_message, va("%s\n^2Crystal of Time ^3- ^2yes",universe_message));
						else
							strcpy(universe_message, va("%s\n^2Crystal of Time ^3- ^1no",universe_message));
					}
					else if (ent->client->pers.universe_quest_progress == 10)
					{
						strcpy(universe_message, "^3\n11. Finally Free\n\n^7You have the sacred crystals.\nGo to ^3t2_trip ^7and free the Guardian of Time.");
					}
					else if (ent->client->pers.universe_quest_progress == 11)
					{
						strcpy(universe_message, va("^3\n12. Battle for the Temple\n\n^7Master of Evil sent his entire army to the temple.\nDefeat all of them with the help of your allies.\nEnemies: ^3%d^7", ent->client->pers.universe_quest_objective_control));
					}
					else if (ent->client->pers.universe_quest_progress == 12)
					{
						strcpy(universe_message, "^3\n13. The Final Revelation\n\n^7Listen to the revelation that will be decisive to the fate of the Universe.");
					}
					else if (ent->client->pers.universe_quest_progress == 13)
					{
						strcpy(universe_message, "^3\n14. The Hero's Choice\n\n^7Listen to the sages, guardians, Guardian of Time and the Master of Evil.\nAfter that, choose one of them that represents your choice and bring him to the center of the water inside the temple.\nThis decision will change the end of the quest and the power you will receive.");
					}
					else if (ent->client->pers.universe_quest_progress == 14)
					{
						strcpy(universe_message, "^3\n15. The Fate of the Universe\n\n^7Go to the Sacred Dimension and defeat the ^1Guardian of Chaos^7.\nThe end of the quest depends on the choice you made earlier.");
					}
				}
				else
				{
					strcpy(universe_message, "^7Completed!");
				}

				sprintf(message,"\n^2Universe Quest\n%s\n\n",universe_message);

				trap->SendServerCommand( ent-g_entities, va("print \"%s\"", message) );
			}
			else if (Q_stricmp( arg1, "bounty" ) == 0)
			{
				trap->SendServerCommand( ent-g_entities, va("print \"\n^3Bounty Quest\n^7Use ^3/bountyquest ^7so the server chooses a player to be the target. If the target defeats a RPG player, he receives 200 bonus credits. If a bounty hunter kills the target, he receives bonus credits based in the target player level.\n\n\"") );
			}
			else if (Q_stricmp( arg1, "guardian" ) == 0)
			{
				trap->SendServerCommand( ent-g_entities, va("print \"\n^3Guardian Quest\n^7Use ^3/guardianquest ^7so the server spawns the map guardian somewhere. If the player defeats it, he gets 5 experience points (Level Up Score) and 1000 credits.\n\n\"") );
			}
			else if (Q_stricmp( arg1, "commands" ) == 0)
			{
				trap->SendServerCommand( ent-g_entities, "print \"\n^2RPG Mode commands\n\n^3/new [login] [password]: ^7creates a new account.\n^3/login [login] [password]: ^7loads the account.\n^3/playermode: ^7switches between ^2Admin-Only Mode ^7and ^2RPG Mode^7.\n^3/up [skill number]: ^7upgrades a skill.\n^3/down [skill number]: ^7downgrades a skill.\n^3/resetaccount: ^7resets quests or the entire RPG Mode of a player.\n^3/adminlist: ^7lists admin commands.\n^3/adminup [player id or name] [command number]: ^7gives the player an admin command.\n^3/admindown [player id or name] [command number]: ^7removes an admin command from a player.\n^3/settings: ^7turn on or off player settings.\n^3/callseller: ^7calls the jawa seller.\n^3/creditgive [player id or name] [amount]: ^7gives credits to a player.\n^3/changepassword <new_password>: ^7changes the account password.\n^3/entitysystem: ^7shows Entity System commands.\n^3/logout: ^7logs out the account.\n\n\"" );
			}
			else if (Q_stricmp( arg1, "classes" ) == 0)
			{
				trap->SendServerCommand( ent-g_entities, "print \"\n^30 - Free Warrior\n^7    Has all skills, but no unique features\n^31 - Force User\n^7    Has force powers, saber and melee. Saber does more damage. Force powers use less force\n^32 - Bounty Hunter\n^7    Has guns with more damage. Gets more credits in battles\n^33 - Armored Soldier\n^7    Deflects some gun shots. Can use guns and some holdable items. Takes less damage. Has auto-heal in shield\n^34 - Monk\n^7    Has melee and some force powers. Melee does more damage and destroy breakable objects. Has auto-heal in HP\n^35 - Stealth Attacker\n^7    Uses some guns. Has a lot of gun damage. Cloak Item does not decloak by electric attacks, and also takes less electric damage\n^36 - Duelist\n^7   Has some force powers and has the highest saber damage. Melee does more damage\n^2Use ^3/rpgclass <class number> ^2to change your class\n\"" );
			}
			else if (Q_stricmp( arg1, "stuff" ) == 0)
			{
				char stuff_message[1024];
				strcpy(stuff_message,"");

				if (ent->client->pers.secrets_found & (1 << 1))
					strcpy(stuff_message,va("%s^3\nBounty Hunter Upgrade - ^2yes\n",stuff_message));
				else
					strcpy(stuff_message,va("%s^3\nBounty Hunter Upgrade - ^1no\n",stuff_message));

				if (ent->client->pers.secrets_found & (1 << 7))
					strcpy(stuff_message,va("%s^3Stealth Attacker Upgrade - ^2yes\n",stuff_message));
				else
					strcpy(stuff_message,va("%s^3Stealth Attacker Upgrade - ^1no\n",stuff_message));

				if (ent->client->pers.secrets_found & (1 << 9))
					strcpy(stuff_message,va("%s^3Impact Reducer - ^2yes\n",stuff_message));
				else
					strcpy(stuff_message,va("%s^3Impact Reducer - ^1no\n",stuff_message));

				if (ent->client->pers.secrets_found & (1 << 10))
					strcpy(stuff_message,va("%s^3Flame Thrower Upgrade - ^2yes\n",stuff_message));
				else
					strcpy(stuff_message,va("%s^3Flame Thrower Upgrade - ^1no\n",stuff_message));

				if (ent->client->pers.secrets_found & (1 << 11))
					strcpy(stuff_message,va("%s^3Power Cell Weapons Upgrade - ^2yes\n",stuff_message));
				else
					strcpy(stuff_message,va("%s^3Power Cell Weapons Upgrade - ^1no\n",stuff_message));

				if (ent->client->pers.secrets_found & (1 << 12))
					strcpy(stuff_message,va("%s^3Blaster Pack Weapons Upgrade - ^2yes\n",stuff_message));
				else
					strcpy(stuff_message,va("%s^3Blaster Pack Weapons Upgrade - ^1no\n",stuff_message));

				if (ent->client->pers.secrets_found & (1 << 13))
					strcpy(stuff_message,va("%s^3Metal Bolts Weapons Upgrade - ^2yes\n",stuff_message));
				else
					strcpy(stuff_message,va("%s^3Metal Bolts Weapons Upgrade - ^1no\n",stuff_message));

				if (ent->client->pers.secrets_found & (1 << 14))
					strcpy(stuff_message,va("%s^3Rocket Upgrade - ^2yes\n",stuff_message));
				else
					strcpy(stuff_message,va("%s^3Rocket Upgrade - ^1no\n",stuff_message));

				if (ent->client->pers.secrets_found & (1 << 15))
					strcpy(stuff_message,va("%s^3Stun Baton Upgrade - ^2yes\n",stuff_message));
				else
					strcpy(stuff_message,va("%s^3Stun Baton Upgrade - ^1no\n",stuff_message));

				if (ent->client->pers.secrets_found & (1 << 16))
					strcpy(stuff_message,va("%s^3Armored Soldier Upgrade - ^2yes\n",stuff_message));
				else
					strcpy(stuff_message,va("%s^3Armored Soldier Upgrade - ^1no\n",stuff_message));

				trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"",stuff_message) );
			}
			else
			{ // zyk: the player can also list the specific info of a skill passing the skill number as argument
				i = atoi(arg1);
				if (i >= 1 && i <= NUMBER_OF_SKILLS)
				{
					if (i == 1)
						trap->SendServerCommand( ent-g_entities, "print \"^3Jump: ^7makes you jump higher\n\"" );
					if (i == 2)
						trap->SendServerCommand( ent-g_entities, "print \"^3Push: ^7pushes the opponent forward\n\"" );
					if (i == 3)
						trap->SendServerCommand( ent-g_entities, "print \"^3Pull: ^7pulls the opponent towards you\n\"" );
					if (i == 4)
						trap->SendServerCommand( ent-g_entities, "print \"^3Speed: ^7increases your speed. Level 1 is 1.5 times normal speed. Level 2 is 2.4, level 3 is 3.3 times and level 4 is 4.2 times\n\"" );
					if (i == 5)
						trap->SendServerCommand( ent-g_entities, "print \"^3Sense: ^7allows you to see people through walls, invisible people or cloaked people and you can dodge disruptor shots. Represents your mind strength to resist Mind Control if your sense level is equal or higher than the enemy's mind trick level\n\"" );
					if (i == 6)
						trap->SendServerCommand( ent-g_entities, "print \"^3Saber Attack: ^7gives you the saber. If you are using Single Saber, gives you the saber styles. If using duals or staff, increases saber damage, which is increased by 25 per cent until level 5, which has 10 per cent more damage\n\"" );
					if (i == 7)
						trap->SendServerCommand( ent-g_entities, "print \"^3Saber Defense: ^7increases your ability to block, parry enemy saber attacks or enemy shots\n\"" );
					if (i == 8)
						trap->SendServerCommand( ent-g_entities, "print \"^3Saber Throw: ^7throws your saber at enemy and gets it back\n\"" );
					if (i == 9)
						trap->SendServerCommand( ent-g_entities, "print \"^3Absorb: ^7allows you to absorb force power attacks done to you\n\"" );
					if (i == 10)
						trap->SendServerCommand( ent-g_entities, "print \"^3Heal: ^7recover some Health. Level 1 restores 5 hp, level 2 restores 10 hp and level 3 restores 25 hp\n\"" );
					if (i == 11)
						trap->SendServerCommand( ent-g_entities, "print \"^3Protect: ^7decreases damage done to you by non-force power attacks\n\"" );
					if (i == 12)
						trap->SendServerCommand( ent-g_entities, "print \"^3Mind Trick: ^7makes yourself invisible to the players affected by this force power. If you have Mind Control, you can also control a player or npc. Level 1 has a duration of 20 seconds, level 2 is 25 seconds and level 3 is 30 seconds\n\"" );
					if (i == 13)
						trap->SendServerCommand( ent-g_entities, "print \"^3Team Heal: ^7restores some health to players near you\n\"" );
					if (i == 14)
						trap->SendServerCommand( ent-g_entities, "print \"^3Lightning: ^7attacks with a powerful electric attack at players near you\n\"" );
					if (i == 15)
						trap->SendServerCommand( ent-g_entities, "print \"^3Grip: ^7attacks a player by holding and damaging him\n\"" );
					if (i == 16)
						trap->SendServerCommand( ent-g_entities, "print \"^3Drain: ^7drains force power from a player to restore you health\n\"" );
					if (i == 17)
						trap->SendServerCommand( ent-g_entities, "print \"^3Rage: ^7makes you 1.3 times faster, increases your saber attack speed and damage and makes you get less damage\n\"" );
					if (i == 18)
						trap->SendServerCommand( ent-g_entities, "print \"^3Team Energize: ^7restores some force power to players near you. If Improvements skill is at least at level 1, regens blaster pack and power cell ammo of the target players\n\"" );
					if (i == 19)
						trap->SendServerCommand( ent-g_entities, "print \"^3Stun Baton: ^7attacks someone with a small electric charge. Level 2 causes double damage and knocks down the enemy (does not work for Force User or Monk). Level 3 causes triple damage, decloaks enemies and fires the flame thrower when using alternate fire (does not work for Force User or Monk). With Stun Baton Upgrade, it opens any door, even locked ones, and can destroy or move some other objects\n\"" );
					if (i == 20)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Blaster Pistol: ^7the popular Star Wars pistol used by Han Solo, Leia and other people in the movies. Normal fire is a single laser shot, alternate fire allows you to fire a powerful charged shot. The pistol laser causes %d damage. The charged shot causes a lot more damage depending on how much it was charged. At level 2 causes 25 per cent more damage\n\"", 50));
					if (i == 21)
						trap->SendServerCommand( ent-g_entities, va("print \"^3E11 Blaster Rifle: ^7the rifle used by the Storm Troopers. E11 shots cause %d damage. Normal fire is a single shot, while the alternate fire is the rapid fire. At level 2 causes 25 per cent more damage\n\"", 50) );
					if (i == 22)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Disruptor: ^7the sniper, used by the rodians ingame. Normal fire is a shot that causes %d damage, alternate fire allows zoom and a charged shot that when fully charged, causes %d damage. At level 2 causes 25 per cent more damage and has a better fire rate\n\"", 50, 50) );
					if (i == 23)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Bowcaster: ^7the famous weapon used by Chewbacca. Normal fire can be charged to fire up to 5 shots at once or 9 at level 2. Alternate fire fires a bouncing shot. At level 2 increases number of bounces. It causes %d damage. At level 2 causes 25 per cent more damage\n\"", 50) );
					if (i == 24)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Repeater: ^7a powerful weapon with a rapid fire and a plasma bomb. Normal fire shoots the rapid fire, and causes %d damage. Alt fire fires the plasma bomb and causes %d damage. At level 2 causes 25 per cent more damage and alternate fire is more accurate\n\"", 50, 50) );
					if (i == 25)
						trap->SendServerCommand( ent-g_entities, va("print \"^3DEMP2: ^7a very powerful weapon against machine npc and some vehicles, causing more damage to them and stunning them. Normal fire causes %d damage and alt fire can be charged and causes %d damage. At level 2 causes 25 per cent more damage\n\"", 50, 50) );
					if (i == 26)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Flechette: ^7this weapon is similar to a shotgun. Normal fire causes %d damage. Alt fire shoots 2 bombs and causes %d damage. At level 2 causes 25 per cent more damage\n\"", 50, 50) );
					if (i == 27)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Rocket Launcher: ^7a powerful explosive weapon. Normal fire shoots a rocket causing %d damage. Alt fire shoots a homing missile. At level 2 causes 25 per cent more damage\n\"", 50) );
					if (i == 28)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Concussion Rifle: ^7it shoots a powerful shot that has a big damage area. Alt fire shoots a ray similar to disruptor shots, but it can go through force fields and can throw the enemy on the ground. Normal fire causes %d damage and alt fire causes %d damage. At level 2 causes 25 per cent more damage\n\"", 50, 50) );
					if (i == 29)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Bryar Pistol: ^7very similar to the blaster pistol, but this one has a better fire rate with normal shot. Causes %d damage. At level 2 causes 25 per cent more damage\n\"", 50) );
					if (i == 30)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Melee: ^7allows you to attack with your fists and legs. You can punch, kick or do a special melee attack by holding both Attack and Alt Attack buttons (usually the mouse buttons). At level 0, melee attacks cause only half normal damage. Right hand punch causes %d normal damage, left hand punch causes %d normal damage and kick causes %d damage at level 1\n\"", 50, 50, (50 * 2)) );
					if (i == 31)
						trap->SendServerCommand( ent-g_entities, "print \"^3Max Shield: ^7The max shield (armor) the player can have. Each level increases 20 per cent of max shield the player can have\n\"" );
					if (i == 32)
						trap->SendServerCommand( ent-g_entities, "print \"^3Shield Strength: ^7Increases your shield resistance to damage. Each level increases your shield resistance by 10 per cent\n\"" );
					if (i == 33)
						trap->SendServerCommand( ent-g_entities, "print \"^3Health Strength: ^7Increases your health resistance to damage. Each level increases your health resistance by 10 per cent\n\"" );
					if (i == 34)
						trap->SendServerCommand( ent-g_entities, "print \"^3Grapple Hook: ^7the famous grapple hook, it allows you to hook walls and other things taking you to there. To use it, press the Lightsaber Style key (default L) when you have melee\n\"" );
					if (i == 35)
						trap->SendServerCommand( ent-g_entities, "print \"^3Jetpack: ^7the jetpack, used by Boba Fett. Allows you to fly. To use it, jump and press the Use key (usually R) while in the middle of the jump. Each level uses less fuel, allowing you to fly for a longer time\n\"" );
					if (i == 36)
						trap->SendServerCommand( ent-g_entities, "print \"^3Sense Health: ^7allows you to see someone health and shield, including npcs. To use it, when you are near a player or npc, use ^3Sense ^7force power\n\"" );
					if (i == 37)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Shield Heal: ^7recovers 4 shield at level 1, 8 shield at level 2 and 12 shield at level 3. To use it, use Heal force power when you have full HP. This skill uses %d force power\n\"", zyk_max_force_power.integer/2) );
					if (i == 38)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Team Shield Heal: ^7recovers 3 shield at level 1, 6 shield at level 2 and 9 shield at level 3 to players near you. To use it, when near players, use Team Heal force power. It will heal their shield after they have full HP\n\"") );
					if (i == 39)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Mind Control: ^7allows you to control every action of a player or npc. To use it, when near a player or npc, use Mind Trick force power, if you have Mind Control\n\"") );
					if (i == 40)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Blaster Pack: ^7used as ammo for Blaster Pistol, Bryar Pistol and E11 Blaster Rifle. You can carry up to %d ammo\n\"",zyk_max_blaster_pack_ammo.integer) );
					if (i == 41)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Power Cell: ^7used as ammo for Disruptor, Bowcaster and DEMP2. You can carry up to %d ammo\n\"",zyk_max_power_cell_ammo.integer) );
					if (i == 42)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Metallic Bolt: ^7used as ammo for Repeater, Flechette and Concussion Rifle. You can carry up to %d ammo\n\"",zyk_max_metal_bolt_ammo.integer) );
					if (i == 43)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Rockets: ^7used as ammo for Rocket Launcher. You can carry up to %d ammo\n\"",zyk_max_rocket_ammo.integer) );
					if (i == 44)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Thermal Detonator: ^7the famous detonator used by Leia in Ep 6 at the Jabba Palace. Normal fire throws it, which explodes after some seconds. Alt fire throws it and it explodes as soon as it touches something. Normal fire causes %d damage and alt fire causes %d damage\n\"", 50, 50) );
					if (i == 45)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Trip Mine: ^7a mine that can be planted somewhere. Normal fire plants a mine with a laser that when touched makes the mine explode, causing %d damage. Alt fire plants proximity mines\n\"", 50) );
					if (i == 46)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Det Pack: ^7a very powerful explosive, which you can detonate remotely with the alt fire button. Causes %d damage\n\"", 50) );
					if (i == 47)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Binoculars: ^7this item allows you to see distant things better with its zoom.\n\"") );
					if (i == 48)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Bacta Canister: ^7allows you to recover 25 HP. At level 2 recovers 75 HP\n\"") );
					if (i == 49)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Sentry Gun: ^7after placed on the ground, shoots at any nearby enemy\n\"") );
					if (i == 50)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Seeker Drone: ^7a flying ball that flies around you, shooting anyone in its range\n\"") );
					if (i == 51)
						trap->SendServerCommand( ent-g_entities, va("print \"^3E-Web: ^7allows you to shoot at people with it, it has a good fire rate\n\"") );
					if (i == 52)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Big Bacta: ^7allows you to recover 50 HP at level 1 and 150 HP at level 2\n\"") );
					if (i == 53)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Force Field: ^7a powerful shield that protects you from enemy attacks, it can resist a lot against any weapon. At level 2, the force field has double HP\n\"") );
					if (i == 54)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Cloak Item: ^7makes you almost invisible to players and invisible to npcs. At level 2, also allows you to cloak your vehicle by pressing the Lightsaber Style key (default L)\n\"") );
					if (i == 55)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Force Power: ^7increases the max force power you have. Necessary to allow you to use force powers and force-based skills\n\"") );
					if (i == 56)
						trap->SendServerCommand( ent-g_entities, va("print \"^3Improvements:\n^7Free Warrior gets more damage and more resistance to damage\nForce User gets more saber damage and force regens faster\nBounty Hunter gets more gun damage, max ammo, credits in battle, jetpack fuel, seeker drone damage, sentry gun damage and health, and E-Web damage and health\nArmored Soldier gets more resistance to damage\nMonk gets more run speed, melee damage, melee attack speed and health resistance\nStealth Attacker gets more gun damage and more resistance to electric attacks\nDuelist gets more saber and melee damage\n\"") );
				}
				else if (Q_stricmp( arg1, "+" ) == 0)
				{
					trap->SendServerCommand( ent-g_entities, va("print \"^3Light Power: ^7Regens 1 hp per second. Regens shield if hp is full. You must finish ^5Light Quest ^7to have it\n\"") );
				}
				else if (Q_stricmp( arg1, "x" ) == 0)
				{
					trap->SendServerCommand( ent-g_entities, va("print \"^3Dark Power: ^7Increases damage by 15 per cent. You must finish ^1Dark Quest ^7to have it\n\"") );
				}
				else if (Q_stricmp( arg1, "*" ) == 0)
				{
					trap->SendServerCommand( ent-g_entities, va("print \"^3Eternity Power: ^7Absorbs 15 per cent of damage. You must finish the ^3Eternity Quest ^7to have it\n\"") );
				}
				else if (Q_stricmp( arg1, "#" ) == 0)
				{
					trap->SendServerCommand( ent-g_entities, va("print \"^3Universe Power: ^7Increases your Max HP and Max Shield. You must defeat the ^2Guardian of Universe ^7to have it\n\"") );
				}
				else if (Q_stricmp( arg1, "!" ) == 0)
				{
					if (ent->client->pers.universe_quest_progress < NUMBER_OF_UNIVERSE_QUEST_OBJECTIVES)
					{
						trap->SendServerCommand( ent-g_entities, va("print \"^3Ultimate Power: ^7You must finish the ^2Universe Quest ^7to have this power\n\"") );
					}
					else
					{
						if (ent->client->pers.universe_quest_counter & (1 << 0))
							trap->SendServerCommand( ent-g_entities, va("print \"^3Ultimate Power: ^7knocks everyone near you down for some seconds\n\"") );
						else if (ent->client->pers.universe_quest_counter & (1 << 1))
							trap->SendServerCommand( ent-g_entities, va("print \"^3Ultimate Power: ^7This skill uses Healing Water, Flaming Burst, Earthquake and Blowing Wind at the same time\n\"") );
						else if (ent->client->pers.universe_quest_counter & (1 << 2))
							trap->SendServerCommand( ent-g_entities, va("print \"^3Ultimate Power: ^7This skill causes high damage, electrifies the enemies and throws them in the ground\n\"") );
						else if (ent->client->pers.universe_quest_counter & (1 << 3))
							trap->SendServerCommand( ent-g_entities, va("print \"^3Ultimate Power: ^7This skill paralyzes enemies for some seconds\n\"") );
					}
				}
				else
				{
					trap->SendServerCommand( ent-g_entities, "print \"Invalid skill number.\n\"" );
				}
			}
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"This command requires no option or just 1 option.\n\"" );
		}
	}
	else if (ent->client->sess.amrpgmode == 1)
	{
		trap->SendServerCommand( ent-g_entities, "print \"\n^2Admin-Only Mode commands\n\n^3/new [login] [password]: ^7creates a new account.\n^3/login [login] [password]: ^7loads the account of the player.\n^3/playermode: ^7switches between the ^2Admin-Only Mode ^7and the ^2RPG Mode^7.\n^3/adminlist: ^7lists admin commands.\n^3/adminup [player id or name] [admin command number]: ^7gives the player a new admin command.\n^3/admindown [player id or name] [admin command number]: ^7removes an admin command from the player.\n^3/settings: ^7turn on or off player settings.\n^3/changepassword <new_password>: ^7changes the account password.\n^3/entity_system: ^7shows Entity System commands.\n^3/logout: ^7logs out the account.\n\n\"" );
	}
	else
	{
		trap->SendServerCommand( ent-g_entities, "print \"\n^1Account System\n\n^7The account system has 2 modes:\n^2Admin-Only Mode: ^7allows you to use admin commands\n^2RPG Mode: ^7allows you to use admin commands and to play the Level System\n\n^7Create a new account with ^3/new <login> <password>\n^7where login and password are of your choice.\n\n\"" );
	}
}

/*
==================
Cmd_CallSeller_f
==================
*/
extern gentity_t *NPC_SpawnType( gentity_t *ent, char *npc_type, char *targetname, qboolean isVehicle );
void Cmd_CallSeller_f( gentity_t *ent ) {
	gentity_t *npc_ent = NULL;

	if (ent->client->pers.guardian_mode == 14)
	{
		trap->SendServerCommand( ent-g_entities, "chat \"You are in the Sacred Dimension. Cannot spawn the seller here.\"");
		return;
	}

	npc_ent = NPC_SpawnType(ent,"jawa_seller",NULL,qfalse);
	if (npc_ent)
	{
		npc_ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_JETPACK);
		trap->SendServerCommand( ent-g_entities, "chat \"^3Jawa Seller: ^7Hello, friend! I have some products to sell.\"");
	}
	else
	{
		trap->SendServerCommand( ent-g_entities, va("chat \"%s: ^7The seller couldn't come...\"", ent->client->pers.netname));
	}
}

/*
==================
Cmd_Stuff_f
==================
*/
void Cmd_Stuff_f( gentity_t *ent ) {
	if (trap->Argc() == 1)
	{ // zyk: shows the categories of stuff
		trap->SendServerCommand( ent-g_entities, "print \"\n^7Use ^2/stuff <category> ^7to buy or sell stuff\nThe Category may be ^3ammo^7, ^3items^7, ^3weapons ^7or ^3upgrades\n^7Use ^3/stuff <number> ^7to see info about the item\n\n^7Use ^2/buy <number> ^7to buy or ^2/sell <number> ^7to sell\n\n\"");
		return;
	}
	else
	{
		char arg1[1024];
		int i = 0;

		trap->Argv(1, arg1, sizeof( arg1 ));
		i = atoi(arg1);

		if (Q_stricmp(arg1, "ammo" ) == 0)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^31 - Blaster Pack: ^7Buy: 30 - Sell: 10\n^32 - Power Cell: ^7Buy: 50 - Sell: 15\n^33 - Metal Bolts: ^7Buy: 70 - Sell: 20\n^34 - Rockets: ^7Buy: 100 - Sell: 30\n^35 - Thermals: ^7Buy: 120 - Sell: 35\n^36 - Trip Mines: ^7Buy: 150 - Sell: 40\n^37 - Det Packs: ^7Buy: 220 - Sell: 45\n^330 - Flame Thrower Fuel: ^7Buy: 200 - Sell: ^1no^7\n\n\"");
		}
		else if (Q_stricmp(arg1, "items" ) == 0)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^39 - Shield Booster: ^7Buy: 250 - Sell: ^1no\n^310 - Sentry Gun: ^7Buy: 200 - Sell: 60\n^311 - Seeker Drone: ^7Buy: 230 - Sell: 65\n^312 - Big Bacta: ^7Buy: 300 - Sell: 70\n^313 - Force Field: ^7Buy: 400 - Sell: 80\n^314 - Ysalamiri: ^7Buy: 200 - Sell: 50\n^331 - Jetpack Fuel: ^7Buy: 300 - Sell: ^1no\n^334 - Bacta Canister: ^7Buy: 100 - Sell: ^1no\n^335 - E-Web: ^7Buy: 150 - Sell: ^1no\n^338 - Binoculars: ^7Buy: 10 - Sell: ^1no^7\n\n\"");
		}
		else if (Q_stricmp(arg1, "weapons" ) == 0)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^317 - E11 Blaster Rifle: ^7Buy: 100 - Sell: 50\n^318 - Disruptor: ^7Buy: 120 - Sell: 60\n^319 - Repeater: ^7Buy: 150 - Sell: 70\n^320 - Rocket Launcher: ^7Buy: 200 - Sell: 100\n^321 - Bowcaster: ^7Buy: 110 - Sell: 50\n^322 - Blaster Pistol: ^7Buy: 90 - Sell: 45\n^323 - Flechette: ^7Buy: 170 - Sell: 90\n^324 - Concussion Rifle: ^7Buy: 300 - Sell: 150\n^332 - Stun Baton: ^7Buy: 20 - Sell: ^1no\n^336 - DEMP2: ^7Buy: 150 - Sell: ^1no\n^337 - Bryar Pistol: ^7Buy: 90 - Sell: ^1no^7\n\n\"");
		}
		else if (Q_stricmp(arg1, "upgrades" ) == 0)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^38 - Stealth Attacker Upgrade: ^7Buy: 5000 - Sell: ^1no\n^315 - Impact Reducer: ^7Buy: 8000 - Sell: 1500\n^316 - Flame Thrower Upgrade: ^7Buy: 5000 - Sell: 3000\n^325 - Power Cell Weapons Upgrade: ^7Buy: 2000 - Sell: ^1no\n^326 - Blaster Pack Weapons Upgrade: ^7Buy: 1500 - Sell: ^1no\n^327 - Metal Bolts Weapons Upgrade: ^7Buy: 2500 - Sell: ^1no\n^328 - Rocket Upgrade: ^7Buy: 3000 - Sell: ^1no\n^329 - Bounty Hunter Upgrade: ^7Buy: 5000 - Sell: ^1no\n^333 - Stun Baton Upgrade: ^7Buy: 1000 - Sell: ^1no\n^339 - Armored Soldier Upgrade: ^7Buy: 5000 - Sell: ^1no^7\n\n\"");
		}
		else if (i == 1)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Blaster Pack: ^7recovers 100 ammo of E11 Blaster Rifle, Blaster Pistol and Bryar Pistol weapons\n\n\"");
		}
		else if (i == 2)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Power Cell: ^7recovers 100 ammo of Disruptor, Bowcaster and DEMP2 weapons\n\n\"");
		}
		else if (i == 3)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Metal Bolts: ^7recovers 100 ammo of Repeater, Flechette and Concussion Rifle weapons\n\n\"");
		}
		else if (i == 4)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Rockets: ^7recovers 10 ammo of Rocket Launcher weapon\n\n\"");
		}
		else if (i == 5)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Thermals: ^7recovers 4 ammo of thermals\n\n\"");
		}
		else if (i == 6)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Trip Mines: ^7recovers 3 ammo of trip mines\n\n\"");
		}
		else if (i == 7)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Det Packs: ^7recovers 2 ammo of det packs\n\n\"");
		}
		else if (i == 8)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Stealth Attacker Upgrade: ^7Stealth Attacker will be invulnerable to electric attacks and will have 5 per cent more damage. This upgrade protects from the stun baton 2/3 and 3/3 knock down and also from losing guns to force pull\n\n\"");
		}
		else if (i == 9)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Shield Booster: ^7recovers 50 shield\n\n\"");
		}
		else if (i == 10)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Sentry Gun: ^7portable gun which is placed in the ground and shoots nearby enemies\n\n\"");
		}
		else if (i == 11)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Seeker Drone: ^7portable remote drone that shoots enemies at sight\n\n\"");
		}
		else if (i == 12)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Big Bacta: ^7recovers 50 HP. If player has Big Bacta 2/2 skill, recovers 150 HP\n\n\"");
		}
		else if (i == 13)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Force Field: ^7creates a force field wall in front of the player that can hold almost any attack, except the concussion rifle alternate fire, which can get through\n\n\"");
		}
		else if (i == 14)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Ysalamiri: ^7disables the player force powers but also protects the player from enemy force powers\n\n\"");
		}
		else if (i == 15)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Impact Reducer: ^7reduces the knockback of some weapons attacks by 80 per cent\n\n\"");
		}
		else if (i == 16)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Flame Thrower Upgrade: ^7reduces flame thrower fuel usage to 50 per cent\n\n\"");
		}
		else if (i == 17)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3E11 Blaster Rifle: ^7Rifle that is used by the stormtroopers. Uses blaster pack ammo\n\n\"");
		}
		else if (i == 18)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Disruptor: ^7Sniper rifle which can desintegrate the enemy. Uses power cell ammo\n\n\"");
		}
		else if (i == 19)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Repeater: ^7imperial weapon that shoots orbs and a plasma bomb with alt fire. Uses metal bolts ammo\n\n\"");
		}
		else if (i == 20)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Rocket Launcher: ^7weapon that shoots rockets and a homing missile with alternate fire. Uses rockets ammo\n\n\"");
		}
		else if (i == 21)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Bowcaster: ^7weapon that shoots green bolts, normal fire can be charged, and alt fire shoots a bouncing bolt. Uses power cell ammo\n\n\"");
		}
		else if (i == 22)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Blaster Pistol: ^7pistol that can shoot a charged shot with alt fire. Uses blaster pack ammo\n\n\"");
		}
		else if (i == 23)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Flechette: ^7it is the shotgun of the game, and can shoot 2 bombs with alt fire. Uses metal bolts ammo\n\n\"");
		}
		else if (i == 24)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Concussion Rifle: ^7powerful weapon, alt fire can shoot a beam that gets through force fields. Uses metal bolts ammo\n\n\"");
		}
		else if (i == 25)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Power Cell Weapons Upgrade: ^7increases damage and other features of weapons that use power cell ammo\n\n\"");
		}
		else if (i == 26)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Blaster Pack Weapons Upgrade: ^7increases damage and other features of weapons that use blaster pack ammo\n\n\"");
		}
		else if (i == 27)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Metal Bolts Weapons Upgrade: ^7increases damage and other features of weapons that use metal bolts ammo\n\n\"");
		}
		else if (i == 28)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Rocket Upgrade: ^7increases damage of rocket launcher which uses rockets as ammo\n\n\"");
		}
		else if (i == 29)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Bounty Hunter Upgrade: ^7increases Bounty Hunter health resistance to damage by 5 per cent and shield resistance by 5 per cent. Seeker Drone lasts 30 seconds more. Allows placing more sentry guns\n\n\"");
		}
		else if (i == 30)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Flame Thrower Fuel: ^7recovers all fuel of the flame thrower\n\n\"");
		}
		else if (i == 31)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Jetpack Fuel: ^7recovers all fuel of the jetpack\n\n\"");
		}
		else if (i == 32)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Stun Baton: ^7weapon that fires a small electric charge\n\n\"");
		}
		else if (i == 33)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Stun Baton Upgrade: ^7allows stun baton to open any door, including locked ones, move elevators, and move or destroy other objects\n\n\"");
		}
		else if (i == 34)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Bacta Canister: ^7recovers 25 HP. If player has bacta canister 2/2 skill, recovers 75 HP\n\n\"");
		}
		else if (i == 35)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3E-Web: ^7portable emplaced gun\n\n\"");
		}
		else if (i == 36)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3DEMP2: ^7fires an electro magnetic pulse that causes bonus damage against droids. Uses power cell ammo\n\n\"");
		}
		else if (i == 37)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Bryar Pistol: ^7similar to blaster pistol, but has a faster fire rate with normal fire. Uses blaster pack ammo\n\n\"");
		}
		else if (i == 38)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Binoculars: ^7allows the player to see far things better with the zoom feature\n\n\"");
		}
		else if (i == 39)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\n^3Armored Soldier Upgrade: ^7Armored Soldier gets an increase of the damage resistance by 5 per cent and cuts flame thrower fuel usage by half\n\n\"");
		}
	}
}

/*
==================
Cmd_Buy_f
==================
*/
void Cmd_Buy_f( gentity_t *ent ) {
	char arg1[MAX_STRING_CHARS];
	int value = 0;
	int item_costs[39] = {30,50,70,100,120,150,220,5000,250,200,230,300,400,200,8000,5000,100,120,150,200,110,90,170,300,2000,1500,2500,3000,5000,200,300,20,1000,100,150,150,90,10,5000};

	if (trap->Argc() == 1)
	{
		trap->SendServerCommand( ent-g_entities, "print \"You must specify a product number.\n\"" );
		return;
	}

	trap->Argv(1, arg1, sizeof( arg1 ));
	value = atoi(arg1);

	if (value < 1 || value > 39)
	{
		trap->SendServerCommand( ent-g_entities, "print \"Invalid product number.\n\"" );
		return;
	}
	else
	{ // zyk: searches for the jawa to see if we are near him to buy or sell to him
		gentity_t *jawa_ent = NULL;
		int j = 0, found = 0;

		for (j = MAX_CLIENTS; j < level.num_entities; j++)
		{
			jawa_ent = &g_entities[j];

			if (jawa_ent && jawa_ent->client && jawa_ent->NPC && jawa_ent->health > 0 && Q_stricmp( jawa_ent->NPC_type, "jawa_seller" ) == 0 && (int)Distance(ent->client->ps.origin, jawa_ent->client->ps.origin) < 90)
			{
				found = 1;
				break;
			}
		}

		if (found == 0)
		{
			trap->SendServerCommand( ent-g_entities, "print \"You must be near the jawa seller to buy from him.\n\"" );
			return;
		}
	}

	// zyk: class validations. Some items require certain conditions to be bought
	if (ent->client->pers.rpg_class == 1 && ((value >= 1 && value <= 7) || (value >= 10 && value <= 13) || (value >= 17 && value <= 24) || (value >= 34 && value <= 38)))
	{
		trap->SendServerCommand( ent-g_entities, "print \"Force User can't buy this item.\n\"" );
		return;
	}
	else if (ent->client->pers.rpg_class == 4 && ((value >= 1 && value <= 7) || (value >= 10 && value <= 13) || (value >= 17 && value <= 24) || (value >= 34 && value <= 38)))
	{
		trap->SendServerCommand( ent-g_entities, "print \"Monk can't buy this item.\n\"" );
		return;
	}
	else if (ent->client->pers.rpg_class == 6 && ((value >= 1 && value <= 7) || (value >= 10 && value <= 13) || (value >= 17 && value <= 24) || (value >= 34 && value <= 38)))
	{
		trap->SendServerCommand( ent-g_entities, "print \"Duelist can't buy this item.\n\"" );
		return;
	}

	// zyk: general validations. Some items require certain conditions to be bought
	if (value == 8 && ent->client->pers.secrets_found & (1 << 7))
	{
		trap->SendServerCommand( ent-g_entities, "print \"You already have the Stealth Attacker Upgrade.\n\"" );
		return;
	}
	else if (value == 15 && ent->client->pers.secrets_found & (1 << 9))
	{
		trap->SendServerCommand( ent-g_entities, "print \"You already have the Impact Reducer.\n\"" );
		return;
	}
	else if (value == 16 && ent->client->pers.secrets_found & (1 << 10))
	{
		trap->SendServerCommand( ent-g_entities, "print \"You already have the Flame Thrower Upgrade.\n\"" );
		return;
	}
	else if (value == 25 && ent->client->pers.secrets_found & (1 << 11))
	{
		trap->SendServerCommand( ent-g_entities, "print \"You already have the Power Cell Weapons Upgrade.\n\"" );
		return;
	}
	else if (value == 26 && ent->client->pers.secrets_found & (1 << 12))
	{
		trap->SendServerCommand( ent-g_entities, "print \"You already have the Blaster Pack Weapons Upgrade.\n\"" );
		return;
	}
	else if (value == 27 && ent->client->pers.secrets_found & (1 << 13))
	{
		trap->SendServerCommand( ent-g_entities, "print \"You already have the Metal Bolts Weapons Weapons Upgrade.\n\"" );
		return;
	}
	else if (value == 28 && ent->client->pers.secrets_found & (1 << 14))
	{
		trap->SendServerCommand( ent-g_entities, "print \"You already have the Rocket Upgrade.\n\"" );
		return;
	}
	else if (value == 29 && ent->client->pers.secrets_found & (1 << 1))
	{
		trap->SendServerCommand( ent-g_entities, "print \"You already have the Bounty Hunter Upgrade.\n\"" );
		return;
	}
	else if (value == 33 && ent->client->pers.secrets_found & (1 << 15))
	{
		trap->SendServerCommand( ent-g_entities, "print \"You already have the Stun Baton Upgrade.\n\"" );
		return;
	}
	else if (value == 39 && ent->client->pers.secrets_found & (1 << 16))
	{
		trap->SendServerCommand( ent-g_entities, "print \"You already have the Armored Soldier Upgrade.\n\"" );
		return;
	}

	// zyk: buying the item if player has enough credits
	if (ent->client->pers.credits >= item_costs[value-1])
	{
		if (value == 1)
		{
			Add_Ammo(ent,AMMO_BLASTER,100);
		}
		else if (value == 2)
		{
			Add_Ammo(ent,AMMO_POWERCELL,100);
		}
		else if (value == 3)
		{
			Add_Ammo(ent,AMMO_METAL_BOLTS,100);
		}
		else if (value == 4)
		{
			Add_Ammo(ent,AMMO_ROCKETS,10);
		}
		else if (value == 5)
		{
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_THERMAL);
			Add_Ammo(ent,AMMO_THERMAL,4);
		}
		else if (value == 6)
		{
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_TRIP_MINE);
			Add_Ammo(ent,AMMO_TRIPMINE,3);
		}
		else if (value == 7)
		{
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_DET_PACK);
			Add_Ammo(ent,AMMO_DETPACK,2);
		}
		else if (value == 8)
		{
			ent->client->pers.secrets_found |= (1 << 7);
		}
		else if (value == 9)
		{
			if (ent->client->ps.stats[STAT_ARMOR] < ent->client->pers.max_rpg_shield)
			{ // zyk: RPG Mode has the Max Shield skill that doesnt allow someone to heal shields above this value
				ent->client->ps.stats[STAT_ARMOR] += 50;
			}

			if (ent->client->ps.stats[STAT_ARMOR] > ent->client->pers.max_rpg_shield)
				ent->client->ps.stats[STAT_ARMOR] = ent->client->pers.max_rpg_shield;
		}
		else if (value == 10)
		{
			ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_SENTRY_GUN);
		}
		else if (value == 11)
		{
			ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_SEEKER);
		}
		else if (value == 12)
		{
			ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_MEDPAC_BIG);
		}
		else if (value == 13)
		{
			ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_SHIELD);
		}
		else if (value == 14)
		{
			ent->client->ps.powerups[PW_YSALAMIRI] = level.time + 60000;
		}
		else if (value == 15)
		{
			ent->client->pers.secrets_found |= (1 << 9);
		}
		else if (value == 16)
		{
			ent->client->pers.secrets_found |= (1 << 10);
		}
		else if (value == 17)
		{
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_BLASTER);
		}
		else if (value == 18)
		{
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_DISRUPTOR);
		}
		else if (value == 19)
		{
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_REPEATER);
		}
		else if (value == 20)
		{
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_ROCKET_LAUNCHER);
		}
		else if (value == 21)
		{
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_BOWCASTER);
		}
		else if (value == 22)
		{
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_BRYAR_PISTOL);
		}
		else if (value == 23)
		{
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_FLECHETTE);
		}
		else if (value == 24)
		{
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_CONCUSSION);
		}
		else if (value == 25)
		{
			ent->client->pers.secrets_found |= (1 << 11);
		}
		else if (value == 26)
		{
			ent->client->pers.secrets_found |= (1 << 12);
		}
		else if (value == 27)
		{
			ent->client->pers.secrets_found |= (1 << 13);
		}
		else if (value == 28)
		{
			ent->client->pers.secrets_found |= (1 << 14);
		}
		else if (value == 29)
		{
			ent->client->pers.secrets_found |= (1 << 1);
		}
		else if (value == 30)
		{
			ent->client->ps.cloakFuel = 100;
		}
		else if (value == 31)
		{
			ent->client->pers.jetpack_fuel = MAX_JETPACK_FUEL;
			ent->client->ps.jetpackFuel = 100;
		}
		else if (value == 32)
		{
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_STUN_BATON);
		}
		else if (value == 33)
		{
			ent->client->pers.secrets_found |= (1 << 15);
		}
		else if (value == 34)
		{
			ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_MEDPAC);
		}
		else if (value == 35)
		{
			ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_EWEB);
		}
		else if (value == 36)
		{
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_DEMP2);
		}
		else if (value == 37)
		{
			ent->client->ps.stats[STAT_WEAPONS] |= (1 << WP_BRYAR_OLD);
		}
		else if (value == 38)
		{
			ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_BINOCULARS);
		}
		else if (value == 39)
		{
			ent->client->pers.secrets_found |= (1 << 16);
		}

		G_Sound(ent, CHAN_AUTO, G_SoundIndex("sound/player/pickupenergy.wav"));

		ent->client->pers.credits -= item_costs[value-1];
		save_account(ent);

		trap->SendServerCommand( ent-g_entities, va("chat \"^3Jawa Seller: ^7Thanks %s^7!\n\"",ent->client->pers.netname) );
	}
	else
	{
		trap->SendServerCommand( ent-g_entities, va("chat \"^3Jawa Seller: ^7%s^7, my products are not free! Give me the money!\n\"",ent->client->pers.netname) );
		return;
	}
}

	/*
==================
Cmd_Sell_f
==================
*/
void Cmd_Sell_f( gentity_t *ent ) {
	char arg1[MAX_STRING_CHARS];
	int value = 0;
	int sold = 0;
	int items_costs[24] = {10,15,20,30,35,40,45,0,0,60,65,70,80,50,1500,3000,50,60,70,100,50,45,90,150};

	if (trap->Argc() == 1)
	{
		trap->SendServerCommand( ent-g_entities, "print \"You must specify a product number.\n\"" );
		return;
	}

	trap->Argv(1, arg1, sizeof( arg1 ));
	value = atoi(arg1);

	if (value < 1 || value > 24)
	{
		trap->SendServerCommand( ent-g_entities, "print \"Invalid product number.\n\"" );
		return;
	}
	else
	{ // zyk: searches for the jawa to see if we are near him to buy or sell to him
		gentity_t *jawa_ent = NULL;
		int j = 0, found = 0;

		for (j = MAX_CLIENTS; j < level.num_entities; j++)
		{
			jawa_ent = &g_entities[j];

			if (jawa_ent && jawa_ent->client && jawa_ent->NPC && jawa_ent->health > 0 && Q_stricmp( jawa_ent->NPC_type, "jawa_seller" ) == 0 && (int)Distance(ent->client->ps.origin, jawa_ent->client->ps.origin) < 90)
			{
				found = 1;
				break;
			}
		}

		if (found == 0)
		{
			trap->SendServerCommand( ent-g_entities, "print \"You must be near the jawa seller to sell to him.\n\"" );
			return;
		}
	}

	if (value == 1 && ent->client->ps.ammo[AMMO_BLASTER] >= 100)
	{
		ent->client->ps.ammo[AMMO_BLASTER] -= 100;
		sold = 1;
	}
	else if (value == 2 && ent->client->ps.ammo[AMMO_POWERCELL] >= 100)
	{
		ent->client->ps.ammo[AMMO_POWERCELL] -= 100;
		sold = 1;
	}
	else if (value == 3 && ent->client->ps.ammo[AMMO_METAL_BOLTS] >= 100)
	{
		ent->client->ps.ammo[AMMO_METAL_BOLTS] -= 100;
		sold = 1;
	}
	else if (value == 4 && ent->client->ps.ammo[AMMO_ROCKETS] >= 10)
	{
		ent->client->ps.ammo[AMMO_ROCKETS] -= 10;
		sold = 1;
	}
	else if (value == 5 && ent->client->ps.ammo[AMMO_THERMAL] >= 4)
	{
		ent->client->ps.ammo[AMMO_THERMAL] -= 4;
		sold = 1;
	}
	else if (value == 6 && ent->client->ps.ammo[AMMO_TRIPMINE] >= 3)
	{
		ent->client->ps.ammo[AMMO_TRIPMINE] -= 3;
		sold = 1;
	}
	else if (value == 7 && ent->client->ps.ammo[AMMO_DETPACK] >= 2)
	{
		ent->client->ps.ammo[AMMO_DETPACK] -= 2;
		sold = 1;
	}
	else if (value == 10 && ent->client->ps.stats[STAT_HOLDABLE_ITEMS] & (1 << HI_SENTRY_GUN))
	{
		ent->client->ps.stats[STAT_HOLDABLE_ITEMS] &= ~(1 << HI_SENTRY_GUN);
		sold = 1;
	}
	else if (value == 11 && ent->client->ps.stats[STAT_HOLDABLE_ITEMS] & (1 << HI_SEEKER))
	{
		ent->client->ps.stats[STAT_HOLDABLE_ITEMS] &= ~(1 << HI_SEEKER);
		sold = 1;
	}
	else if (value == 12 && ent->client->ps.stats[STAT_HOLDABLE_ITEMS] & (1 << HI_MEDPAC_BIG))
	{
		ent->client->ps.stats[STAT_HOLDABLE_ITEMS] &= ~(1 << HI_MEDPAC_BIG);
		sold = 1;
	}
	else if (value == 13 && ent->client->ps.stats[STAT_HOLDABLE_ITEMS] & (1 << HI_SHIELD))
	{
		ent->client->ps.stats[STAT_HOLDABLE_ITEMS] &= ~(1 << HI_SHIELD);
		sold = 1;
	}
	else if (value == 14 && ent->client->ps.powerups[PW_YSALAMIRI])
	{
		ent->client->ps.powerups[PW_YSALAMIRI] = 0;
		sold = 1;
	}
	else if (value == 15 && ent->client->pers.secrets_found & (1 << 9))
	{
		ent->client->pers.secrets_found &= ~(1 << 9);
		sold = 1;
	}
	else if (value == 16 && ent->client->pers.secrets_found & (1 << 10))
	{
		ent->client->pers.secrets_found &= ~(1 << 10);
		sold = 1;
	}
	else if (value == 17 && (ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_BLASTER)))
	{
		ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_BLASTER);
		if (ent->client->ps.weapon == WP_BLASTER)
			ent->client->ps.weapon = WP_MELEE;
		sold = 1;
	}
	else if (value == 18 && (ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_DISRUPTOR)))
	{
		ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_DISRUPTOR);
		if (ent->client->ps.weapon == WP_DISRUPTOR)
			ent->client->ps.weapon = WP_MELEE;
		sold = 1;
	}
	else if (value == 19 && (ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_REPEATER)))
	{
		ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_REPEATER);
		if (ent->client->ps.weapon == WP_REPEATER)
			ent->client->ps.weapon = WP_MELEE;
		sold = 1;
	}
	else if (value == 20 && (ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_ROCKET_LAUNCHER)))
	{
		ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_ROCKET_LAUNCHER);
		if (ent->client->ps.weapon == WP_ROCKET_LAUNCHER)
			ent->client->ps.weapon = WP_MELEE;
		sold = 1;
	}
	else if (value == 21 && (ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_BOWCASTER)))
	{
		ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_BOWCASTER);
		if (ent->client->ps.weapon == WP_BOWCASTER)
			ent->client->ps.weapon = WP_MELEE;
		sold = 1;
	}
	else if (value == 22 && (ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_BRYAR_PISTOL)))
	{
		ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_BRYAR_PISTOL);
		if (ent->client->ps.weapon == WP_BRYAR_PISTOL)
			ent->client->ps.weapon = WP_MELEE;
		sold = 1;
	}
	else if (value == 23 && (ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_FLECHETTE)))
	{
		ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_FLECHETTE);
		if (ent->client->ps.weapon == WP_FLECHETTE)
			ent->client->ps.weapon = WP_MELEE;
		sold = 1;
	}
	else if (value == 24 && (ent->client->ps.stats[STAT_WEAPONS] & (1 << WP_CONCUSSION)))
	{
		ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << WP_CONCUSSION);
		if (ent->client->ps.weapon == WP_CONCUSSION)
			ent->client->ps.weapon = WP_MELEE;
		sold = 1;
	}

	if (sold == 1)
	{
		add_credits(ent,items_costs[value-1]);
		save_account(ent);
	}
			
	if (value == 8 || value == 9)
	{
		trap->SendServerCommand( ent-g_entities, va("chat \"^3Jawa Seller: ^7This item can't be sold, %s^7!\n\"",ent->client->pers.netname) );
	}
	else if (sold == 1)
	{
		trap->SendServerCommand( ent-g_entities, va("chat \"^3Jawa Seller: ^7Thanks %s^7! :D\n\"",ent->client->pers.netname) );
	}
	else
	{
		trap->SendServerCommand( ent-g_entities, va("chat \"^3Jawa Seller: ^7You don't have this item.\n\"") );
	}
}

/*
==================
Cmd_ChangePassword_f
==================
*/
void Cmd_ChangePassword_f( gentity_t *ent ) {
	int i = 0;
	char new_content[1024];
	char content[1024];
	char login[64];
	char arg1[1024];
	FILE *account_file;
	FILE *new_account_file;
	strcpy(login,"");
	strcpy(content,"");
	strcpy(new_content,"");

	if (ent->client->sess.sessionTeam != TEAM_SPECTATOR)
	{
		trap->SendServerCommand( ent-g_entities, "print \"You must be at Spectator Mode to change password.\n\"" );
		return;
	}

	if (trap->Argc() != 2)
	{
		trap->SendServerCommand( ent-g_entities, "print \"Use ^3changepassword <new_password> ^7to change it.\n\"" );
		return;
	}

	// zyk: gets the player login
	for (i = 0; i < strlen(ent->client->sess.filename); i++)
	{
		if (ent->client->sess.filename[i] == '_' && ent->client->sess.filename[i+1] == 'S' && ent->client->sess.filename[i+2] == '_')
			break;
		else
			sprintf(login,"%s%c",login,ent->client->sess.filename[i]);
	}

	// zyk: gets the new password
	trap->Argv(1, arg1, sizeof( arg1 ));

	if (strlen(arg1) > 30)
	{
		trap->SendServerCommand( ent-g_entities, "print \"The password must have a maximum of 30 characters.\n\"" );
		return;
	}

	account_file = fopen(va("accounts/%s.txt",ent->client->sess.filename),"r");

	// zyk: reads the account file to save it in a new file
	for (i = 0; i < NUMBER_OF_LINES; i++)
	{
		fscanf(account_file,"%s",content);
		sprintf(new_content,"%s%s\n",new_content,content);
	}

	fclose(account_file);

	new_account_file = fopen(va("accounts/%s_S_%s.txt",login,arg1),"w");
	fprintf(new_account_file,"%s",new_content);
	fclose(new_account_file);

	remove(va("accounts/%s.txt",ent->client->sess.filename));

	strcpy(ent->client->sess.filename,va("%s_S_%s",login,arg1));

	trap->SendServerCommand( ent-g_entities, "print \"Your password was changed successfully.\n\"" );
}

/*
==================
Cmd_ResetAccount_f
==================
*/
void Cmd_ResetAccount_f( gentity_t *ent ) {
	char arg1[MAX_STRING_CHARS];
			
	if (ent->client->sess.sessionTeam != TEAM_SPECTATOR)
	{
		trap->SendServerCommand( ent-g_entities, "print \"You must be at Spectator Mode to reset account.\n\"" );
		return;
	}
	else if (trap->Argc() == 1)
	{
		trap->SendServerCommand( ent-g_entities, "print \"^2Choose one of the options below\n\n^3/resetaccount rpg: ^7resets your entire account except admin commands.\n^3/resetaccount quests: ^7resets your RPG quests.\n\"" );
		return;
	}

	trap->Argv( 1, arg1, sizeof( arg1 ) );

	if (Q_stricmp( arg1, "rpg") == 0)
	{
		int i = 0;

		for (i = 0; i < 18; i++)
			ent->client->pers.force_powers_levels[i] = 0;

		for (i = 0; i < 10; i++)
			ent->client->pers.weapons_levels[i] = 0;

		for (i = 0; i < 7; i++)
			ent->client->pers.ammo_levels[i] = 0;

		for (i = 0; i < 8; i++)
			ent->client->pers.holdable_items_levels[i] = 0;

		ent->client->pers.starting_shield_level = 0;
		ent->client->pers.max_rpg_shield = 0;
		ent->client->pers.jetpack_level = 0;
		ent->client->pers.playerhealth = 0;
		ent->client->pers.shield = 0;
		ent->client->pers.teamshield = 0;
		ent->client->pers.drain_shield = 0;
		ent->client->pers.stun_baton_level = 0;
		ent->client->pers.mind_control = 0;
		ent->client->pers.melee_level = 0;
		ent->client->pers.health_strength = 0;
		ent->client->pers.shield_strength = 0;
		ent->client->pers.secrets_found = 0;
		ent->client->pers.max_force_power_level = 0;
		ent->client->pers.improvements_level = 0;

		ent->client->pers.defeated_guardians = 0;
		ent->client->pers.hunter_quest_progress = 0;
		ent->client->pers.eternity_quest_progress = 0;
		ent->client->pers.universe_quest_progress = 0;
		ent->client->pers.universe_quest_counter = 0;
		ent->client->pers.player_settings = 0;

		ent->client->pers.level = 1;
		ent->client->pers.level_up_score = 0;
		ent->client->pers.skillpoints = 1;

		ent->client->pers.credits = 0;

		save_account(ent);

		trap->SendServerCommand( ent-g_entities, "print \"You are back to level 1 again.\n\"" );
	}
	else if (Q_stricmp( arg1, "quests") == 0)
	{
		ent->client->pers.defeated_guardians = 0;
		ent->client->pers.hunter_quest_progress = 0;
		ent->client->pers.eternity_quest_progress = 0;
		ent->client->pers.universe_quest_progress = 0;
		ent->client->pers.universe_quest_counter = 0;

		save_account(ent);

		trap->SendServerCommand( ent-g_entities, "print \"Your quests are reset.\n\"" );
	}
	else
	{
		trap->SendServerCommand( ent-g_entities, "print \"Invalid option.\n\"" );
	}
}

extern void zyk_TeleportPlayer( gentity_t *player, vec3_t origin, vec3_t angles );

/*
==================
Cmd_Teleport_f
==================
*/
void Cmd_Teleport_f( gentity_t *ent )
{
	char arg1[MAX_STRING_CHARS];
	char arg2[MAX_STRING_CHARS];
	char arg3[MAX_STRING_CHARS];

	if (!(ent->client->pers.bitvalue & (1 << 3)))
	{ // zyk: teleport admin command
		trap->SendServerCommand( ent-g_entities, "print \"You don't have this admin command.\n\"" );
		return;
	}

	if (ent->client->sess.amrpgmode == 2 && ent->client->pers.guardian_mode > 0)
	{
		trap->SendServerCommand( ent-g_entities, "print \"Cannot teleport while in a guardian battle.\n\"" );
		return;
	}

	if (trap->Argc() == 1)
	{
		zyk_TeleportPlayer(ent,ent->client->pers.teleport_point,ent->client->pers.teleport_angles);
	}
	else if (trap->Argc() == 2)
	{
		int client_id = -1;

		trap->Argv( 1,  arg1, sizeof( arg1 ) );

		if (Q_stricmp(arg1, "point") == 0)
		{
			VectorCopy(ent->client->ps.origin,ent->client->pers.teleport_point);
			VectorCopy(ent->client->ps.viewangles,ent->client->pers.teleport_angles);
			trap->SendServerCommand( ent-g_entities, va("print \"Marked point %s with angles %s\n\"", vtos(ent->client->pers.teleport_point), vtos(ent->client->pers.teleport_angles)) );
			return;
		}
		else
		{
			vec3_t target_origin;

			client_id = zyk_get_client( arg1 );

			if (client_id == -1)
			{
				trap->SendServerCommand( ent-g_entities, va("print \"The player was not found\n\"") );
				return;
			}

			if (g_entities[client_id].client->sess.amrpgmode > 0 && g_entities[client_id].client->pers.bitvalue & (1 << 4) && !(g_entities[client_id].client->pers.player_settings & (1 << 13)))
			{
				trap->SendServerCommand( ent-g_entities, va("print \"Target player is adminprotected\n\"") );
				return;
			}

			if (g_entities[client_id].client->sess.amrpgmode == 2 && g_entities[client_id].client->pers.guardian_mode > 0)
			{
				trap->SendServerCommand( ent-g_entities, "print \"Cannot teleport to a player in a guardian battle.\n\"" );
				return;
			}

			VectorCopy(g_entities[client_id].client->ps.origin,target_origin);
			target_origin[2] = target_origin[2] + 120;

			zyk_TeleportPlayer(ent,target_origin,g_entities[client_id].client->ps.viewangles);
		}
	}
	else if (trap->Argc() == 3)
	{
		// zyk: teleporting a player to another
		int client1_id;
		int client2_id;

		vec3_t target_origin;

		trap->Argv( 1,  arg1, sizeof( arg1 ) );
		trap->Argv( 2,  arg2, sizeof( arg2 ) );

		client1_id = zyk_get_client( arg1 );
		client2_id = zyk_get_client( arg2 );

		if (client1_id == -1)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"The player was not found\n\"") );
			return;
		}

		if (g_entities[client1_id].client->sess.amrpgmode > 0 && g_entities[client1_id].client->pers.bitvalue & (1 << 4) && !(g_entities[client1_id].client->pers.player_settings & (1 << 13)))
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Target player is adminprotected\n\"") );
			return;
		}

		if (g_entities[client1_id].client->sess.amrpgmode == 2 && g_entities[client1_id].client->pers.guardian_mode > 0)
		{
			trap->SendServerCommand( ent-g_entities, "print \"Cannot teleport to a player in a guardian battle.\n\"" );
			return;
		}

		if (client2_id == -1)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"The player was not found\n\"") );
			return;
		}

		if (g_entities[client2_id].client->sess.amrpgmode > 0 && g_entities[client2_id].client->pers.bitvalue & (1 << 4) && !(g_entities[client2_id].client->pers.player_settings & (1 << 13)))
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Target player is adminprotected\n\"") );
			return;
		}

		if (g_entities[client2_id].client->sess.amrpgmode == 2 && g_entities[client2_id].client->pers.guardian_mode > 0)
		{
			trap->SendServerCommand( ent-g_entities, "print \"Cannot teleport to a player in a guardian battle.\n\"" );
			return;
		}

		VectorCopy(g_entities[client2_id].client->ps.origin,target_origin);
		target_origin[2] = target_origin[2] + 120;

		zyk_TeleportPlayer(&g_entities[client1_id],target_origin,g_entities[client2_id].client->ps.viewangles);
	}
	else if (trap->Argc() == 4)
	{
		// zyk: teleporting to coordinates
		vec3_t target_origin;

		trap->Argv( 1,  arg1, sizeof( arg1 ) );
		trap->Argv( 2,  arg2, sizeof( arg2 ) );
		trap->Argv( 3,  arg3, sizeof( arg3 ) );

		VectorSet(target_origin,atoi(arg1),atoi(arg2),atoi(arg3));

		zyk_TeleportPlayer(ent,target_origin,ent->client->ps.viewangles);
	}
}

/*
==================
Cmd_CreditGive_f
==================
*/
void Cmd_CreditGive_f( gentity_t *ent ) {
	char arg1[MAX_STRING_CHARS];
	char arg2[MAX_STRING_CHARS];
	int client_id = 0, value = 0;

	if (trap->Argc() == 1)
	{
		trap->SendServerCommand( ent-g_entities, "print \"You must specify a player.\n\"" );
		return;
	}

	if (trap->Argc() == 2)
	{
		trap->SendServerCommand( ent-g_entities, "print \"You must specify the amount of credits.\n\"" );
		return;
	}

	trap->Argv( 1,  arg1, sizeof( arg1 ) );
	trap->Argv( 2,  arg2, sizeof( arg2 ) );

	client_id = zyk_get_client( arg1 ); 
	value = atoi(arg2);

	if (client_id == -1)
	{
		trap->SendServerCommand( ent-g_entities, va("print \"The player was not found\n\"") );
		return;
	}

	if (g_entities[client_id].client->sess.amrpgmode < 2)
	{
		trap->SendServerCommand( ent-g_entities, va("print \"The player is not in RPG Mode\n\"") );
		return;
	}

	if ((ent->client->pers.credits - value) < 0)
	{
		trap->SendServerCommand( ent-g_entities, va("print \"You don't have this amount of credits\n\"") );
		return;
	}

	add_credits(&g_entities[client_id], value);
	save_account(&g_entities[client_id]);

	remove_credits(ent, value);
	save_account(ent);

	trap->SendServerCommand( client_id, va("chat \"^3Credit System: ^7You got %d credits from %s\n\"", value, ent->client->pers.netname) );

	trap->SendServerCommand( ent-g_entities, "print \"Done.\n\"" );
}

/*
==================
Cmd_QuestAnswer_f
==================
*/
void Cmd_QuestAnswer_f( gentity_t *ent ) {
	char arg1[MAX_STRING_CHARS];

	if (ent->client->pers.universe_quest_progress != 5 || ent->client->pers.universe_quest_messages != 101)
	{
		trap->SendServerCommand( ent-g_entities, "print \"You dont need to answer the mayor name now.\n\"" );
		return;
	}

	if (level.quest_map != 24)
	{
		trap->SendServerCommand( ent-g_entities, "print \"This is not the right map.\n\"" );
		return;
	}

	trap->Argv(1, arg1, sizeof( arg1 ));

	if (Q_stricmp( arg1, "Samir" ) == 0 || Q_stricmp( arg1, "samir" ) == 0)
	{
		ent->client->pers.universe_quest_messages = 102;
	}
	else
	{
		ent->client->pers.universe_quest_messages = 103;
	}
}

/*
==================
Cmd_Answer_f
==================
*/
void Cmd_Answer_f( gentity_t *ent ) {
	char arg1[MAX_STRING_CHARS];

	if (ent->client->pers.eternity_quest_progress >= (NUMBER_OF_ETERNITY_QUEST_OBJECTIVES - 1))
	{
		trap->SendServerCommand( ent-g_entities, "print \"You dont need to answer riddles now.\n\"" );
		return;
	}

	if (level.quest_map != 10)
	{
		trap->SendServerCommand( ent-g_entities, "print \"You are not in the right map.\n\"" );
		return;
	}

	if (ent->client->pers.can_play_quest == 0)
	{
		trap->SendServerCommand( ent-g_entities, "print \"It is not your turn to answer riddles.\n\"" );
		return;
	}

	if (ent->client->pers.eternity_quest_timer == 0)
	{
		trap->SendServerCommand( ent-g_entities, "print \"You must find the location of the riddle in this map.\n\"" );
		return;
	}

	if (trap->Argc() != 2)
	{
		trap->SendServerCommand( ent-g_entities, "print \"Use ^3/answer <word> ^7to answer the riddle.\n\"" );
		return;
	}

	trap->Argv(1, arg1, sizeof( arg1 ));

	if (ent->client->pers.eternity_quest_progress == 0 && Q_stricmp( arg1, "key" ) == 0)
	{
		ent->client->pers.eternity_quest_timer = level.time + 5000;
		ent->client->pers.eternity_quest_progress = 1;
		save_account(ent);

		quest_get_new_player(ent);

		trap->SendServerCommand( -1, va("chat \"You answered the ^3Riddle of Earth ^7correctly, %s^7...\n\"",ent->client->pers.netname) );
		return;
	}
	else if (ent->client->pers.eternity_quest_progress == 1 && Q_stricmp( arg1, "clock" ) == 0)
	{
		ent->client->pers.eternity_quest_timer = level.time + 5000;
		ent->client->pers.eternity_quest_progress = 2;
		save_account(ent);

		quest_get_new_player(ent);

		trap->SendServerCommand( -1, va("chat \"You answered the ^1Riddle of Fire ^7correctly, %s^7...\n\"",ent->client->pers.netname) );
		return;
	}
	else if (ent->client->pers.eternity_quest_progress == 2 && Q_stricmp( arg1, "sword" ) == 0)
	{
		ent->client->pers.eternity_quest_timer = 0;
		ent->client->pers.eternity_quest_progress = 3;
		save_account(ent);

		quest_get_new_player(ent);

		trap->SendServerCommand( -1, va("chat \"You answered the ^1Riddle of Darkness ^7correctly, %s^7...\n\"",ent->client->pers.netname) );
		return;
	}
	else if (ent->client->pers.eternity_quest_progress == 3 && Q_stricmp( arg1, "sun" ) == 0)
	{
		ent->client->pers.eternity_quest_timer = level.time + 5000;
		ent->client->pers.eternity_quest_progress = 4;
		save_account(ent);

		quest_get_new_player(ent);

		trap->SendServerCommand( -1, va("chat \"You answered the ^4Riddle of Water ^7correctly, %s^7...\n\"",ent->client->pers.netname) );
		return;
	}
	else if (ent->client->pers.eternity_quest_progress == 4 && Q_stricmp( arg1, "fire" ) == 0)
	{
		ent->client->pers.eternity_quest_timer = level.time + 5000;
		ent->client->pers.eternity_quest_progress = 5;
		save_account(ent);

		quest_get_new_player(ent);

		trap->SendServerCommand( -1, va("chat \"You answered the ^7Riddle of Wind ^7correctly, %s^7...\n\"",ent->client->pers.netname) );
		return;
	}
	else if (ent->client->pers.eternity_quest_progress == 5 && Q_stricmp( arg1, "water" ) == 0)
	{
		ent->client->pers.eternity_quest_timer = 0;
		ent->client->pers.eternity_quest_progress = 6;
		save_account(ent);

		quest_get_new_player(ent);

		trap->SendServerCommand( -1, va("chat \"You answered the ^5Riddle of Light ^7correctly, %s^7...\n\"",ent->client->pers.netname) );
		return;
	}
	else if (ent->client->pers.eternity_quest_progress == 6 && Q_stricmp( arg1, "time" ) == 0)
	{
		ent->client->pers.eternity_quest_timer = level.time + 5000;
		ent->client->pers.eternity_quest_progress = 7;
		save_account(ent);

		quest_get_new_player(ent);

		trap->SendServerCommand( -1, va("chat \"You answered the ^6Riddle of Agility ^7correctly, %s^7...\n\"",ent->client->pers.netname) );
		return;
	}
	else if (ent->client->pers.eternity_quest_progress == 7 && Q_stricmp( arg1, "star" ) == 0)
	{
		ent->client->pers.eternity_quest_timer = level.time + 5000;
		ent->client->pers.eternity_quest_progress = 8;
		save_account(ent);

		quest_get_new_player(ent);

		trap->SendServerCommand( -1, va("chat \"You answered the ^2Riddle of Forest ^7correctly, %s^7...\n\"",ent->client->pers.netname) );
		return;
	}
	else if (ent->client->pers.eternity_quest_progress == 8 && Q_stricmp( arg1, "nature" ) == 0)
	{
		ent->client->pers.eternity_quest_timer = 0;
		ent->client->pers.eternity_quest_progress = 9;
		save_account(ent);

		quest_get_new_player(ent);

		trap->SendServerCommand( -1, va("chat \"You answered the ^5Riddle of Intelligence ^7correctly, %s^7...\n\"",ent->client->pers.netname) );
		return;
	}
	else if (ent->client->pers.eternity_quest_progress == 9 && Q_stricmp( arg1, "love" ) == 0)
	{
		ent->client->pers.eternity_quest_timer = 0;
		ent->client->pers.eternity_quest_progress = 10;
		save_account(ent);

		quest_get_new_player(ent);

		trap->SendServerCommand( -1, va("chat \"You answered the ^3Riddle of Eternity ^7correctly, %s^7...\n\"",ent->client->pers.netname) );
		return;
	}
	else
	{
		trap->SendServerCommand( -1, va("chat \"Thats not the right answer, %s^7...\n\"",ent->client->pers.netname) );
	}
}

/*
==================
Cmd_AllyList_f
==================
*/
void Cmd_AllyList_f( gentity_t *ent ) {
	char message[1024];

	strcpy(message,"");

	if (ent->client->sess.ally1 != -1)	
	{
		strcpy(message,va("%s^7%s\n",message,g_entities[ent->client->sess.ally1].client->pers.netname));
	}
	if (ent->client->sess.ally2 != -1)	
	{
		strcpy(message,va("%s^7%s\n",message,g_entities[ent->client->sess.ally2].client->pers.netname));
	}
	if (ent->client->sess.ally3 != -1)	
	{
		strcpy(message,va("%s^7%s\n",message,g_entities[ent->client->sess.ally3].client->pers.netname));
	}

	trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"", message) );
}

/*
==================
Cmd_AllyAdd_f
==================
*/
void Cmd_AllyAdd_f( gentity_t *ent ) {
	if (trap->Argc() == 1)
	{
		trap->SendServerCommand( ent-g_entities, va("print \"Use ^3/allyadd <player name or id> ^7to add an ally.\n\"") );
	}
	else
	{
		char arg1[MAX_STRING_CHARS];
		int client_id = -1;

		if (ent->client->sess.amrpgmode == 2 && ent->client->pers.guardian_mode > 0)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"You cannot add allies during a boss battle.\n\"") );
			return;
		}

		trap->Argv(1, arg1, sizeof( arg1 ));
		client_id = zyk_get_client( arg1 ); 

		if (client_id == -1)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Player not found\n\"") );
			return;
		}

		if (client_id == (ent-g_entities))
		{ // zyk: player cant add himself as ally
			trap->SendServerCommand( ent-g_entities, va("print \"You cannot add yourself as ally\n\"") );
			return; 
		}

		if (ent->client->sess.ally1 == client_id || ent->client->sess.ally2 == client_id || ent->client->sess.ally3 == client_id)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"You already have this ally.\n\"") );
			return;
		}

		// zyk: add this player as an ally
		if (ent->client->sess.ally1 == -1)
		{
			ent->client->sess.ally1 = client_id;
		}
		else if (ent->client->sess.ally2 == -1)
		{
			ent->client->sess.ally2 = client_id;
		}
		else if (ent->client->sess.ally3 == -1)
		{ 
			ent->client->sess.ally3 = client_id;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, va("print \"You reached the max number of allies.\n\"") );
			return;
		}

		trap->SendServerCommand( ent-g_entities, va("print \"Added ally %s^7\n\"", g_entities[client_id].client->pers.netname) );
		trap->SendServerCommand( client_id, va("print \"%s^7 added you as ally\n\"", ent->client->pers.netname) );
	}
}

/*
==================
Cmd_AllyRemove_f
==================
*/
void Cmd_AllyRemove_f( gentity_t *ent ) {
	if (trap->Argc() == 1)
	{
		trap->SendServerCommand( ent-g_entities, va("print \"Use ^3/allyremove <player name or id> ^7to remove an ally.\n\"") );
	}
	else
	{
		char arg1[MAX_STRING_CHARS];
		int client_id = -1;

		trap->Argv(1, arg1, sizeof( arg1 ));
		client_id = zyk_get_client( arg1 ); 

		if (client_id == -1)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Player not found\n\"") );
			return;
		}

		// zyk: removes this ally
		if (ent->client->sess.ally1 == client_id)
		{
			ent->client->sess.ally1 = -1;
		}
		else if (ent->client->sess.ally2 == client_id)
		{
			ent->client->sess.ally2 = -1;
		}
		else if (ent->client->sess.ally3 == client_id)
		{ 
			ent->client->sess.ally3 = -1;
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, va("print \"You don't have this ally!\n\"") );
			return;
		}
		trap->SendServerCommand( ent-g_entities, va("print \"Removed ally %s^7\n\"", g_entities[client_id].client->pers.netname) );
		trap->SendServerCommand( client_id, va("print \"%s^7 removed you as ally\n\"", ent->client->pers.netname) );
	}
}

/*
==================
Cmd_Settings_f
==================
*/
void Cmd_Settings_f( gentity_t *ent ) {
	if (trap->Argc() == 1)
	{
		char message[1024];
		strcpy(message,"");

		if (ent->client->pers.player_settings & (1 << 0))
		{
			sprintf(message,"\n^3 0 - RPG quests - ^1OFF");
		}
		else
		{
			sprintf(message,"\n^3 0 - RPG quests - ^2ON");
		}

		if (ent->client->pers.player_settings & (1 << 1))
		{
			sprintf(message,"%s\n^3 1 - Light Power - ^1OFF", message);
		}
		else
		{
			sprintf(message,"%s\n^3 1 - Light Power - ^2ON", message);
		}

		if (ent->client->pers.player_settings & (1 << 2))
		{
			sprintf(message,"%s\n^3 2 - Dark Power - ^1OFF", message);
		}
		else
		{
			sprintf(message,"%s\n^3 2 - Dark Power - ^2ON", message);
		}

		if (ent->client->pers.player_settings & (1 << 3))
		{
			sprintf(message,"%s\n^3 3 - Eternity Power - ^1OFF", message);
		}
		else
		{
			sprintf(message,"%s\n^3 3 - Eternity Power - ^2ON", message);
		}

		if (ent->client->pers.player_settings & (1 << 4))
		{
			sprintf(message,"%s\n^3 4 - Universe Power - ^1OFF", message);
		}
		else
		{
			sprintf(message,"%s\n^3 4 - Universe Power - ^2ON", message);
		}

		if (ent->client->pers.player_settings & (1 << 5))
		{
			sprintf(message,"%s\n^3 5 - Ultimate Power - ^1OFF", message);
		}
		else
		{
			sprintf(message,"%s\n^3 5 - Ultimate Power - ^2ON", message);
		}

		if (ent->client->pers.player_settings & (1 << 6))
		{
			sprintf(message,"%s\n^3 6 - Allow Force Powers from allies - ^1OFF", message);
		}
		else
		{
			sprintf(message,"%s\n^3 6 - Allow Force Powers from allies - ^2ON", message);
		}

		if (ent->client->pers.player_settings & (1 << 7))
		{
			sprintf(message,"%s\n^3 7 - Use Movers with Stun Baton - ^1OFF", message);
		}
		else
		{
			sprintf(message,"%s\n^3 7 - Use Movers with Stun Baton - ^2ON", message);
		}

		// zyk: Saber Style flags
		if (ent->client->pers.player_settings & (1 << 26))
			sprintf(message,"%s\n^3 8 - RPG Starting Single Saber Style - ^3Yellow", message);
		else if (ent->client->pers.player_settings & (1 << 27))
			sprintf(message,"%s\n^3 8 - RPG Starting Single Saber Style - ^1Red", message);
		else if (ent->client->pers.player_settings & (1 << 28))
			sprintf(message,"%s\n^3 8 - RPG Starting Single Saber Style - ^1Desann", message);
		else if (ent->client->pers.player_settings & (1 << 29))
			sprintf(message,"%s\n^3 8 - RPG Starting Single Saber Style - ^5Tavion", message);
		else
			sprintf(message,"%s\n^3 8 - RPG Starting Single Saber Style - ^5Blue", message);

		if (ent->client->pers.player_settings & (1 << 9))
		{
			sprintf(message,"%s\n^3 9 - RPG Starting Dual Sabers Style - ^1OFF", message);
		}
		else
		{
			sprintf(message,"%s\n^3 9 - RPG Starting Dual Sabers Style - ^2ON", message);
		}

		if (ent->client->pers.player_settings & (1 << 10))
		{
			sprintf(message,"%s\n^310 - RPG Starting Staff Style - ^1OFF", message);
		}
		else
		{
			sprintf(message,"%s\n^310 - RPG Starting Staff Style - ^2ON", message);
		}

		if (ent->client->pers.player_settings & (1 << 11))
		{
			sprintf(message,"%s\n^311 - Saber Starts ^1OFF", message);
		}
		else
		{
			sprintf(message,"%s\n^311 - Saber Starts ^2ON", message);
		}

		if (ent->client->pers.player_settings & (1 << 12))
		{
			sprintf(message,"%s\n^312 - Jetpack ^1OFF", message);
		}
		else
		{
			sprintf(message,"%s\n^312 - Jetpack ^2ON", message);
		}

		if (ent->client->pers.player_settings & (1 << 13))
		{
			sprintf(message,"%s\n^313 - Admin Protect ^1OFF", message);
		}
		else
		{
			sprintf(message,"%s\n^313 - Admin Protect ^2ON", message);
		}

		if (ent->client->pers.player_settings & (1 << 14))
		{
			sprintf(message,"%s\n^314 - Mind Control ^1OFF", message);
		}
		else
		{
			sprintf(message,"%s\n^314 - Mind Control ^2ON", message);
		}

		if (ent->client->pers.player_settings & (1 << 15))
		{
			sprintf(message,"%s\n^315 - Difficulty ^1Hard", message);
		}
		else
		{
			sprintf(message,"%s\n^315 - Difficulty ^2Normal", message);
		}

		trap->SendServerCommand( ent-g_entities, va("print \"%s\n\n^7Choose a setting above and use ^3/settings <number> ^7to turn it ^2ON ^7or ^1OFF^7\n\"", message) );
	}
	else
	{
		char arg1[MAX_STRING_CHARS];
		char new_status[32];
		int value = 0;

		trap->Argv(1, arg1, sizeof( arg1 ));
		value = atoi(arg1);

		if (value < 0 || value > 15)
		{
			trap->SendServerCommand( ent-g_entities, "print \"Invalid settings value.\n\"" );
			return;
		}

		if (value == 0 && ent->client->pers.player_settings & (1 << value) && ent->client->sess.sessionTeam != TEAM_SPECTATOR)
		{ // zyk: player can only activate quests again in Spectator Mode
			trap->SendServerCommand( ent-g_entities, "print \"You can only activate RPG quests in Spectator Mode.\n\"" );
			return;
		}

		if (value != 8 && value != 15)
		{
			if (ent->client->pers.player_settings & (1 << value))
			{
				ent->client->pers.player_settings &= ~(1 << value);
				strcpy(new_status,"^2ON^7");
			}
			else
			{
				ent->client->pers.player_settings |= (1 << value);
				strcpy(new_status,"^1OFF^7");
			}
		}
		else if (value == 15)
		{
			if (ent->client->pers.player_settings & (1 << value))
			{
				ent->client->pers.player_settings &= ~(1 << value);
				strcpy(new_status,"^2Normal^7");
			}
			else
			{
				ent->client->pers.player_settings |= (1 << value);
				strcpy(new_status,"^1Hard^7");
			}
		}
		else
		{ // zyk: starting saber style has its own handling code
			if (ent->client->pers.player_settings & (1 << 26))
			{
				ent->client->pers.player_settings &= ~(1 << 26);
				ent->client->pers.player_settings |= (1 << 27);
				strcpy(new_status,"^1Red^7");
			}
			else if (ent->client->pers.player_settings & (1 << 27))
			{
				ent->client->pers.player_settings &= ~(1 << 27);
				ent->client->pers.player_settings |= (1 << 28);
				strcpy(new_status,"^1Desann^7");
			}
			else if (ent->client->pers.player_settings & (1 << 28))
			{
				ent->client->pers.player_settings &= ~(1 << 28);
				ent->client->pers.player_settings |= (1 << 29);
				strcpy(new_status,"^5Tavion");
			}
			else if (ent->client->pers.player_settings & (1 << 29))
			{
				ent->client->pers.player_settings &= ~(1 << 29);
				strcpy(new_status,"^5Blue^7");
			}
			else
			{
				ent->client->pers.player_settings |= (1 << 26);
				strcpy(new_status,"^3Yellow^7");
			}
		}

		save_account(ent);	

		if (value == 0)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Quests %s\n\"", new_status) );
		}
		else if (value == 1)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Light Power %s\n\"", new_status) );
		}
		else if (value == 2)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Dark Power %s\n\"", new_status) );
		}
		else if (value == 3)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Eternity Power %s\n\"", new_status) );
		}
		else if (value == 4)
		{
			set_max_health(ent);
			set_max_shield(ent);
			trap->SendServerCommand( ent-g_entities, va("print \"Universe Power %s\n\"", new_status) );
		}
		else if (value == 5)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Ultimate Power %s\n\"", new_status) );
		}
		else if (value == 6)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Allow Force Powers from allies %s\n\"", new_status) );
		}
		else if (value == 7)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Use Movers with Stun Baton %s\n\"", new_status) );
		}
		else if (value == 8)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"RPG Starting Single Saber Style %s\n\"", new_status) );
		}
		else if (value == 9)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"RPG Starting Dual Sabers Style %s\n\"", new_status) );
		}
		else if (value == 10)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"RPG Starting Staff Style %s\n\"", new_status) );
		}
		else if (value == 11)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Saber Starts %s\n\"", new_status) );
		}
		else if (value == 12)
		{
			// zyk: player starts with jetpack if it is enabled in player settings
			if (!(ent->client->pers.player_settings & (1 << 12)))
			{
				ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_JETPACK);
			}
			else
			{
				ent->client->ps.stats[STAT_HOLDABLE_ITEMS] &= ~(1 << HI_JETPACK);
				if (ent->client->jetPackOn)
				{
					Jetpack_Off(ent);
				}
			}
			trap->SendServerCommand( ent-g_entities, va("print \"Jetpack %s\n\"", new_status) );
		}
		else if (value == 13)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Admin Protect %s\n\"", new_status) );
		}
		else if (value == 14)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Mind Control %s\n\"", new_status) );
		}
		else if (value == 15)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Difficulty %s\n\"", new_status) );
		}
		return;
	}
}

/*
==================
Cmd_RpgClass_f
==================
*/
void Cmd_RpgClass_f( gentity_t *ent ) {
	char arg1[MAX_STRING_CHARS];
	int value = 0;
	int i = 0;

	if (ent->client->sess.sessionTeam != TEAM_SPECTATOR)
	{
		trap->SendServerCommand( ent-g_entities, "print \"You must be at Spectator Mode to change your RPG class.\n\"" );
		return;
	}

	if (trap->Argc() == 1)
	{
		trap->SendServerCommand( ent-g_entities, "print \"Look at ^3/list classes ^7to see the class number, then ^2/rpgclass <class number>^7\n\"" );
		return;
	}

	trap->Argv(1, arg1, sizeof( arg1 ));
	value = atoi(arg1);

	ent->client->pers.rpg_class = value;

	// zyk: resetting skills that are not allowed in this class
	if (ent->client->pers.rpg_class == 1)
	{ // zyk: Force User
		for (i = 0; i < 7; i++)
		{
			while (ent->client->pers.ammo_levels[i] > 0)
			{
				ent->client->pers.ammo_levels[i]--;
				ent->client->pers.skillpoints++;
			}
		}

		for (i = 0; i < 10; i++)
		{
			while (ent->client->pers.weapons_levels[i] > 0)
			{
				ent->client->pers.weapons_levels[i]--;
				ent->client->pers.skillpoints++;
			}
		}

		for (i = 0; i < 8; i++)
		{
			while (ent->client->pers.holdable_items_levels[i] > 0)
			{
				ent->client->pers.holdable_items_levels[i]--;
				ent->client->pers.skillpoints++;
			}
		}

		while (ent->client->pers.jetpack_level > 0)
		{
			ent->client->pers.jetpack_level--;
			ent->client->pers.skillpoints++;
		}

		trap->SendServerCommand( ent-g_entities, "print \"You are now a Force User.\n\"" );
	}
	else if (ent->client->pers.rpg_class == 2)
	{ // zyk: Bounty Hunter
		for (i = 0; i < 18; i++)
		{
			if (i != 4)
			{
				while (ent->client->pers.force_powers_levels[i] > 0)
				{
					ent->client->pers.force_powers_levels[i]--;
					ent->client->pers.skillpoints++;
				}
			}
		}

		while (ent->client->pers.playerhealth > 0)
		{
			ent->client->pers.playerhealth--;
			ent->client->pers.skillpoints++;
		}

		while (ent->client->pers.shield > 0)
		{
			ent->client->pers.shield--;
			ent->client->pers.skillpoints++;
		}

		while (ent->client->pers.teamshield > 0)
		{
			ent->client->pers.teamshield--;
			ent->client->pers.skillpoints++;
		}

		while (ent->client->pers.mind_control > 0)
		{
			ent->client->pers.mind_control--;
			ent->client->pers.skillpoints++;
		}

		while (ent->client->pers.max_force_power_level > 0)
		{
			ent->client->pers.max_force_power_level--;
			ent->client->pers.skillpoints++;
		}

		trap->SendServerCommand( ent-g_entities, "print \"You are now a Bounty Hunter.\n\"" );
	}
	else if (ent->client->pers.rpg_class == 3)
	{ // zyk: Armored Soldier
		for (i = 0; i < 18; i++)
		{
			if (i != 4)
			{
				while (ent->client->pers.force_powers_levels[i] > 0)
				{
					ent->client->pers.force_powers_levels[i]--;
					ent->client->pers.skillpoints++;
				}
			}
		}

		for (i = 0; i < 8; i++)
		{
			if (i != 0 && i != 1 && i != 3 && i != 4)
			{
				while (ent->client->pers.holdable_items_levels[i] > 0)
				{
					ent->client->pers.holdable_items_levels[i]--;
					ent->client->pers.skillpoints++;
				}
			}
		}

		while (ent->client->pers.playerhealth > 0)
		{
			ent->client->pers.playerhealth--;
			ent->client->pers.skillpoints++;
		}

		while (ent->client->pers.shield > 0)
		{
			ent->client->pers.shield--;
			ent->client->pers.skillpoints++;
		}

		while (ent->client->pers.teamshield > 0)
		{
			ent->client->pers.teamshield--;
			ent->client->pers.skillpoints++;
		}

		while (ent->client->pers.mind_control > 0)
		{
			ent->client->pers.mind_control--;
			ent->client->pers.skillpoints++;
		}

		while (ent->client->pers.max_force_power_level > 0)
		{
			ent->client->pers.max_force_power_level--;
			ent->client->pers.skillpoints++;
		}

		trap->SendServerCommand( ent-g_entities, "print \"You are now an Armored Soldier.\n\"" );
	}
	else if (ent->client->pers.rpg_class == 4)
	{ // zyk: Monk
		for (i = 0; i < 18; i++)
		{
			if (i != 0 && i != 1 && i != 2 && i != 4 && i != 9 && i != 11 && i != 12 && i != 14 && i != 15 && i != 17)
			{
				while (ent->client->pers.force_powers_levels[i] > 0)
				{
					ent->client->pers.force_powers_levels[i]--;
					ent->client->pers.skillpoints++;
				}
			}
		}

		for (i = 0; i < 7; i++)
		{
			while (ent->client->pers.ammo_levels[i] > 0)
			{
				ent->client->pers.ammo_levels[i]--;
				ent->client->pers.skillpoints++;
			}
		}

		for (i = 0; i < 10; i++)
		{
			while (ent->client->pers.weapons_levels[i] > 0)
			{
				ent->client->pers.weapons_levels[i]--;
				ent->client->pers.skillpoints++;
			}
		}

		for (i = 0; i < 8; i++)
		{
			while (ent->client->pers.holdable_items_levels[i] > 0)
			{
				ent->client->pers.holdable_items_levels[i]--;
				ent->client->pers.skillpoints++;
			}
		}

		while (ent->client->pers.jetpack_level > 0)
		{
			ent->client->pers.jetpack_level--;
			ent->client->pers.skillpoints++;
		}

		while (ent->client->pers.mind_control > 0)
		{
			ent->client->pers.mind_control--;
			ent->client->pers.skillpoints++;
		}

		trap->SendServerCommand( ent-g_entities, "print \"You are now a Monk.\n\"" );
	}
	else if (ent->client->pers.rpg_class == 5)
	{ // zyk: Stealth Attacker
		for (i = 0; i < 18; i++)
		{
			if (i != 4)
			{
				while (ent->client->pers.force_powers_levels[i] > 0)
				{
					ent->client->pers.force_powers_levels[i]--;
					ent->client->pers.skillpoints++;
				}
			}
		}

		for (i = 0; i < 7; i++)
		{
			if (i != 1 && i != 2 && i != 5 && i != 6)
			{
				while (ent->client->pers.ammo_levels[i] > 0)
				{
					ent->client->pers.ammo_levels[i]--;
					ent->client->pers.skillpoints++;
				}
			}
		}

		for (i = 0; i < 10; i++)
		{
			if (i != 2 && i != 4 && i != 5 && i != 8)
			{
				while (ent->client->pers.weapons_levels[i] > 0)
				{
					ent->client->pers.weapons_levels[i]--;
					ent->client->pers.skillpoints++;
				}
			}
		}

		for (i = 0; i < 8; i++)
		{
			if (i != 0 && i != 7)
			{
				while (ent->client->pers.holdable_items_levels[i] > 0)
				{
					ent->client->pers.holdable_items_levels[i]--;
					ent->client->pers.skillpoints++;
				}
			}
		}

		while (ent->client->pers.playerhealth > 0)
		{
			ent->client->pers.playerhealth--;
			ent->client->pers.skillpoints++;
		}

		while (ent->client->pers.shield > 0)
		{
			ent->client->pers.shield--;
			ent->client->pers.skillpoints++;
		}

		while (ent->client->pers.teamshield > 0)
		{
			ent->client->pers.teamshield--;
			ent->client->pers.skillpoints++;
		}

		while (ent->client->pers.mind_control > 0)
		{
			ent->client->pers.mind_control--;
			ent->client->pers.skillpoints++;
		}

		while (ent->client->pers.max_force_power_level > 0)
		{
			ent->client->pers.max_force_power_level--;
			ent->client->pers.skillpoints++;
		}

		trap->SendServerCommand( ent-g_entities, "print \"You are now a Stealth Attacker.\n\"" );
	}
	else if (ent->client->pers.rpg_class == 6)
	{ // zyk: Duelist
		for (i = 0; i < 18; i++)
		{
			if (i > 10 && i != 13 && i != 14 && i != 15)
			{
				while (ent->client->pers.force_powers_levels[i] > 0)
				{
					ent->client->pers.force_powers_levels[i]--;
					ent->client->pers.skillpoints++;
				}
			}
		}

		for (i = 0; i < 7; i++)
		{
			while (ent->client->pers.ammo_levels[i] > 0)
			{
				ent->client->pers.ammo_levels[i]--;
				ent->client->pers.skillpoints++;
			}
		}

		for (i = 0; i < 10; i++)
		{
			while (ent->client->pers.weapons_levels[i] > 0)
			{
				ent->client->pers.weapons_levels[i]--;
				ent->client->pers.skillpoints++;
			}
		}

		for (i = 0; i < 8; i++)
		{
			while (ent->client->pers.holdable_items_levels[i] > 0)
			{
				ent->client->pers.holdable_items_levels[i]--;
				ent->client->pers.skillpoints++;
			}
		}

		while (ent->client->pers.jetpack_level > 0)
		{
			ent->client->pers.jetpack_level--;
			ent->client->pers.skillpoints++;
		}

		while (ent->client->pers.teamshield > 0)
		{
			ent->client->pers.teamshield--;
			ent->client->pers.skillpoints++;
		}

		while (ent->client->pers.mind_control > 0)
		{
			ent->client->pers.mind_control--;
			ent->client->pers.skillpoints++;
		}

		trap->SendServerCommand( ent-g_entities, "print \"You are now a Duelist.\n\"" );
	}
	else
	{
		trap->SendServerCommand( ent-g_entities, "print \"You are now a Free Warrior.\n\"" );
	}

	save_account(ent);
}

/*
==================
Cmd_GuardianQuest_f
==================
*/
void Cmd_GuardianQuest_f( gentity_t *ent ) {
	if (level.guardian_quest == 0)
	{
		int j = 0;
		gentity_t *this_ent = NULL;

		// zyk: player cant spawn if someone is fighting a guardian
		for (j = 0; j < level.maxclients; j++)
		{
			this_ent = &g_entities[j];
			if (this_ent && this_ent->client && this_ent->client->sess.amrpgmode == 2 && this_ent->client->pers.guardian_mode > 0)
			{
				trap->SendServerCommand( ent-g_entities, "print \"You can't start this quest while a player is fighting a guardian.\n\"" );
				return;
			}
		}

		level.guardian_quest_timer = level.time + 5000;
		level.guardian_quest = 1;
		trap->SendServerCommand( -1, va("print \"Guardian Quest activated.\n\"") );
	}
	else
	{
		trap->SendServerCommand( -1, va("print \"Guardian Quest is already active.\n\"") );
	}
}

/*
==================
Cmd_BountyQuest_f
==================
*/
void Cmd_BountyQuest_f( gentity_t *ent ) {
	gentity_t *this_ent = NULL;

	// zyk: reached MAX_CLIENTS, reset it to 0
	if (level.bounty_quest_target_id == level.maxclients)
		level.bounty_quest_target_id = 0;

	if (level.bounty_quest_choose_target == qtrue)
	{ // zyk: no one is the target, so choose one player to be the target
		while (level.bounty_quest_target_id < level.maxclients)
		{
			this_ent = &g_entities[level.bounty_quest_target_id];

			if (this_ent && this_ent->client && this_ent->client->sess.amrpgmode == 2 && this_ent->health > 0 && this_ent->client->sess.sessionTeam != TEAM_SPECTATOR)
			{
				level.bounty_quest_choose_target = qfalse;
				trap->SendServerCommand( -1, va("chat \"^3Bounty Quest: ^7A reward of ^3%d ^7credits will be given to who kills %s^7\n\"", (this_ent->client->pers.level*15), this_ent->client->pers.netname) );
				return;
			}

			level.bounty_quest_target_id++;
		}
		trap->SendServerCommand( -1, va("chat \"^3Bounty Quest: ^7No one was chosen as the target\n\"") );
	}
	else
	{ // zyk: there is already a target player
		this_ent = &g_entities[level.bounty_quest_target_id];
		if (this_ent && this_ent->client)
			trap->SendServerCommand( -1, va("chat \"^3Bounty Quest: ^7%s ^7is already the target\n\"", this_ent->client->pers.netname) );
	}
}

/*
==================
Cmd_PlayerMode_f
==================
*/
void Cmd_PlayerMode_f( gentity_t *ent ) {
	if (ent->client->sess.sessionTeam != TEAM_SPECTATOR)
	{
		trap->SendServerCommand( ent-g_entities, "print \"You must be at Spectator Mode to change mode.\n\"" );
		return;
	}

	load_account(ent, qtrue);
	save_account(ent);

	if (ent->client->sess.amrpgmode == 1)
		trap->SendServerCommand( ent-g_entities, "print \"^7You are now in ^2Admin-Only mode^7.\n\"" );
	else
		trap->SendServerCommand( ent-g_entities, "print \"^7You are now in ^2RPG mode^7.\n\"" );
}

/*
==================
Cmd_News_f
==================
*/
void Cmd_News_f( gentity_t *ent ) {
	int page = 1; // zyk: page the user wants to see
	char arg1[MAX_STRING_CHARS];
	char file_content[MAX_STRING_CHARS];
	char content[512];
	int i = 0;
	int results_per_page = 8; // zyk: number of results per page
	FILE *news_file;
	strcpy(file_content,"");
	strcpy(content,"");

	if ( trap->Argc() < 2 )
	{
		trap->SendServerCommand( ent-g_entities, "print \"Use ^3/news <page number> ^7to see the results of this page\n\"" );
		return;
	}

	trap->Argv(1, arg1, sizeof( arg1 ));
	page = atoi(arg1);

	if (page == 0)
	{
		trap->SendServerCommand( ent-g_entities, "print \"Invalid page number\n\"" );
		return;
	}

	news_file = fopen("news.txt","r");
	if (news_file != NULL)
	{
		while(i < (results_per_page * (page-1)))
		{ // zyk: reads the file until it reaches the position corresponding to the page number
			fgets(content,sizeof(content),news_file);
			i++;
		}

		while(i < (results_per_page * page) && fgets(content,sizeof(content),news_file) != NULL)
		{ // zyk: fgets returns NULL at EOF
			strcpy(file_content,va("%s%s",file_content,content));
			i++;
		}

		fclose(news_file);
		trap->SendServerCommand(ent-g_entities, va("print \"\n%s\n\"",file_content));
	}
	else
	{
		trap->SendServerCommand( ent-g_entities, "print \"The news file does not exist\n\"" );
		return;
	}
}

/*
==================
Cmd_NpcList_f
==================
*/
void Cmd_NpcList_f( gentity_t *ent ) {
	int page = 1; // zyk: page the user wants to see
	char arg1[MAX_STRING_CHARS];
	char file_content[MAX_STRING_CHARS];
	char content[512];
	int i = 0;
	int results_per_page = 9; // zyk: number of results per page
	FILE *npc_list_file;
	strcpy(file_content,"");
	strcpy(content,"");

	if ( trap->Argc() < 2 )
	{
		trap->SendServerCommand( ent-g_entities, "print \"Use ^3/npclist <page number> ^7to see the results of this page\n\"" );
		return;
	}

	trap->Argv(1, arg1, sizeof( arg1 ));
	page = atoi(arg1);

	if (page == 0)
	{
		trap->SendServerCommand( ent-g_entities, "print \"Invalid page number\n\"" );
		return;
	}

	npc_list_file = fopen("npclist.txt","r");
	if (npc_list_file != NULL)
	{
		while(i < (results_per_page * (page-1)))
		{ // zyk: reads the file until it reaches the position corresponding to the page number
			fgets(content,sizeof(content),npc_list_file);
			i++;
		}

		while(i < (results_per_page * page) && fgets(content,sizeof(content),npc_list_file) != NULL)
		{ // zyk: fgets returns NULL at EOF
			strcpy(file_content,va("%s%s",file_content,content));
			i++;
		}

		fclose(npc_list_file);
		trap->SendServerCommand(ent-g_entities, va("print \"\n%s\n\"",file_content));
	}
	else
	{
		trap->SendServerCommand( ent-g_entities, "print \"The npclist file does not exist\n\"" );
		return;
	}
}

/*
==================
Cmd_VehicleList_f
==================
*/
void Cmd_VehicleList_f( gentity_t *ent ) {
	int page = 1; // zyk: page the user wants to see
	char arg1[MAX_STRING_CHARS];
	char file_content[MAX_STRING_CHARS];
	char content[512];
	int i = 0;
	int results_per_page = 9; // zyk: number of results per page
	FILE *vehicle_list_file;
	strcpy(file_content,"");
	strcpy(content,"");

	if ( trap->Argc() < 2 )
	{
		trap->SendServerCommand( ent-g_entities, "print \"Use ^3/vehiclelist <page number> ^7to see the results of this page\n\"" );
		return;
	}

	trap->Argv(1, arg1, sizeof( arg1 ));
	page = atoi(arg1);

	if (page == 0)
	{
		trap->SendServerCommand( ent-g_entities, "print \"Invalid page number\n\"" );
		return;
	}

	vehicle_list_file = fopen("vehiclelist.txt","r");
	if (vehicle_list_file != NULL)
	{
		while(i < (results_per_page * (page-1)))
		{ // zyk: reads the file until it reaches the position corresponding to the page number
			fgets(content,sizeof(content),vehicle_list_file);
			i++;
		}

		while(i < (results_per_page * page) && fgets(content,sizeof(content),vehicle_list_file) != NULL)
		{ // zyk: fgets returns NULL at EOF
			strcpy(file_content,va("%s%s",file_content,content));
			i++;
		}

		fclose(vehicle_list_file);
		trap->SendServerCommand(ent-g_entities, va("print \"\n%s\n\"",file_content));
	}
	else
	{
		trap->SendServerCommand( ent-g_entities, "print \"The vehiclelist file does not exist\n\"" );
		return;
	}
}

/*
==================
Cmd_RaceMode_f
==================
*/
void Cmd_RaceMode_f( gentity_t *ent ) {
	if (ent->client->pers.race_position == 0)
	{
		int j = 0, swoop_number = -1;
		int occupied_positions[MAX_CLIENTS]; // zyk: sets 1 to each race position already occupied by a player
		gentity_t *this_ent = NULL;
		vec3_t origin, yaw;
		char zyk_info[MAX_INFO_STRING] = {0};
		char zyk_mapname[128] = {0};

		// zyk: getting the map name
		trap->GetServerinfo(zyk_info, sizeof(zyk_info));
		Q_strncpyz(zyk_mapname, Info_ValueForKey( zyk_info, "mapname" ), sizeof(zyk_mapname));

		if (Q_stricmp(zyk_mapname, "t2_trip") == 0)
		{
			int max_racers = 8; // zyk: each race map will have its own max_racers

			if (level.race_mode > 1)
			{
				trap->SendServerCommand( ent-g_entities, "print \"Race has already started. Try again at the next race!\n\"" );
				return;
			}

			for (j = 0; j < MAX_CLIENTS; j++)
			{ // zyk: if race already started, this player cant join anymore
				this_ent = &g_entities[j];
				if (this_ent && this_ent->client && this_ent->inuse && this_ent->health > 0 && this_ent->client->sess.amrpgmode == 2 && this_ent->client->pers.guardian_mode > 0)
				{
					trap->SendServerCommand( ent-g_entities, "print \"You can't start a race while someone is in a Guardian Battle!\n\"" );
					return;
				}
			}

			// zyk: initializing array of occupied_positions
			for (j = 0; j < MAX_CLIENTS; j++)
			{
				occupied_positions[j] = 0;
			}

			// zyk: calculates which position the swoop of this player must be spawned
			for (j = 0; j < MAX_CLIENTS; j++)
			{
				this_ent = &g_entities[j];
				if (this_ent && ent != this_ent && this_ent->client && this_ent->inuse && this_ent->health > 0 && this_ent->client->sess.sessionTeam != TEAM_SPECTATOR && this_ent->client->pers.race_position > 0)
					occupied_positions[this_ent->client->pers.race_position - 1] = 1;
			}

			for (j = 0; j < max_racers; j++)
			{
				if (occupied_positions[j] == 0)
				{ // zyk: an empty race position, use this one
					swoop_number = j;
					break;
				}
			}

			if (swoop_number == -1)
			{ // zyk: the meximum allowed number of racers is 8 in this map
				trap->SendServerCommand( ent-g_entities, "print \"The race is already full of racers! Try again later!\n\"" );
				return;
			}
			
			origin[0] = -3930;
			origin[1] = (-20683 + (swoop_number * 80));
			origin[2] = 1509;

			yaw[0] = 0.0f;
			yaw[1] = -179.0f;
			yaw[2] = 0.0f;

			if (swoop_number < max_racers)
			{
				// zyk: teleporting player to the swoop area
				TeleportPlayer( ent, origin, yaw);

				ent->client->pers.race_position = swoop_number + 1;

				NPC_SpawnType(ent,"swoop",NULL,qtrue);

				level.race_start_timer = level.time + 15000; // zyk: race will start 15 seconds after the last player who joined the race
				level.race_mode = 1;

				trap->SendServerCommand( -1, va("chat \"^3Race System: ^7%s ^7joined the race!\n\"",ent->client->pers.netname) );
			}
		}
		else
		{
			trap->SendServerCommand( ent-g_entities, "print \"Races can only be done in ^3t2_trip ^7map.\n\"" );
		}
	}
	else
	{
		trap->SendServerCommand( -1, va("chat \"^3Race System: ^7%s ^7abandoned the race!\n\"",ent->client->pers.netname) );

		ent->client->pers.race_position = 0;
		try_finishing_race();

		return;
	}
}

/*
==================
Cmd_Drop_f
==================
*/
extern qboolean saberKnockOutOfHand(gentity_t *saberent, gentity_t *saberOwner, vec3_t velocity);
void Cmd_Drop_f( gentity_t *ent ) {
	vec3_t vel;
	gitem_t *item;
	gentity_t *launched;
	int weapon = ent->s.weapon;
	vec3_t uorg, vecnorm, thispush_org;

	if (weapon == WP_NONE || weapon == WP_MELEE || weapon == WP_EMPLACED_GUN || weapon == WP_TURRET)
	{ //can't have this
		return;
	}

	VectorCopy(ent->client->ps.origin, thispush_org);

	VectorCopy(ent->client->ps.origin, uorg);
	uorg[2] += 64;

	VectorSubtract(uorg, thispush_org, vecnorm);
	VectorNormalize(vecnorm);

	if (weapon == WP_SABER)
	{
		vel[0] = vecnorm[0]*100;
		vel[1] = vecnorm[1]*100;
		vel[2] = vecnorm[2]*100;
		saberKnockOutOfHand(&g_entities[ent->client->ps.saberEntityNum],ent,vel);
		return;
	}

	// find the item type for this weapon
	item = BG_FindItemForWeapon( weapon );

	vel[0] = vecnorm[0]*500;
	vel[1] = vecnorm[1]*500;
	vel[2] = vecnorm[2]*500;

	launched = LaunchItem(item, ent->client->ps.origin, vel);

	launched->s.generic1 = ent->s.number;
	launched->s.powerups = level.time + 1500;

	launched->count = bg_itemlist[BG_GetItemIndexByTag(weapon, IT_WEAPON)].quantity;

	ent->client->ps.ammo[weaponData[weapon].ammoIndex] -= (int)ceil(bg_itemlist[BG_GetItemIndexByTag(weapon, IT_WEAPON)].quantity * 0.5);

	if (ent->client->ps.ammo[weaponData[weapon].ammoIndex] < 0)
	{
		launched->count -= (-ent->client->ps.ammo[weaponData[weapon].ammoIndex]);
		ent->client->ps.ammo[weaponData[weapon].ammoIndex] = 0;
	}

	if ((ent->client->ps.ammo[weaponData[weapon].ammoIndex] < 1 && weapon != WP_DET_PACK) ||
		(weapon != WP_THERMAL && weapon != WP_DET_PACK && weapon != WP_TRIP_MINE))
	{
		int i = 0;
		int weap = -1;

		ent->client->ps.stats[STAT_WEAPONS] &= ~(1 << weapon);

		while (i < WP_NUM_WEAPONS)
		{
			if ((ent->client->ps.stats[STAT_WEAPONS] & (1 << i)) && i != WP_NONE)
			{ //this one's good
				weap = i;
				break;
			}
			i++;
		}

		if (weap != -1)
		{
			ent->s.weapon = weap;
			ent->client->ps.weapon = weap;
		}
		else
		{
			ent->s.weapon = 0;
			ent->client->ps.weapon = 0;
		}

		G_AddEvent(ent, EV_NOAMMO, weapon);
	}
}

/*
==================
Cmd_Jetpack_f
==================
*/
void Cmd_Jetpack_f( gentity_t *ent ) {
	if (!(ent->client->ps.stats[STAT_HOLDABLE_ITEMS] & (1 << HI_JETPACK)) && 
		(ent->client->sess.amrpgmode < 2 || ent->client->pers.jetpack_level > 0))
	{ // zyk: gets jetpack if player does not have it. RPG players need jetpack skill to get it
		ent->client->ps.stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_JETPACK);
	}
	else
	{
		if (ent->client->jetPackOn)
			Jetpack_Off(ent);
		ent->client->ps.stats[STAT_HOLDABLE_ITEMS] &= ~(1 << HI_JETPACK);
	}
}

/*
==================
Cmd_EntAdd_f
==================
*/
void Cmd_EntAdd_f( gentity_t *ent ) {
	gentity_t *new_ent = NULL;
	qboolean worked = qfalse;
	int number_of_args = trap->Argc();
	char arg1[MAX_STRING_CHARS];
	char arg2[MAX_STRING_CHARS];
	char arg3[MAX_STRING_CHARS];

	if (!(ent->client->pers.bitvalue & (1 << 5)))
	{ // zyk: admin command
		trap->SendServerCommand( ent-g_entities, "print \"You don't have this admin command.\n\"" );
		return;
	}

	if ( number_of_args < 2)
	{
		trap->SendServerCommand( ent-g_entities, va("print \"You must specify at least the entity class. Ex: ^3/entadd info_player_deathmatch^7, which spawns a spawn point in the map\n\"") );
		return;
	}

	trap->Argv( 1, arg1, sizeof( arg1 ) );

	// zyk: spawns the new entity
	new_ent = G_Spawn();

	if (new_ent)
	{
		zyk_set_entity_field(new_ent,"classname",G_NewString(arg1));

		if (number_of_args > 2)
		{
			trap->Argv( 2, arg2, sizeof( arg2 ) );
			zyk_set_entity_field(new_ent,"spawnflags",G_NewString(arg2));
		}
		if (number_of_args > 3)
		{
			trap->Argv( 3, arg3, sizeof( arg3 ) );
			if ((Q_stricmp (arg1, "fx_runner") == 0))
				new_ent->s.modelindex = G_EffectIndex( G_NewString(arg3) );
			else
				zyk_set_entity_field(new_ent,"model",G_NewString(arg3));
		}

		zyk_spawn_entity(new_ent);

		trap->SendServerCommand( ent-g_entities, va("print \"Entity %d spawned\n\"", new_ent->s.number) );
	}
	else
	{
		trap->SendServerCommand( ent-g_entities, va("print \"Error in entity spawn\n\"") );
		return;
	}
}

/*
==================
Cmd_EntEdit_f
==================
*/
void Cmd_EntEdit_f( gentity_t *ent ) {
	gentity_t *this_ent = NULL;
	qboolean worked = qfalse;
	int number_of_args = trap->Argc();
	int entity_id = -1;
	char arg1[MAX_STRING_CHARS];
	char arg2[MAX_STRING_CHARS];
	char arg3[MAX_STRING_CHARS];
	char arg4[MAX_STRING_CHARS];
	char arg5[MAX_STRING_CHARS];

	if (!(ent->client->pers.bitvalue & (1 << 5)))
	{ // zyk: admin command
		trap->SendServerCommand( ent-g_entities, "print \"You don't have this admin command.\n\"" );
		return;
	}

	if ( number_of_args < 2)
	{
		trap->SendServerCommand( ent-g_entities, va("print \"You must specify at least the entity ID.\n\"") );
		return;
	}

	trap->Argv( 1, arg1, sizeof( arg1 ) );
	entity_id = atoi(arg1);

	if (entity_id < 0 || entity_id >= level.num_entities)
	{
		trap->SendServerCommand( ent-g_entities, va("print \"Invalid Entity ID.\n\"") );
		return;
	}

	this_ent = &g_entities[entity_id];

	if (number_of_args == 2)
	{
		// zyk: players have their origin and yaw set in ps struct
		if (entity_id < MAX_CLIENTS)
			trap->SendServerCommand( ent-g_entities, va("print \"^2Entity %d\n^3classname: ^7%s\n^3targetname: ^7%s\n^3target: ^7%s\n^3target2: ^7%s\n^3spawnflags: ^7%d\n^3wait: ^7%f\n^3delay: ^7%d\n^3message: ^7%s\n^3model: ^7%s\n^3model2: ^7%s\n^3origin(x y z yaw): ^7%d %d %d %d\n^3mins(x y z): ^7%d %d %d\n^3maxs(x y z): ^7%d %d %d\n\"",this_ent->s.number,this_ent->classname,this_ent->targetname,this_ent->target,this_ent->target2,this_ent->spawnflags,this_ent->wait,this_ent->delay,this_ent->message,this_ent->model,this_ent->model2,(int)this_ent->client->ps.origin[0],(int)this_ent->client->ps.origin[1],(int)this_ent->client->ps.origin[2],(int)this_ent->client->ps.viewangles[1],(int)this_ent->r.mins[0],(int)this_ent->r.mins[1],(int)this_ent->r.mins[2],(int)this_ent->r.maxs[0],(int)this_ent->r.maxs[1],(int)this_ent->r.maxs[2]) );
		else
			trap->SendServerCommand( ent-g_entities, va("print \"^2Entity %d\n^3classname: ^7%s\n^3targetname: ^7%s\n^3target: ^7%s\n^3target2: ^7%s\n^3spawnflags: ^7%d\n^3wait: ^7%f\n^3delay: ^7%d\n^3message: ^7%s\n^3model: ^7%s\n^3model2: ^7%s\n^3origin(x y z yaw): ^7%d %d %d %d\n^3mins(x y z): ^7%d %d %d\n^3maxs(x y z): ^7%d %d %d\n\"",this_ent->s.number,this_ent->classname,this_ent->targetname,this_ent->target,this_ent->target2,this_ent->spawnflags,this_ent->wait,this_ent->delay,this_ent->message,this_ent->model,this_ent->model2,(int)this_ent->s.origin[0],(int)this_ent->s.origin[1],(int)this_ent->s.origin[2],(int)this_ent->s.angles[1],(int)this_ent->r.mins[0],(int)this_ent->r.mins[1],(int)this_ent->r.mins[2],(int)this_ent->r.maxs[0],(int)this_ent->r.maxs[1],(int)this_ent->r.maxs[2]) );
	}
	else
	{
		if ( number_of_args == 3)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"You must specify an attribute and the value for it. Ex: /entedit 150 targetname test.\n\"") );
			return;
		}

		trap->Argv( 2, arg2, sizeof( arg2 ) );
		trap->Argv( 3, arg3, sizeof( arg3 ) );

		if (Q_stricmp (this_ent->classname, "fx_runner") == 0 && Q_stricmp (arg2, "fxFile") == 0)
		{ // yk: to change the fxFile, we must change the modelIndex
			this_ent->s.modelindex = G_EffectIndex( G_NewString(arg3) );
		}
		else
		{
			if (Q_stricmp (arg2, "origin") == 0 || Q_stricmp (arg2, "angles") == 0 || Q_stricmp (arg2, "mins") == 0 || Q_stricmp (arg2, "maxs") == 0)
			{
				if ( number_of_args < 6)
				{
					trap->SendServerCommand( ent-g_entities, va("print \"You must specify the x, y and z axis for this attribute. Ex: 0 0 0.\n\"") );
					return;
				}

				trap->Argv( 4, arg4, sizeof( arg4 ) );
				trap->Argv( 5, arg5, sizeof( arg5 ) );

				strcpy(arg3,va("%s %s %s",arg3,arg4,arg5));
			}

			zyk_set_entity_field(this_ent,G_NewString(arg2),G_NewString(arg3));
		}

		zyk_spawn_entity(this_ent);

		trap->SendServerCommand( ent-g_entities, va("print \"Entity %d edited\n\"", this_ent->s.number) );
	}
}

/*
==================
Cmd_EntNear_f
==================
*/
void Cmd_EntNear_f( gentity_t *ent ) {
	int i = 0;
	char message[1024];
	gentity_t *this_ent = NULL;

	if (!(ent->client->pers.bitvalue & (1 << 5)))
	{ // zyk: admin command
		trap->SendServerCommand( ent-g_entities, "print \"You don't have this admin command.\n\"" );
		return;
	}

	strcpy(message,"");

	for (i = 0; i < level.num_entities; i++)
	{
		this_ent = &g_entities[i];
		if ((int)Distance(ent->client->ps.origin, this_ent->s.origin) < 200)
		{
			strcpy(message,va("%s\n%d - %s",message,this_ent->s.number,this_ent->classname));
		}
	}

	trap->SendServerCommand( ent-g_entities, va("print \"%s\n\"",message) );
}

/*
==================
Cmd_EntList_f
==================
*/
void Cmd_EntList_f( gentity_t *ent ) {
	int i = 0;
	int page_number = 0;
	gentity_t *target_ent;
	char arg1[MAX_STRING_CHARS];
	char message[1024];

	strcpy(message,"");

	if (!(ent->client->pers.bitvalue & (1 << 5)))
	{ // zyk: admin command
		trap->SendServerCommand( ent-g_entities, "print \"You don't have this admin command.\n\"" );
		return;
	}

	if ( trap->Argc() < 2)
	{
		trap->SendServerCommand( ent-g_entities, va("print \"You must specify a page number greater than 0. Example: ^3/entlist 1^7.\n\"") );
		return;
	}

	trap->Argv( 1, arg1, sizeof( arg1 ) );
	page_number = atoi(arg1);

	for (i = 0; i < level.num_entities; i++)
	{
		if (i >= ((page_number - 1) * 10) && i < (page_number * 10))
		{ // zyk: this command lists 10 entities per page
			target_ent = &g_entities[i];
			sprintf(message,"%s\n%d - %s - %s - %s",message,(target_ent-g_entities),target_ent->classname,target_ent->targetname,target_ent->target);
		}
	}

	trap->SendServerCommand( ent-g_entities, va("print \"^3\nID - classname - targetname - target\n^7%s\n\n\"",message) );
}

/*
==================
Cmd_EntRemove_f
==================
*/
void Cmd_EntRemove_f( gentity_t *ent ) {
	int i = 0;
	int entity_id = -1;
	int entity_id2 = -1;
	gentity_t *target_ent;
	char   arg1[MAX_STRING_CHARS];
	char   arg2[MAX_STRING_CHARS];

	if (!(ent->client->pers.bitvalue & (1 << 5)))
	{ // zyk: admin command
		trap->SendServerCommand( ent-g_entities, "print \"You don't have this admin command.\n\"" );
		return;
	}

	if ( trap->Argc() < 2)
	{
		trap->SendServerCommand( ent-g_entities, va("print \"You must specify an entity id.\n\"") );
		return;
	}

	if (trap->Argc() == 2)
	{
		trap->Argv( 1, arg1, sizeof( arg1 ) );
		entity_id = atoi(arg1);

		if (entity_id >= 0 && entity_id < MAX_CLIENTS)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Entity ID %d is a player slot and cannot be removed.\n\"",entity_id) );
			return;
		}

		for (i = 0; i < level.num_entities; i++)
		{
			target_ent = &g_entities[i];
			if ((target_ent-g_entities) == entity_id)
			{
				G_FreeEntity( target_ent );
				trap->SendServerCommand( ent-g_entities, va("print \"Entity %d removed.\n\"",i) );
				return;
			}
		}

		trap->SendServerCommand( ent-g_entities, va("print \"Entity %d not found.\n\"",entity_id) );
	}
	else
	{
		trap->Argv( 1, arg1, sizeof( arg1 ) );
		entity_id = atoi(arg1);

		if (entity_id >= 0 && entity_id < MAX_CLIENTS)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Entity 1 ID %d is a player slot and cannot be removed.\n\"",entity_id) );
			return;
		}

		trap->Argv( 2, arg2, sizeof( arg2 ) );
		entity_id2 = atoi(arg2);

		if (entity_id2 >= 0 && entity_id2 < MAX_CLIENTS)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Entity 2 ID %d is a player slot and cannot be removed.\n\"",entity_id) );
			return;
		}

		for (i = 0; i < level.num_entities; i++)
		{
			target_ent = &g_entities[i];
			if ((target_ent-g_entities) >= entity_id && (target_ent-g_entities) <= entity_id2)
			{
				if (Q_stricmp( target_ent->classname, "info_player_deathmatch") != 0)
					G_FreeEntity( target_ent );
			}
		}

		trap->SendServerCommand( ent-g_entities, "print \"Entities removed.\n\"" );
		return;
	}
}

/*
==================
Cmd_AdminList_f
==================
*/
void Cmd_AdminList_f( gentity_t *ent ) {
	char message[1024];
	char message_content[7][150];
	int i = 0;
	strcpy(message,"");

	if (trap->Argc() == 1)
	{
		while (i < 6)
		{
			strcpy(message_content[i],"");
			i++;
		}
		message_content[7][0] = '\0';

		if ((ent->client->pers.bitvalue & (1 << 0))) 
		{
			strcpy(message_content[0],va("^3 %d ^7- NPC: ^2yes\n",0));
		}
		else
		{
			strcpy(message_content[0],va("^3 %d ^7- NPC: ^1no\n",0));
		}

		if ((ent->client->pers.bitvalue & (1 << 1))) 
		{
			strcpy(message_content[1],va("^3 %d ^7- NoClip: ^2yes\n",1));
		}
		else
		{
			strcpy(message_content[1],va("^3 %d ^7- NoClip: ^1no\n",1));
		}

		if ((ent->client->pers.bitvalue & (1 << 2))) 
		{
			strcpy(message_content[2],va("^3 %d ^7- GiveAdmin: ^2yes\n",2));
		}
		else
		{
			strcpy(message_content[2],va("^3 %d ^7- GiveAdmin: ^1no\n",2));
		}

		if ((ent->client->pers.bitvalue & (1 << 3))) 
		{
			strcpy(message_content[3],va("^3 %d ^7- Teleport: ^2yes\n",3));
		}
		else
		{
			strcpy(message_content[3],va("^3 %d ^7- Teleport: ^1no\n",3));
		}

		if ((ent->client->pers.bitvalue & (1 << 4))) 
		{
			strcpy(message_content[4],va("^3 %d ^7- AdminProtect: ^2yes\n",4));
		}
		else
		{
			strcpy(message_content[4],va("^3 %d ^7- AdminProtect: ^1no\n",4));
		}

		if ((ent->client->pers.bitvalue & (1 << 5))) 
		{
			strcpy(message_content[5],va("^3 %d ^7- EntitySystem: ^2yes\n",5));
		}
		else
		{
			strcpy(message_content[5],va("^3 %d ^7- EntitySystem: ^1no\n",5));
		}

		for (i = 0; i < 6; i++)
		{
			sprintf(message,"%s%s",message,message_content[i]);
		}

		trap->SendServerCommand( ent-g_entities, va("print \"\n%s\nUse ^3/adminlist <number> ^7to see command info\n\n\"", message) );
	}
	else
	{ // zyk: display help info for an admin command
		char arg1[MAX_STRING_CHARS];
		int command_number = 0;
		
		trap->Argv( 1,  arg1, sizeof( arg1 ) );
		command_number = atoi(arg1);

		if (command_number == 0)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\nUse ^3/npc spawn <name> ^7to spawn a npc. Use ^3/npc spawn vehicle <name> to spawn a vehicle. Use ^3/npc kill all ^7to kill all npcs\n\n\"" );
		}
		else if (command_number == 1)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\nUse ^3/noclip ^7to toggle noclip\n\n\"" );
		}
		else if (command_number == 2)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\nThis flag allows admins to give or remove admin commands from a player with ^3/adminup <name> <command number> ^7and ^3/admindown <name> <command number>^7\n\n\"" );
		}
		else if (command_number == 3)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\nThis command can be ^3/teleport^7 or ^3/tele^7. Use ^3/teleport point ^7to mark a spot in map, then use ^3/teleport ^7to go there. Use ^3/teleport <player name or ID> ^7to teleport to a player. Use ^3/teleport <player name or ID> <player name or ID> ^7to teleport a player to another. Use ^3/teleport <x> <y> <z> ^7to teleport to coordinates\n\n\"" );
		}
		else if (command_number == 4)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\nWith this flag, a player can use Admin Protect option in /settings to protect himself from admin commands\n\n\"" );
		}
		else if (command_number == 5)
		{
			trap->SendServerCommand( ent-g_entities, "print \"\nUse ^3/entitysystem ^7to see the Entity System commands\n\n\"" );
		}
	}
}

/*
==================
Cmd_AdminUp_f
==================
*/
void Cmd_AdminUp_f( gentity_t *ent ) {
	if (ent->client->pers.bitvalue & (1 << 2))
	{
		char	arg1[MAX_STRING_CHARS];
		char	arg2[MAX_STRING_CHARS];
		int client_id = -1;
		int i = 0;
		int bitvaluecommand = 0;

		if ( trap->Argc() != 3 )
		{ 
			trap->SendServerCommand( ent-g_entities, "print \"You must write the player name and the admin command number.\n\"" ); 
			return; 
		}
		trap->Argv( 1,  arg1, sizeof( arg1 ) );
		trap->Argv( 2,  arg2, sizeof( arg2 ) );
		client_id = zyk_get_client( arg1 );

		if (client_id == -1)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Player not found\n\"") );
			return;
		}

		if (g_entities[client_id].client->sess.amrpgmode == 0)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Player is not logged in\n\"") );
			return;
		}
		if (Q_stricmp (arg2, "all") == 0)
		{ // zyk: if player wrote all, give all commands to the target player
			for (i = 0; i < 6; i++)
				g_entities[client_id].client->pers.bitvalue |= (1 << i);
		}
		else
		{
			bitvaluecommand = atoi(arg2);
			if (bitvaluecommand < 0 || bitvaluecommand >= 6)
			{
				trap->SendServerCommand( ent-g_entities, va("print \"Invalid admin command\n\"") );
				return; 
			}
			g_entities[client_id].client->pers.bitvalue |= (1 << bitvaluecommand);
		}

		save_account(&g_entities[client_id]);

		trap->SendServerCommand( ent-g_entities, "print \"Admin commands upgraded successfully.\n\"" );
	}
	else
	{
		trap->SendServerCommand( ent-g_entities, "print \"You can't use this command.\n\"" );
	}
}

/*
==================
Cmd_AdminDown_f
==================
*/
void Cmd_AdminDown_f( gentity_t *ent ) {
	if (ent->client->pers.bitvalue & (1 << 2))
	{
		char	arg1[MAX_STRING_CHARS];
		char	arg2[MAX_STRING_CHARS];
		int client_id = -1;
		int bitvaluecommand = 0;

		if ( trap->Argc() != 3 )
		{ 
			trap->SendServerCommand( ent-g_entities, "print \"You must write a player name and the admin command number.\n\"" ); 
			return; 
		}
		trap->Argv( 1,  arg1, sizeof( arg1 ) );
		trap->Argv( 2,  arg2, sizeof( arg2 ) );
		client_id = zyk_get_client( arg1 ); 
				
		if (client_id == -1)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Player not found\n\"") );
			return;
		}

		if (g_entities[client_id].client->sess.amrpgmode == 0)
		{
			trap->SendServerCommand( ent-g_entities, va("print \"Player is not logged in\n\"") );
			return;
		}

		if (Q_stricmp (arg2, "all") == 0)
		{ // zyk: if player wrote all, take away all admin commands from target player
			g_entities[client_id].client->pers.bitvalue = 0;
		}
		else
		{
			bitvaluecommand = atoi(arg2);
			if (bitvaluecommand < 0 || bitvaluecommand >= 6)
			{
				trap->SendServerCommand( ent-g_entities, va("print \"Invalid admin command\n\"") );
				return; 
			}
			g_entities[client_id].client->pers.bitvalue &= ~(1 << bitvaluecommand);
		}

		save_account(&g_entities[client_id]);

		trap->SendServerCommand( ent-g_entities, "print \"Admin commands upgraded successfully.\n\"" );
	}
	else
	{
		trap->SendServerCommand( ent-g_entities, "print \"You can't use this command.\n\"" );
	}
}

/*
==================
Cmd_EntitySystem_f
==================
*/
void Cmd_EntitySystem_f( gentity_t *ent ) {
	if (!(ent->client->pers.bitvalue & (1 << 5)))
	{ // zyk: admin command
		trap->SendServerCommand( ent-g_entities, "print \"You don't have this admin command.\n\"" );
		return;
	}

	trap->SendServerCommand( ent-g_entities, va("print \"\n^2Entity System Commands\n\n^3/entadd <classname> [spawnflags] [model or fxFile]: ^7places a new entity in the map\n^3/entedit <entity id> [attribute] [value]: ^7edits the entity attributes\n^3/entnear: ^7lists entities with a distance to you less than 200 map units\n^3/entlist <page number>: ^7lists all entities of the map. This command lists 10 entities per page\n^3/entremove <entity id>: ^7removes the entity from the map\n\n\"") );
}

/*
=================
ClientCommand
=================
*/

#define CMD_NOINTERMISSION		(1<<0)
#define CMD_CHEAT				(1<<1)
#define CMD_ALIVE				(1<<2)
#define CMD_LOGGEDIN			(1<<3) // zyk: player must be logged in his account
#define CMD_RPG					(1<<4) // zyk: player must be in RPG Mode

typedef struct command_s {
	const char	*name;
	void		(*func)(gentity_t *ent);
	int			flags;
} command_t;

int cmdcmp( const void *a, const void *b ) {
	return Q_stricmp( (const char *)a, ((command_t*)b)->name );
}

/* This array MUST be sorted correctly by alphabetical name field */
command_t commands[] = {
	{ "addbot",				Cmd_AddBot_f,				0 },
	{ "admindown",			Cmd_AdminDown_f,			CMD_LOGGEDIN|CMD_NOINTERMISSION },
	{ "adminlist",			Cmd_AdminList_f,			CMD_LOGGEDIN|CMD_NOINTERMISSION },
	{ "adminup",			Cmd_AdminUp_f,				CMD_LOGGEDIN|CMD_NOINTERMISSION },
	{ "allyadd",			Cmd_AllyAdd_f,				CMD_NOINTERMISSION },
	{ "allylist",			Cmd_AllyList_f,				CMD_NOINTERMISSION },
	{ "allyremove",			Cmd_AllyRemove_f,			CMD_NOINTERMISSION },
	{ "answer",				Cmd_Answer_f,				CMD_RPG|CMD_ALIVE|CMD_NOINTERMISSION },
	{ "bountyquest",		Cmd_BountyQuest_f,			CMD_RPG|CMD_NOINTERMISSION },
	{ "buy",				Cmd_Buy_f,					CMD_RPG|CMD_ALIVE|CMD_NOINTERMISSION },
	{ "callseller",			Cmd_CallSeller_f,			CMD_RPG|CMD_ALIVE|CMD_NOINTERMISSION },
	{ "callteamvote",		Cmd_CallTeamVote_f,			CMD_NOINTERMISSION },
	{ "callvote",			Cmd_CallVote_f,				CMD_NOINTERMISSION },
	{ "changepassword",		Cmd_ChangePassword_f,		CMD_LOGGEDIN|CMD_NOINTERMISSION },
	{ "creditgive",			Cmd_CreditGive_f,			CMD_RPG|CMD_NOINTERMISSION },
	{ "debugBMove_Back",	Cmd_BotMoveBack_f,			CMD_CHEAT|CMD_ALIVE },
	{ "debugBMove_Forward",	Cmd_BotMoveForward_f,		CMD_CHEAT|CMD_ALIVE },
	{ "debugBMove_Left",	Cmd_BotMoveLeft_f,			CMD_CHEAT|CMD_ALIVE },
	{ "debugBMove_Right",	Cmd_BotMoveRight_f,			CMD_CHEAT|CMD_ALIVE },
	{ "debugBMove_Up",		Cmd_BotMoveUp_f,			CMD_CHEAT|CMD_ALIVE },
	{ "down",				Cmd_DownSkill_f,			CMD_RPG|CMD_NOINTERMISSION },
	{ "drop",				Cmd_Drop_f,					CMD_ALIVE|CMD_NOINTERMISSION },
	{ "duelteam",			Cmd_DuelTeam_f,				CMD_NOINTERMISSION },
	{ "entadd",				Cmd_EntAdd_f,				CMD_LOGGEDIN|CMD_NOINTERMISSION },
	{ "entedit",			Cmd_EntEdit_f,				CMD_LOGGEDIN|CMD_NOINTERMISSION },
	{ "entitysystem",		Cmd_EntitySystem_f,			CMD_LOGGEDIN|CMD_NOINTERMISSION },
	{ "entlist",			Cmd_EntList_f,				CMD_LOGGEDIN|CMD_NOINTERMISSION },
	{ "entnear",			Cmd_EntNear_f,				CMD_LOGGEDIN|CMD_NOINTERMISSION },
	{ "entremove",			Cmd_EntRemove_f,			CMD_LOGGEDIN|CMD_NOINTERMISSION },
	{ "follow",				Cmd_Follow_f,				CMD_NOINTERMISSION },
	{ "follownext",			Cmd_FollowNext_f,			CMD_NOINTERMISSION },
	{ "followprev",			Cmd_FollowPrev_f,			CMD_NOINTERMISSION },
	{ "forcechanged",		Cmd_ForceChanged_f,			0 },
	{ "gc",					Cmd_GameCommand_f,			CMD_NOINTERMISSION },
	{ "give",				Cmd_Give_f,					CMD_CHEAT|CMD_ALIVE|CMD_NOINTERMISSION },
	{ "giveother",			Cmd_GiveOther_f,			CMD_CHEAT|CMD_NOINTERMISSION },
	{ "god",				Cmd_God_f,					CMD_CHEAT|CMD_ALIVE|CMD_NOINTERMISSION },
	{ "guardianquest",		Cmd_GuardianQuest_f,		CMD_RPG|CMD_NOINTERMISSION },
	{ "jetpack",			Cmd_Jetpack_f,				CMD_ALIVE|CMD_NOINTERMISSION },
	{ "kill",				Cmd_Kill_f,					CMD_ALIVE|CMD_NOINTERMISSION },
	{ "killother",			Cmd_KillOther_f,			CMD_CHEAT|CMD_NOINTERMISSION },
//	{ "kylesmash",			TryGrapple,					0 },
	{ "levelshot",			Cmd_LevelShot_f,			CMD_CHEAT|CMD_ALIVE|CMD_NOINTERMISSION },
	{ "list",				Cmd_ListAccount_f,			CMD_NOINTERMISSION },
	{ "login",				Cmd_LoginAccount_f,			CMD_NOINTERMISSION },
	{ "logout",				Cmd_LogoutAccount_f,		CMD_LOGGEDIN|CMD_NOINTERMISSION },
	{ "maplist",			Cmd_MapList_f,				CMD_NOINTERMISSION },
	{ "new",				Cmd_NewAccount_f,			CMD_NOINTERMISSION },
	{ "news",				Cmd_News_f,					CMD_NOINTERMISSION },
	{ "noclip",				Cmd_Noclip_f,				CMD_LOGGEDIN|CMD_ALIVE|CMD_NOINTERMISSION },
	{ "notarget",			Cmd_Notarget_f,				CMD_CHEAT|CMD_ALIVE|CMD_NOINTERMISSION },
	{ "npc",				Cmd_NPC_f,					CMD_LOGGEDIN|CMD_ALIVE },
	{ "npclist",			Cmd_NpcList_f,				CMD_NOINTERMISSION },
	{ "playermode",			Cmd_PlayerMode_f,			CMD_LOGGEDIN|CMD_NOINTERMISSION },
	{ "questanswer",		Cmd_QuestAnswer_f,			CMD_RPG|CMD_ALIVE|CMD_NOINTERMISSION },
	{ "racemode",			Cmd_RaceMode_f,				CMD_ALIVE|CMD_NOINTERMISSION },
	{ "resetaccount",		Cmd_ResetAccount_f,			CMD_LOGGEDIN|CMD_NOINTERMISSION },
	{ "rpgclass",			Cmd_RpgClass_f,				CMD_RPG|CMD_NOINTERMISSION },
	{ "say",				Cmd_Say_f,					0 },
	{ "say_team",			Cmd_SayTeam_f,				0 },
	{ "score",				Cmd_Score_f,				0 },
	{ "sell",				Cmd_Sell_f,					CMD_RPG|CMD_ALIVE|CMD_NOINTERMISSION },
	{ "settings",			Cmd_Settings_f,				CMD_LOGGEDIN|CMD_NOINTERMISSION },
	{ "setviewpos",			Cmd_SetViewpos_f,			CMD_CHEAT|CMD_NOINTERMISSION },
	{ "siegeclass",			Cmd_SiegeClass_f,			CMD_NOINTERMISSION },
	{ "stuff",				Cmd_Stuff_f,				CMD_RPG|CMD_ALIVE|CMD_NOINTERMISSION },
	{ "team",				Cmd_Team_f,					CMD_NOINTERMISSION },
//	{ "teamtask",			Cmd_TeamTask_f,				CMD_NOINTERMISSION },
	{ "teamvote",			Cmd_TeamVote_f,				CMD_NOINTERMISSION },
	{ "tele",				Cmd_Teleport_f,				CMD_LOGGEDIN|CMD_ALIVE|CMD_NOINTERMISSION },
	{ "teleport",			Cmd_Teleport_f,				CMD_LOGGEDIN|CMD_ALIVE|CMD_NOINTERMISSION },
	{ "tell",				Cmd_Tell_f,					0 },
	{ "thedestroyer",		Cmd_TheDestroyer_f,			CMD_CHEAT|CMD_ALIVE|CMD_NOINTERMISSION },
	{ "t_use",				Cmd_TargetUse_f,			CMD_CHEAT|CMD_ALIVE },
	{ "up",					Cmd_UpSkill_f,				CMD_RPG|CMD_NOINTERMISSION },
	{ "vehiclelist",		Cmd_VehicleList_f,			CMD_NOINTERMISSION },
	{ "voice_cmd",			Cmd_VoiceCommand_f,			CMD_NOINTERMISSION },
	{ "vote",				Cmd_Vote_f,					CMD_NOINTERMISSION },
	{ "where",				Cmd_Where_f,				CMD_NOINTERMISSION },
};
static const size_t numCommands = ARRAY_LEN( commands );

void ClientCommand( int clientNum ) {
	gentity_t	*ent = NULL;
	char		cmd[MAX_TOKEN_CHARS] = {0};
	command_t	*command = NULL;

	ent = g_entities + clientNum;
	if ( !ent->client || ent->client->pers.connected != CON_CONNECTED ) {
		G_SecurityLogPrintf( "ClientCommand(%d) without an active connection\n", clientNum );
		return;		// not fully in game yet
	}

	trap->Argv( 0, cmd, sizeof( cmd ) );

	//rww - redirect bot commands
	if ( strstr( cmd, "bot_" ) && AcceptBotCommand( cmd, ent ) )
		return;
	//end rww

	command = (command_t *)bsearch( cmd, commands, numCommands, sizeof( commands[0] ), cmdcmp );
	if ( !command )
	{
		trap->SendServerCommand( clientNum, va( "print \"Unknown command %s\n\"", cmd ) );
		return;
	}

	else if ( (command->flags & CMD_NOINTERMISSION)
		&& ( level.intermissionQueued || level.intermissiontime ) )
	{
		trap->SendServerCommand( clientNum, va( "print \"%s (%s)\n\"", G_GetStringEdString( "MP_SVGAME", "CANNOT_TASK_INTERMISSION" ), cmd ) );
		return;
	}

	else if ( (command->flags & CMD_CHEAT)
		&& !sv_cheats.integer )
	{
		trap->SendServerCommand( clientNum, va( "print \"%s\n\"", G_GetStringEdString( "MP_SVGAME", "NOCHEATS" ) ) );
		return;
	}

	else if ( (command->flags & CMD_ALIVE)
		&& (ent->health <= 0
			|| ent->client->tempSpectate >= level.time
			|| ent->client->sess.sessionTeam == TEAM_SPECTATOR) )
	{
		trap->SendServerCommand( clientNum, va( "print \"%s\n\"", G_GetStringEdString( "MP_SVGAME", "MUSTBEALIVE" ) ) );
		return;
	}

	else if ( (command->flags & CMD_LOGGEDIN)
		&& ent->client->sess.amrpgmode == 0 )
	{ // zyk: new condition
		trap->SendServerCommand( clientNum, "print \"You must be logged in\n\"" );
		return;
	}

	else if ( (command->flags & CMD_RPG)
		&& ent->client->sess.amrpgmode < 2 )
	{ // zyk: new condition
		trap->SendServerCommand( clientNum, "print \"You must be in RPG Mode\n\"" );
		return;
	}

	else
		command->func( ent );
}
