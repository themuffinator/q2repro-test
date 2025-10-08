/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "server.h"
#include "common/mapdb.h"

#define SAVE_MAGIC1     MakeLittleLong('S','S','V','2')
#define SAVE_MAGIC2     MakeLittleLong('S','A','V','2')
#define SAVE_VERSION    1

#define SAVE_CURRENT    ".current"
#define SAVE_AUTO       "save0"

// only load saves from home dir
#define SAVE_LOOKUP_FLAGS   (FS_TYPE_REAL | FS_PATH_GAME | FS_DIR_HOME)

typedef enum {
    SAVE_MANUAL,        // manual save
    SAVE_LEVEL_START,   // autosave at level start
    SAVE_AUTOSAVE       // remaster "autosave"
} savetype_t;

typedef enum {
    LOAD_NONE,          // not a savegame load
    LOAD_NORMAL,        // regular savegame, or remaster "autosave"
    LOAD_LEVEL_START,   // autosave at level start
} loadtype_t;

static cvar_t   *sv_noreload;

static bool have_enhanced_savegames(void);

static int write_server_file(savetype_t autosave)
{
    cvar_t      *var;
    int         ret;

    // write magic
    MSG_WriteLong(SAVE_MAGIC1);
    MSG_WriteLong(SAVE_VERSION);

    // write the comment field
    MSG_WriteLong64(time(NULL));
    MSG_WriteByte(autosave);
    MSG_WriteString(sv.configstrings[CS_NAME]);

    // write the mapcmd
    MSG_WriteString(sv.mapcmd);

    // write all CVAR_LATCH cvars - these will be things like coop, skill, deathmatch, etc
    // also write all CVAR_SERVERINFO vars - they mainly serve to provide some troubleshooting info
    for (var = cvar_vars; var; var = var->next) {
        if (!(var->flags & (CVAR_LATCH | CVAR_SERVERINFO)))
            continue;
        if (var->flags & CVAR_PRIVATE)
            continue;
        MSG_WriteString(var->name);
        MSG_WriteString(var->string);
    }
    MSG_WriteString(NULL);

    // check for overflow
    if (msg_write.overflowed) {
        SZ_Clear(&msg_write);
        return -1;
    }

    // write server state
    ret = FS_WriteFile("save/" SAVE_CURRENT "/server.ssv",
                       msg_write.data, msg_write.cursize);

    SZ_Clear(&msg_write);

    if (ret < 0)
        return -1;

    // write game state
    size_t json_size = 0;
    char *game_json = ge->WriteGameJson(autosave == SAVE_LEVEL_START, &json_size);
    if (!game_json)
        return -1;

    ret = FS_WriteFile("save/" SAVE_CURRENT "/game.ssv",
                       game_json, json_size);
    Z_Free(game_json);
    if (ret < 0)
        return -1;

    return 0;
}

static int write_level_file(bool transition)
{
    char        name[MAX_OSPATH];
    int         i, ret;
    char        *s;
    size_t      len;
    byte        portalbits[MAX_MAP_PORTAL_BYTES];
    qhandle_t   f;

    if (Q_snprintf(name, MAX_QPATH, "save/" SAVE_CURRENT "/%s.sv2", sv.name) >= MAX_QPATH)
        return -1;

    FS_OpenFile(name, &f, FS_MODE_WRITE);
    if (!f)
        return -1;

    // write magic
    MSG_WriteLong(SAVE_MAGIC2);
    MSG_WriteLong(SAVE_VERSION);

    // write configstrings
    for (i = 0; i < svs.csr.end; i++) {
        s = sv.configstrings[i];
        if (!s[0])
            continue;

        len = Q_strnlen(s, CS_MAX_STRING_LENGTH);
        MSG_WriteShort(i);
        MSG_WriteData(s, len);
        MSG_WriteByte(0);

        if (msg_write.cursize > msg_write.maxsize / 2) {
            FS_Write(msg_write.data, msg_write.cursize, f);
            SZ_Clear(&msg_write);
        }
    }
    MSG_WriteShort(i);

    len = CM_WritePortalBits(&sv.cm, portalbits);
    MSG_WriteByte(len);
    MSG_WriteData(portalbits, len);

    FS_Write(msg_write.data, msg_write.cursize, f);
    SZ_Clear(&msg_write);

    ret = FS_CloseFile(f);
    if (ret < 0)
        return -1;

    // write game level
    size_t json_size = 0;
    char *level_json = ge->WriteLevelJson(transition, &json_size);
    if (!level_json)
        return -1;

    if (Q_snprintf(name, MAX_QPATH, "save/" SAVE_CURRENT "/%s.sav", sv.name) >= MAX_QPATH)
        ret = -1;
    else
        ret = FS_WriteFile(name, level_json, json_size);
    Z_Free(level_json);

    if (ret < 0)
        return -1;
    return 0;
}

static int copy_file(const char *src, const char *dst, const char *name)
{
    char    path[MAX_OSPATH];
    byte    buf[0x10000];
    FILE    *ifp, *ofp;
    size_t  len, res;
    int     ret = -1;

    if (Q_snprintf(path, MAX_OSPATH, "%s/save/%s/%s", fs_gamedir, src, name) >= MAX_OSPATH)
        goto fail0;

    ifp = fopen(path, "rb");
    if (!ifp)
        goto fail0;

    if (Q_snprintf(path, MAX_OSPATH, "%s/save/%s/%s", fs_gamedir, dst, name) >= MAX_OSPATH)
        goto fail1;

    if (FS_CreatePath(path))
        goto fail1;

    ofp = fopen(path, "wb");
    if (!ofp)
        goto fail1;

    do {
        len = fread(buf, 1, sizeof(buf), ifp);
        res = fwrite(buf, 1, len, ofp);
    } while (len == sizeof(buf) && res == len);

    if (ferror(ifp))
        goto fail2;

    if (ferror(ofp))
        goto fail2;

    ret = 0;
fail2:
    ret |= fclose(ofp);
fail1:
    ret |= fclose(ifp);
fail0:
    return ret;
}

static int remove_file(const char *dir, const char *name)
{
    char path[MAX_OSPATH];

    if (Q_snprintf(path, MAX_OSPATH, "%s/save/%s/%s", fs_gamedir, dir, name) >= MAX_OSPATH)
        return -1;

    return remove(path);
}

static void **list_save_dir(const char *dir, int *count)
{
    return FS_ListFiles(va("save/%s", dir), ".ssv;.sav;.sv2",
        SAVE_LOOKUP_FLAGS | FS_SEARCH_RECURSIVE, count);
}

static int wipe_save_dir(const char *dir)
{
    void **list;
    int i, count, ret = 0;

    if ((list = list_save_dir(dir, &count)) == NULL)
        return 0;

    for (i = 0; i < count; i++)
        ret |= remove_file(dir, list[i]);

    FS_FreeList(list);
    return ret;
}

static int copy_save_dir(const char *src, const char *dst)
{
    void **list;
    int i, count, ret = 0;

    if ((list = list_save_dir(src, &count)) == NULL)
        return -1;

    for (i = 0; i < count; i++)
        ret |= copy_file(src, dst, list[i]);

    FS_FreeList(list);
    return ret;
}

static int read_binary_file(const char *name)
{
    qhandle_t f;
    int64_t len;

    len = FS_OpenFile(name, &f, SAVE_LOOKUP_FLAGS | FS_MODE_READ);
    if (!f)
        return -1;

    if (len > MAX_MSGLEN)
        goto fail;

    if (FS_Read(msg_read_buffer, len, f) != len)
        goto fail;

    SZ_InitRead(&msg_read, msg_read_buffer, len);

    FS_CloseFile(f);
    return 0;

fail:
    FS_CloseFile(f);
    return -1;
}

// try to fetch a friendly name from the given title.
// - for strings with EOUs, check if there's a matching unit
//   that we can pull the unit name from.
// - if no EOU, check if the BSP exists at all stand-alone.
static void SV_GetFriendlyMapDBTitle(char *name, size_t name_len)
{
    int i = 0;
    const mapdb_t *mapdb = MapDB_Get();
    const char *bsp_name = strrchr(name, '+');

    if (!bsp_name)
        bsp_name = name;
    else
        bsp_name++;

    if (*bsp_name == '*')
        bsp_name++;

    mapcmd_t cmd = { 0 };
    Q_strlcpy(cmd.buffer, bsp_name, sizeof(cmd.buffer));

    if (!SV_ParseMapCmd(&cmd))
        return;

    // the BSP needs to exist for short episode listing
    // TODO: hash mapping for names
    const mapdb_map_t *mapdb_map = NULL;

    for (mapdb_map = mapdb->maps, i = 0; i < mapdb->num_maps; mapdb_map++, i++)
        if (!mapdb_map->sp && !strcmp(mapdb_map->bsp, cmd.server))
            break;

    // no idea what map this came from
    if (i == mapdb->num_maps)
        return;

    // find a matching unit
    const mapdb_map_t *mapdb_unit = NULL;
    
    for (mapdb_unit = mapdb->maps, i = 0; i < mapdb->num_maps; mapdb_unit++, i++)
        if (mapdb_unit->sp && !strcmp(mapdb_unit->bsp, name))
            break;

    if (i != mapdb->num_maps) {
        Q_snprintf(name, name_len, "[%s] %s", mapdb_map->short_name, mapdb_unit->title);
        return;
    }
    
    Q_snprintf(name, name_len, "[%s] %s", mapdb_map->short_name, mapdb_map->title);
}

char *SV_GetSaveInfo(const char *dir)
{
    char        name[MAX_QPATH], date[MAX_QPATH];
    size_t      len;
    int64_t     timestamp;
    int         autosave, year;
    time_t      t;
    struct tm   *tm;

    if (Q_snprintf(name, MAX_QPATH, "save/%s/server.ssv", dir) >= MAX_QPATH)
        return NULL;

    if (read_binary_file(name))
        return NULL;

    if (MSG_ReadLong() != SAVE_MAGIC1)
        return NULL;

    if (MSG_ReadLong() != SAVE_VERSION)
        return NULL;

    // read the comment field
    timestamp = MSG_ReadLong64();
    autosave = MSG_ReadByte();
    MSG_ReadString(NULL, 0);
    MSG_ReadString(name, sizeof(name));

    SV_GetFriendlyMapDBTitle(name, sizeof(name));

    if (autosave == SAVE_LEVEL_START)
        return Z_CopyString(va("ENTERING %s", name));

    if (autosave == SAVE_AUTOSAVE)
        return Z_CopyString(va("AUTOSAVE %s", name));

    // get current year
    t = time(NULL);
    tm = localtime(&t);
    year = tm ? tm->tm_year : -1;

    // format savegame date
    t = timestamp;
    len = 0;
    if ((tm = localtime(&t)) != NULL) {
        if (tm->tm_year == year)
            len = strftime(date, sizeof(date), "%b %d %H:%M", tm);
        else
            len = strftime(date, sizeof(date), "%b %d  %Y", tm);
    }
    if (!len)
        strcpy(date, "???");

    return Z_CopyString(va("%s %s", date, name));
}

static void abort_func(void *arg)
{
    CM_FreeMap(arg);
}

static int read_server_file(void)
{
    char        name[MAX_OSPATH], string[MAX_STRING_CHARS];
    mapcmd_t    cmd;
    void        *buf;

    // errors like missing file, bad version, etc are
    // non-fatal and just return to the command handler
    if (read_binary_file("save/" SAVE_CURRENT "/server.ssv"))
        return -1;

    if (MSG_ReadLong() != SAVE_MAGIC1)
        return -1;

    if (MSG_ReadLong() != SAVE_VERSION)
        return -1;

    memset(&cmd, 0, sizeof(cmd));

    // read the comment field
    MSG_ReadLong64();
    if (MSG_ReadByte() == SAVE_LEVEL_START)
        cmd.loadgame = LOAD_LEVEL_START;
    else
        cmd.loadgame = LOAD_NORMAL;
    MSG_ReadString(NULL, 0);

    // read the mapcmd
    if (MSG_ReadString(cmd.buffer, sizeof(cmd.buffer)) >= sizeof(cmd.buffer))
        return -1;

    // now try to load the map
    if (!SV_ParseMapCmd(&cmd))
        return -1;

    // save pending CM to be freed later if ERR_DROP is thrown
    Com_AbortFunc(abort_func, &cmd.cm);

    // any error will drop from this point
    SV_Shutdown("Server restarted\n", ERR_RECONNECT);

    // the rest can't underflow
    msg_read.allowunderflow = false;

    // read all CVAR_LATCH cvars
    // these will be things like coop, skill, deathmatch, etc
    while (1) {
        if (MSG_ReadString(name, MAX_QPATH) >= MAX_QPATH)
            Com_Error(ERR_DROP, "Savegame cvar name too long");
        if (!name[0])
            break;

        if (MSG_ReadString(string, sizeof(string)) >= sizeof(string))
            Com_Error(ERR_DROP, "Savegame cvar value too long");

        /* we store cvars with either CVAR_LATCH or CVAR_SERVERINFO,
         * but only restore those with CVAR_LATCH */
        cvar_t *cvar = Cvar_FindVar(name);
        if (!cvar || cvar->flags & CVAR_LATCH)
            Cvar_UserSet(name, string);
    }

    // start a new game fresh with new cvars
    SV_InitGame(MVD_SPAWN_DISABLED);

    // error out immediately if game doesn't support safe savegames
    if (!have_enhanced_savegames())
        Com_Error(ERR_DROP, "Game does not support enhanced savegames");

    // read game state
    FS_LoadFile("save/" SAVE_CURRENT "/game.ssv", (void **)&buf);
    if (!buf)
        Com_Error(ERR_DROP, "Couldn't read game.ssv");
    ge->ReadGameJson(buf);
    Z_Free(buf);

    // clear pending CM
    Com_AbortFunc(NULL, NULL);

    // go to the map
    SV_SpawnServer(&cmd);
    return 0;
}

static int read_level_file(void)
{
    char    name[MAX_OSPATH];
    size_t  len, maxlen;
    int     index;
    void    *data;

    if (Q_snprintf(name, MAX_QPATH, "save/" SAVE_CURRENT "/%s.sv2", sv.name) >= MAX_QPATH)
        return -1;

    len = FS_LoadFileEx(name, &data, SAVE_LOOKUP_FLAGS, TAG_SERVER);
    if (!data)
        return -1;

    SZ_InitRead(&msg_read, data, len);

    if (MSG_ReadLong() != SAVE_MAGIC2) {
        FS_FreeFile(data);
        return -1;
    }

    if (MSG_ReadLong() != SAVE_VERSION) {
        FS_FreeFile(data);
        return -1;
    }

    // any error will drop from this point
    Com_AbortFunc(Z_Free, data);

    // the rest can't underflow
    msg_read.allowunderflow = false;

    // read all configstrings
    while (1) {
        index = MSG_ReadWord();
        if (index == svs.csr.end)
            break;

        if (index < 0 || index >= svs.csr.end)
            Com_Error(ERR_DROP, "Bad savegame configstring index");

        maxlen = Com_ConfigstringSize(&svs.csr, index);
        if (MSG_ReadString(sv.configstrings[index], maxlen) >= maxlen)
            Com_Error(ERR_DROP, "Savegame configstring too long");
    }

    SV_ClearWorld();

    len = MSG_ReadByte();
    CM_SetPortalStates(&sv.cm, MSG_ReadData(len), len);

    Com_AbortFunc(NULL, NULL);
    FS_FreeFile(data);

    // read game level
    if (Q_snprintf(name, MAX_OSPATH, "save/" SAVE_CURRENT "/%s.sav", sv.name) >= MAX_OSPATH)
        Com_Error(ERR_DROP, "Savegame path too long");

    FS_LoadFile(name, (void **)&data);
    if (!data)
        Com_Error(ERR_DROP, "Couldn't read %s", name);
    ge->ReadLevelJson(data);
    Z_Free(data);
    return 0;
}

static bool no_save_games(void)
{
    if (!have_enhanced_savegames())
        return true;

    if (Cvar_VariableInteger("deathmatch"))
        return true;

    return false;
}

bool SV_AutoSaveBegin(const mapcmd_t *cmd)
{
    byte        bitmap[MAX_CLIENTS / CHAR_BIT];
    edict_t     *ent;
    int         i;

    // check for clearing the current savegame
    if (cmd->endofunit) {
        wipe_save_dir(SAVE_CURRENT);
        return false;
    }

    if (sv.state != ss_game)
        return false;

    if (no_save_games())
        return false;

    memset(bitmap, 0, sizeof(bitmap));

    // clear all the client inuse flags before saving so that
    // when the level is re-entered, the clients will spawn
    // at spawn points instead of occupying body shells
    for (i = 0; i < svs.maxclients; i++) {
        ent = EDICT_NUM(i + 1);
        if (ent->inuse) {
            Q_SetBit(bitmap, i);
            ent->inuse = false;
        }
    }

    // save the map just exited
    if (write_level_file(true))
        Com_EPrintf("Couldn't write level file.\n");

    // we must restore these for clients to transfer over correctly
    for (i = 0; i < svs.maxclients; i++) {
        ent = EDICT_NUM(i + 1);
        ent->inuse = Q_IsBitSet(bitmap, i);
    }

    return true;
}

void SV_AutoSaveEnd(void)
{
    // save server state
    if (write_server_file(SAVE_LEVEL_START)) {
        Com_EPrintf("Couldn't write server file.\n");
        return;
    }

    // clear whatever savegames are there
    if (wipe_save_dir(SAVE_AUTO)) {
        Com_EPrintf("Couldn't wipe '%s' directory.\n", SAVE_AUTO);
        return;
    }

    // copy off the level to the autosave slot
    if (copy_save_dir(SAVE_CURRENT, SAVE_AUTO)) {
        Com_EPrintf("Couldn't write '%s' directory.\n", SAVE_AUTO);
        return;
    }
}

void SV_CheckForSavegame(const mapcmd_t *cmd)
{
    int frames;

    if (no_save_games())
        return;
    if (sv_noreload->integer)
        return;

    if (read_level_file()) {
        // only warn when loading a regular savegame. autosave without level
        // file is ok and simply starts the map from the beginning.
        if (cmd->loadgame == LOAD_NORMAL)
            Com_EPrintf("Couldn't read level file.\n");
        return;
    }

    if (cmd->loadgame == LOAD_NORMAL) {
        // called from SV_Loadgame_f
        frames = 2;
    } else {
        // coming back to a level after being in a different
        // level, so run it for ten seconds
        frames = 10 * SV_FRAMERATE;
    }

    for (int i = 0; i < frames; i++, sv.framenum++)
        ge->RunFrame(false);
}

static bool have_enhanced_savegames(void)
{
    return (g_features->integer & GMF_ENHANCED_SAVEGAMES)
        || (svs.gamedetecthack == 4) || sys_allow_unsafe_savegames->integer;
}

void SV_CheckForEnhancedSavegames(void)
{
    if (Cvar_VariableInteger("deathmatch"))
        return;

    if (g_features->integer & GMF_ENHANCED_SAVEGAMES)
        Com_Printf("Game supports Q2PRO enhanced savegames.\n");
    else if (svs.gamedetecthack == 4)
        Com_Printf("Game supports YQ2 enhanced savegames.\n");
    else if (sys_allow_unsafe_savegames->integer)
        Com_WPrintf("Use of unsafe savegames forced from command line.\n");
    else
        Com_WPrintf("Game does not support enhanced savegames. Savegames will not work.\n");
}

static void SV_Savegame_c(genctx_t *ctx, int argnum)
{
    if (argnum == 1) {
        FS_File_g("save", NULL, SAVE_LOOKUP_FLAGS | FS_SEARCH_DIRSONLY, ctx);
    }
}

static void SV_Loadgame_f(void)
{
    char *dir;

    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: %s <directory>\n", Cmd_Argv(0));
        return;
    }

    dir = Cmd_Argv(1);
    if (!COM_IsPath(dir)) {
        Com_Printf("Bad savedir.\n");
        return;
    }

    // make sure the server files exist
    if (!FS_FileExistsEx(va("save/%s/server.ssv", dir), SAVE_LOOKUP_FLAGS) ||
        !FS_FileExistsEx(va("save/%s/game.ssv", dir), SAVE_LOOKUP_FLAGS)) {
        Com_Printf("No such savegame: %s\n", dir);
        return;
    }

    // clear whatever savegames are there
    if (wipe_save_dir(SAVE_CURRENT)) {
        Com_Printf("Couldn't wipe '%s' directory.\n", SAVE_CURRENT);
        return;
    }

    // copy it off
    if (copy_save_dir(dir, SAVE_CURRENT)) {
        Com_Printf("Couldn't read '%s' directory.\n", dir);
        return;
    }

    // read server state
    if (read_server_file()) {
        Com_Printf("Couldn't read server file.\n");
        return;
    }
}

static void SV_Savegame_f(void)
{
    char *dir;
    savetype_t type;

    if (sv.state != ss_game) {
        Com_Printf("You must be in a game to save.\n");
        return;
    }

    // don't bother saving if we can't read them back!
    if (!have_enhanced_savegames()) {
        Com_Printf("Game does not support enhanced savegames.\n");
        return;
    }

    if (Cvar_VariableInteger("deathmatch")) {
        Com_Printf("Can't savegame in a deathmatch.\n");
        return;
    }

    if (!ge->CanSave())
        return;

    if (!strcmp(Cmd_Argv(0), "autosave")) {
        dir = "save1";
        type = SAVE_AUTOSAVE;
    } else {
    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: %s <directory>\n", Cmd_Argv(0));
        return;
    }

    dir = Cmd_Argv(1);
    if (!COM_IsPath(dir)) {
        Com_Printf("Bad savedir.\n");
        return;
    }
        type = SAVE_MANUAL;
    }

    // archive current level, including all client edicts.
    // when the level is reloaded, they will be shells awaiting
    // a connecting client
    if (write_level_file(false)) {
        Com_Printf("Couldn't write level file.\n");
        return;
    }

    // save server state
    if (write_server_file(type)) {
        Com_Printf("Couldn't write server file.\n");
        return;
    }

    // clear whatever savegames are there
    if (wipe_save_dir(dir)) {
        Com_Printf("Couldn't wipe '%s' directory.\n", dir);
        return;
    }

    // copy it off
    if (copy_save_dir(SAVE_CURRENT, dir)) {
        Com_Printf("Couldn't write '%s' directory.\n", dir);
        return;
    }

    Com_Printf("Game saved.\n");
}

static const cmdreg_t c_savegames[] = {
    { "autosave", SV_Savegame_f },
    { "save", SV_Savegame_f, SV_Savegame_c },
    { "load", SV_Loadgame_f, SV_Savegame_c },
    { NULL }
};

void SV_RegisterSavegames(void)
{
    sv_noreload = Cvar_Get("sv_noreload", "0", 0);

    Cmd_Register(c_savegames);
}
