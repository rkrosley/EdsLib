/*
 * LEW-19710-1, CCSDS SOIS Electronic Data Sheet Implementation
 * 
 * Copyright (c) 2020 United States Government as represented by
 * the Administrator of the National Aeronautics and Space Administration.
 * All Rights Reserved.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *    http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


/**
 * \file     eds2cfetbl.c
 * \ingroup  cfecfs
 * \author   joseph.p.hickey@nasa.gov
 *
 * Build-time utility to write table files from C source files using edslib
 * Replacement for the old "elf2cfetbl" utility
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <expat.h>
#include <getopt.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "edslib_init.h"

/* compatibility shim to support compilation with Lua5.1 */
#include "edslib_lua51_compatibility.h"

#include "cfe_mission_eds_parameters.h"
#include "cfe_mission_eds_interface_parameters.h"
#include "cfe_tbl_eds_typedefs.h"
#include "cfe_fs_eds_typedefs.h"
#include "edslib_lua_objects.h"
#include "cfe_missionlib_lua_softwarebus.h"


const char LUA_APPLIST_KEY = 0;

void LoadTemplateFile(lua_State *lua, const char *Filename)
{
    void *dlhandle;
    void *objptr;
    const char *tmpstr;
    int obj_idx;
    void *tgtptr;
    size_t tgtsize;
    ptrdiff_t NameLen;
    CFE_TBL_FileDef_t *CFE_TBL_FileDefPtr;
    BASE_TYPES_ApiName_String_t AppName;
    BASE_TYPES_ApiName_String_t TableName;

    dlerror();
    dlhandle = dlopen(Filename, RTLD_LAZY | RTLD_LOCAL);
    tmpstr = dlerror();
    if (tmpstr != NULL || dlhandle == NULL)
    {
        if (tmpstr == NULL)
        {
            tmpstr = "[Unknown Error]";
        }
        fprintf(stderr,"Load Error: %s\n", tmpstr);
        exit(EXIT_FAILURE);
    }

    CFE_TBL_FileDefPtr = (CFE_TBL_FileDef_t *)dlsym(dlhandle, "CFE_TBL_FileDef");
    tmpstr = dlerror();
    if (CFE_TBL_FileDefPtr == NULL || tmpstr != NULL)
    {
        if (tmpstr == NULL)
        {
            tmpstr = "[Table object is NULL]";
        }
        fprintf(stderr,"Error looking up CFE_TBL_FileDef: %s\n", tmpstr);
        exit(EXIT_FAILURE);
    }

    objptr = dlsym(dlhandle, CFE_TBL_FileDefPtr->ObjectName);
    tmpstr = dlerror();
    if (objptr == NULL || tmpstr != NULL)
    {
        if (tmpstr == NULL)
        {
            tmpstr = "[Object pointer is NULL]";
        }
        fprintf(stderr,"Lookup Error: %s\n", tmpstr);
        exit(EXIT_FAILURE);
    }

    /* The TableName in the FileDef object is supposed to be of the form "AppName.TableName" -
     * first split this into its constituent parts */
    tmpstr = strchr(CFE_TBL_FileDefPtr->TableName, '.');
    if (tmpstr == NULL)
    {
        NameLen = 0;
        tmpstr = CFE_TBL_FileDefPtr->TableName;
    }
    else
    {
        NameLen = tmpstr - CFE_TBL_FileDefPtr->TableName;
        ++tmpstr;
    }

    if (NameLen >= sizeof(AppName))
    {
        NameLen = sizeof(AppName) - 1;
    }
    memcpy(AppName, CFE_TBL_FileDefPtr->TableName, NameLen);
    AppName[NameLen] = 0;

    strncpy(TableName, tmpstr, sizeof(TableName) - 1);
    TableName[sizeof(TableName) - 1] = 0;

    lua_getglobal(lua, AppName);
    if (lua_type(lua, -1) != LUA_TTABLE)
    {
        lua_pop(lua, 1);
        lua_newtable(lua);
        lua_pushvalue(lua, -1);
        lua_setglobal(lua, AppName);
    }

    lua_newtable(lua);
    obj_idx = lua_gettop(lua);
    lua_getglobal(lua, "EdsDB");
    lua_getfield(lua, -1, "NewObject");
    lua_pushnumber(lua, (double)(CFE_TBL_FileDefPtr->ObjectSize));
    lua_pcall(lua, 1, 1, 1);    /* Call "MISSION_New_Object" for the object type */
    EdsLib_LuaBinding_GetNativeObject(lua, -1, &tgtptr, &tgtsize);
    if (tgtsize > 0)
    {
        memcpy(tgtptr, objptr, tgtsize);
    }
    lua_setfield(lua, obj_idx, "Content");  /* Set "Content" in Table object */
    lua_settop(lua, obj_idx);

    lua_pushstring(lua, CFE_TBL_FileDefPtr->TableName);
    lua_setfield(lua, obj_idx, "TableName");  /* Set "TableName" in Table object */

    lua_pushstring(lua, CFE_TBL_FileDefPtr->Description);
    lua_setfield(lua, obj_idx, "Description");  /* Set "Description" in Table object */

    lua_pushstring(lua, CFE_TBL_FileDefPtr->TgtFilename);
    lua_setfield(lua, obj_idx, "TgtFilename");  /* Set "TgtFilename" in Table object */

    printf("Loaded Template: %s/%s\n", AppName, TableName);
    dlclose(dlhandle);

    /* Append the Applist table to hold the loaded table template objects */
    lua_rawgetp(lua, LUA_REGISTRYINDEX, &LUA_APPLIST_KEY);
    lua_pushvalue(lua, obj_idx);
    lua_rawseti(lua, -2, 1 + lua_rawlen(lua, -2));

    lua_setfield(lua, -2, TableName);  /* Set entry in App table */
    lua_pop(lua, 1);
}

void LoadLuaFile(lua_State *lua, const char *Filename)
{
    if (luaL_loadfile(lua, Filename) != LUA_OK)
    {
        fprintf(stderr,"Cannot load Lua file: %s: %s\n", Filename, lua_tostring(lua, -1));
        exit(EXIT_FAILURE);
    }
    printf("Executing LUA: %s\n", Filename);
    if (lua_pcall(lua, 0, 0, 1) != LUA_OK)
    {
        /* note - error handler already displayed error message */
        fprintf(stderr, "Failed to execute: %s\n", Filename);
        exit(EXIT_FAILURE);
    }
}

void PushEncodedBlob(lua_State *lua)
{
    luaL_checkudata(lua, -1, "EdsLib_Object");
    lua_getglobal(lua, "EdsDB");
    lua_getfield(lua, -1, "Encode");
    lua_remove(lua, -2);     /* remove the EdsDB global */
    lua_pushvalue(lua, -2);
    lua_call(lua, 1, 1);
}

int Write_GenericFile(lua_State *lua)
{
    FILE *OutputFile = NULL;
    const char *OutputName = luaL_checkstring(lua, 1);

    OutputFile = fopen(OutputName, "w");
    if (OutputFile == NULL)
    {
        return luaL_error(lua, "%s: %s", OutputName, strerror(errno));
    }

    PushEncodedBlob(lua);
    fwrite(lua_tostring(lua, -1), lua_rawlen(lua, -1), 1, OutputFile);
    fclose(OutputFile);
    lua_pop(lua, 1);

    return 0;
}

int Write_CFE_EnacapsulationFile(lua_State *lua)
{
    union
    {
        CFE_FS_Header_t FileHeader;
        CFE_TBL_File_Hdr_t TblHeader;
    } Buffer;
    CFE_FS_Header_PackedBuffer_t PackedFileHeader;
    CFE_TBL_File_Hdr_PackedBuffer_t PackedTblHeader;
    uint32_t TblHeaderBlockSize;
    uint32_t FileHeaderBlockSize;
    EdsLib_Id_t PackedEdsId;
    FILE *OutputFile = NULL;
    const char *OutputName = luaL_checkstring(lua, 1);
    EdsLib_DataTypeDB_TypeInfo_t BlockInfo;

    PushEncodedBlob(lua);

    memset(&Buffer, 0, sizeof(Buffer));
    Buffer.TblHeader.NumBytes = lua_rawlen(lua, -1);
    strncpy(Buffer.TblHeader.TableName, luaL_checkstring(lua, 3), sizeof(Buffer.TblHeader.TableName));

    PackedEdsId = EDSLIB_MAKE_ID(EDS_INDEX(CFE_TBL), CFE_TBL_File_Hdr_DATADICTIONARY);
    EdsLib_DataTypeDB_PackCompleteObject(&EDS_DATABASE, &PackedEdsId,
            PackedTblHeader, &Buffer.TblHeader, sizeof(PackedTblHeader) * 8, sizeof(Buffer.TblHeader));
    EdsLib_DataTypeDB_GetTypeInfo(&EDS_DATABASE, PackedEdsId, &BlockInfo);
    TblHeaderBlockSize = (BlockInfo.Size.Bits + 7) / 8;

    memset(&Buffer, 0, sizeof(Buffer));
    Buffer.FileHeader.ContentType = CFE_FS_FILE_CONTENT_ID;
    Buffer.FileHeader.SubType = luaL_checkinteger(lua, lua_upvalueindex(1));
    Buffer.FileHeader.Length = TblHeaderBlockSize + lua_rawlen(lua, -1);
    snprintf(Buffer.FileHeader.Description, sizeof(Buffer.FileHeader.Description), "%s", luaL_checkstring(lua, 2));

    PackedEdsId = EDSLIB_MAKE_ID(EDS_INDEX(CFE_FS), CFE_FS_Header_DATADICTIONARY);
    EdsLib_DataTypeDB_PackCompleteObject(&EDS_DATABASE, &PackedEdsId,
            PackedFileHeader, &Buffer, sizeof(PackedFileHeader) * 8, sizeof(Buffer));
    EdsLib_DataTypeDB_GetTypeInfo(&EDS_DATABASE, PackedEdsId, &BlockInfo);
    FileHeaderBlockSize = (BlockInfo.Size.Bits + 7) / 8;

    OutputFile = fopen(OutputName, "w");
    if (OutputFile == NULL)
    {
        return luaL_error(lua, "%s: %s", OutputName, strerror(errno));
    }

    fwrite(PackedFileHeader, FileHeaderBlockSize, 1, OutputFile);
    fwrite(PackedTblHeader, TblHeaderBlockSize, 1, OutputFile);
    fwrite(lua_tostring(lua, -1), lua_rawlen(lua, -1), 1, OutputFile);
    fclose(OutputFile);
    printf("Wrote File: %s\n", OutputName);
    lua_pop(lua, 1);

    return 0;
}

static int ErrorHandler(lua_State *lua)
{
    lua_getglobal(lua, "debug");
    lua_getfield(lua, -1, "traceback");
    lua_remove(lua, -2);
    lua_pushnil(lua);
    lua_pushinteger(lua, 3);
    lua_call(lua, 2, 1);
    fprintf(stderr, "Error: %s\n%s\n", lua_tostring(lua, 1), lua_tostring(lua, 2));
    return 0;
}

int main(int argc, char *argv[])
{
    int arg;
    size_t slen;
    lua_State *lua;

    EdsLib_Initialize();

    /* Create a Lua state and register all of our local test functions */
    lua = luaL_newstate();
    luaL_openlibs(lua);

    /* Stack index 1 will be the error handler function (used for protected calls) */
    lua_pushcfunction(lua, ErrorHandler);

    /* Create an Applist table to hold the loaded table template objects */
    lua_newtable(lua);
    lua_rawsetp(lua, LUA_REGISTRYINDEX, &LUA_APPLIST_KEY);

    while ((arg = getopt (argc, argv, "e:")) != -1)
    {
       switch (arg)
       {
       case 'e':
          if (luaL_loadstring(lua, optarg) != LUA_OK)
          {
              fprintf(stderr,"ERROR: Invalid command line expression: %s\n",
                      luaL_tolstring(lua,-1, NULL));
              lua_pop(lua, 2);
              return EXIT_FAILURE;
          }

          if (lua_pcall(lua, 0, 0, 1) != LUA_OK)
          {
              fprintf(stderr,"ERROR: Cannot evaluate command line expression: %s\n",
                      luaL_tolstring(lua,-1, NULL));
              lua_pop(lua, 2);
              return EXIT_FAILURE;
          }
          break;

       case '?':
          if (isprint (optopt))
          {
             fprintf(stderr, "Unknown option `-%c'.\n", optopt);
          }
          else
          {
             fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
          }
          return EXIT_FAILURE;
          break;

       default:
          return EXIT_FAILURE;
          break;
       }
    }

    /* The CPUNAME and CPUNUMBER variables should both be defined.
     * If only one was provided then look up the other value in EDS */
    lua_getglobal(lua, "CPUNAME");
    lua_getglobal(lua, "CPUNUMBER");
    if (lua_isstring(lua, -2) && lua_isnil(lua, -1))
    {
        lua_pushinteger(lua, CFE_MissionLib_GetInstanceNumber(&CFE_SOFTWAREBUS_INTERFACE,
                lua_tostring(lua, -2)));
        lua_setglobal(lua, "CPUNUMBER");
    }
    else if (lua_isnumber(lua, -1) && lua_isnil(lua, -2))
    {
        char TempBuf[64];
        lua_pushstring(lua, CFE_MissionLib_GetInstanceName(&CFE_SOFTWAREBUS_INTERFACE,
                lua_tointeger(lua, -1), TempBuf, sizeof(TempBuf)));
        lua_setglobal(lua, "CPUNAME");
    }
    lua_pop(lua, 2);

    /*
     * if the SIMULATION compile-time directive is set, create a global
     * Lua variable with that name so Lua code can do "if SIMULATION" statements
     * as needed to support simulated environments.
     *
     * Note that a double macro is needed here, to force expansion of the macro
     * rather than stringifying the macro name itself i.e. "SIMULATION"
     */
#ifdef SIMULATION
#define EDS2CFETBL_STRINGIFY(x)     #x
#define EDS2CFETBL_SIMSTRING(x)     EDS2CFETBL_STRINGIFY(x)
    lua_pushstring(lua, EDS2CFETBL_SIMSTRING(SIMULATION));
    lua_setglobal(lua, "SIMULATION");
#endif

    lua_pushinteger(lua, CFE_FS_SubType_TBL_IMG);
    lua_pushcclosure(lua, Write_CFE_EnacapsulationFile, 1);
    lua_setglobal(lua, "Write_CFE_LoadFile");

    lua_pushcfunction(lua, Write_GenericFile);
    lua_setglobal(lua, "Write_GenericFile");

    EdsLib_Lua_Attach(lua, &EDS_DATABASE);
    CFE_MissionLib_Lua_SoftwareBus_Attach(lua, &CFE_SOFTWAREBUS_INTERFACE);
    lua_setglobal(lua, "EdsDB");

    for (arg = optind; arg < argc; arg++)
    {
        /* assume the argument is a file to execute/load */
        slen = strlen(argv[arg]);
        if (slen >= 3 && strcmp(argv[arg] + slen - 3, ".so") == 0)
        {
            LoadTemplateFile(lua, argv[arg]);
        }
        else if (slen >= 4 && strcmp(argv[arg] + slen - 4, ".lua") == 0)
        {
            LoadLuaFile(lua, argv[arg]);
        }
        else
        {
            fprintf(stderr,"WARNING: Unable to handle file argument: %s\n",argv[arg]);
            return EXIT_FAILURE;
        }
    }

    lua_rawgetp(lua, LUA_REGISTRYINDEX, &LUA_APPLIST_KEY);
    lua_pushnil(lua);
    while (lua_next(lua, -2))
    {
        luaL_checktype(lua, -1, LUA_TTABLE);
        lua_getglobal(lua, "Write_CFE_LoadFile");
        assert(lua_type(lua, -1) == LUA_TFUNCTION);
        lua_pushstring(lua, "TgtFilename");
        lua_gettable(lua, -3);
        lua_pushstring(lua, "Description");
        lua_gettable(lua, -4);
        lua_pushstring(lua, "TableName");
        lua_gettable(lua, -5);
        lua_pushstring(lua, "Content");
        lua_gettable(lua, -6);
        assert(lua_type(lua, -5) == LUA_TFUNCTION);
        if (lua_pcall(lua, 4, 0, 1) != LUA_OK)
        {
            /* note - more descriptive error message already printed by error handler function */
            fprintf(stderr, "Failed to write CFE table file from template\n");
            return (EXIT_FAILURE);
        }

        lua_pop(lua, 1);
    }
    lua_pop(lua, 1); /* Applist Global */

    return EXIT_SUCCESS;
}
