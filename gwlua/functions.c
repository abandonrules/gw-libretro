#include <gwlua.h>

#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <bsreader.h>

#include "lua/class.h"
#include "lua/classes.h"
#include "lua/controls.h"
#include "lua/dialogs.h"
#include "lua/extctrls.h"
#include "lua/fmod.h"
#include "lua/fmodtypes.h"
#include "lua/forms.h"
#include "lua/graphics.h"
#include "lua/jpeg.h"
#include "lua/math.h"
#include "lua/messages.h"
#include "lua/registry.h"
#include "lua/stdctrls.h"
#include "lua/sysutils.h"
#include "lua/windows.h"

#include "lua/system.h"

#define get_state( L ) ( ( gwlua_t* )lua_touserdata( L, lua_upvalueindex( 1 ) ) )

static int l_playsound( lua_State* L )
{
  gwlua_sound_t* sound = (gwlua_sound_t*)luaL_checkudata( L, 1, "sound" );
  gwlua_play_sound( sound );
  return 0;
}

static int l_stopsounds( lua_State* L )
{
  gwlua_t* state = get_state( L );
  gwlua_stop_all_sounds( state );
  return 0;
}

static int l_randomize( lua_State* L )
{
  gwlua_t* state = get_state( L );
  state->seed = time( NULL );
  return 0;
}

static int l_random( lua_State* L )
{
  gwlua_t* state = get_state( L );
  state->seed = 6364136223846793005ULL * state->seed + 1;
  double frac = ( state->seed >> 11 ) / 9007199254740992.0;
  
  if ( lua_isnumber( L, 1 ) )
  {
    lua_pushinteger( L, (int64_t)( frac * lua_tointeger( L, 1 ) ) );
  }
  else
  {
    lua_pushnumber( L, frac );
  }
  
  return 1;
}

static int l_round( lua_State* L )
{
  double x = luaL_checknumber( L, 1 );
  double f = floor( x );
  double c = ceil( x );
  double d1 = x - f;
  double d2 = c - x;
  
  if ( d1 < d2 )
  {
    lua_pushnumber( L, f );
  }
  else if ( d1 > d2 )
  {
    lua_pushnumber( L, c );
  }
  else
  {
    if ( 1 & (int64_t)f )
    {
      lua_pushnumber( L, c );
    }
    else
    {
      lua_pushnumber( L, f );
    }
  }
  
  return 1;
}

static int l_now( lua_State* L )
{
  lua_pushinteger( L, time( NULL ) );
  return 1;
}

static int l_splittime( lua_State* L )
{
  time_t tt = (time_t)luaL_checkinteger( L, 1 );
  struct tm* tm = localtime( &tt );
  
  lua_pushinteger( L, tm->tm_hour );
  lua_pushinteger( L, tm->tm_min );
  lua_pushinteger( L, tm->tm_sec );
  lua_pushinteger( L, 0 );
  lua_pushinteger( L, tm->tm_mday );
  lua_pushinteger( L, tm->tm_mon + 1 );
  lua_pushinteger( L, tm->tm_year + 1900 );
  return 7;
}

static int l_inttostr( lua_State* L )
{
  lua_tostring( L, 1 );
  lua_pushvalue( L, 1 );
  return 1;
}

static int l_loadvalue( lua_State* L )
{
  gwlua_t* state = get_state( L );
  const char* key = luaL_checkstring( L, 1 );
  
  int type;
  const char* value = gwlua_load_value( state, key, &type );
  
  if ( value )
  {
    switch ( type )
    {
    case GWLUA_NULL:
    default:
      lua_pushnil( L );
      break;
      
    case GWLUA_BOOLEAN:
      lua_pushboolean( L, !strcmp( value, "true" ) );
      break;
      
    case GWLUA_NUMBER:
      if ( !lua_stringtonumber( L, value ) )
      {
        lua_pushinteger( L, 0 );
      }
      break;
      
    case GWLUA_STRING:
      lua_pushstring( L, value );
      break;
    }
  }
  else
  {
    lua_pushnil( L );
  }
  
  return 1;
}

static int l_savevalue( lua_State* L )
{
  gwlua_t* state = get_state( L );
  const char* key = luaL_checkstring( L, 1 );
  
  switch ( lua_type( L, 2 ) )
  {
  case LUA_TBOOLEAN:
    gwlua_save_value( state, key, lua_toboolean( L, 2 ) ? "true" : "false", GWLUA_BOOLEAN );
    break;
    
  case LUA_TNUMBER:
    gwlua_save_value( state, key, lua_tostring( L, 2 ), GWLUA_NUMBER );
    break;
    
  case LUA_TSTRING:
    gwlua_save_value( state, key, lua_tostring( L, 2 ), GWLUA_STRING );
    break;
    
  default:
    gwlua_save_value( state, key, NULL, GWLUA_NULL );
    break;
  }
  
  return 1;
}

static int l_setbackground( lua_State* L )
{
  gwlua_t* state = get_state( L );
  gwlua_picture_t* bg = (gwlua_picture_t*)luaL_checkudata( L, 1, "picture" );
  gwlua_ref_create( L, 1, &state->bg_ref );
  
  state->screen.state = state;
  state->screen.width = bg->width;
  state->screen.height = bg->height;
  
  size_t size = bg->width * bg->height * sizeof( uint16_t );
  
  state->screen.pixels = (uint16_t*)gwlua_malloc( size );
  
  if ( !state->screen.pixels )
  {
    return luaL_error( L, "out of memory" );
  }
  
  if ( lua_isnoneornil( L, 2 ) )
  {
    memcpy( (void*)state->screen.pixels, (void*)bg->pixels, size );
  }
  else
  {
    uint16_t* src = bg->pixels;
    const uint16_t* end = src + size / sizeof( uint16_t );
    uint16_t* dest = state->screen.pixels;
    uint32_t color = (uint32_t)luaL_checkinteger( L, 2 );
    uint16_t transp = ( ( color & 0xf8 ) << 8 ) | ( ( color & 0xf800 ) >> 5 ) | ( ( color & 0xf80000 ) >> 19 ) | ( ( ( color & 0xff000000U ) != 0 ) << 5 );
    
    while ( src < end )
    {
      *dest++ = *src == transp ? 0 : *src;
      src++;
    }
  }
  
  state->bg = bg;
  gwlua_set_fb( &state->screen );
  return 0;
}

static const char* button_name( int button )
{
  switch ( button )
  {
  case GWLUA_UP:     return "up";
  case GWLUA_DOWN:   return "down";
  case GWLUA_LEFT:   return "left";
  case GWLUA_RIGHT:  return "right";
  case GWLUA_A:      return "a";
  case GWLUA_B:      return "b";
  case GWLUA_X:      return "x";
  case GWLUA_Y:      return "y";
  case GWLUA_L1:     return "l1";
  case GWLUA_R1:     return "r1";
  case GWLUA_L2:     return "l2";
  case GWLUA_R2:     return "r2";
  case GWLUA_L3:     return "l3";
  case GWLUA_R3:     return "r3";
  case GWLUA_SELECT: return "select";
  case GWLUA_START:  return "start";
  default:           return "?";
  }
}

static int l_inputstate( lua_State* L )
{
  gwlua_t* state = get_state( L );
  int i;
  
  if ( lua_type( L, 1 ) == LUA_TTABLE )
  {
    lua_pushvalue( L, 1 );
  }
  else
  {
    lua_createtable( L, 0, 17 );
  }
  
  for ( i = 1; i < sizeof( state->input ) / sizeof( state->input[ 0 ] ); i++ )
  {
    lua_pushboolean( L, state->input[ i ] );
    lua_setfield( L, -2, button_name( i ) );
  }
  
  return 1;
}

static int l_loadbin( lua_State* L )
{
  gwlua_t* state = get_state( L );
  const char* name = luaL_checkstring( L, 1 );
  gwrom_entry_t entry;
  uint32_t hash = gwlua_djb2( name );

  switch ( hash )
  {
  case 0xe95068abU: // class.lua
    entry.data = (void*)gwlua_lua_class_lua;
    entry.size = gwlua_lua_class_lua_len;
    break;
  case 0xffb0f4e3U: // classes.lua
    entry.data = (void*)gwlua_lua_classes_lua;
    entry.size = gwlua_lua_classes_lua_len;
    break;
  case 0x38da66e9U: // controls.lua
    entry.data = (void*)gwlua_lua_controls_lua;
    entry.size = gwlua_lua_controls_lua_len;
    break;
  case 0xd3e282d8U: // dialogs.lua
    entry.data = (void*)gwlua_lua_dialogs_lua;
    entry.size = gwlua_lua_dialogs_lua_len;
    break;
  case 0x6671c6aeU: // extctrls.lua
    entry.data = (void*)gwlua_lua_extctrls_lua;
    entry.size = gwlua_lua_extctrls_lua_len;
    break;
  case 0x6e933e5bU: // fmod.lua
    entry.data = (void*)gwlua_lua_fmod_lua;
    entry.size = gwlua_lua_fmod_lua_len;
    break;
  case 0x609badf0U: // fmodtypes.lua
    entry.data = (void*)gwlua_lua_fmodtypes_lua;
    entry.size = gwlua_lua_fmodtypes_lua_len;
    break;
  case 0x1a2a4f5cU: // forms.lua
    entry.data = (void*)gwlua_lua_forms_lua;
    entry.size = gwlua_lua_forms_lua_len;
    break;
  case 0x68d4ae86U: // graphics.lua
    entry.data = (void*)gwlua_lua_graphics_lua;
    entry.size = gwlua_lua_graphics_lua_len;
    break;
  case 0xef6bfb5bU: // jpeg.lua
    entry.data = (void*)gwlua_lua_jpeg_lua;
    entry.size = gwlua_lua_jpeg_lua_len;
    break;
  case 0x5497649fU: // math.lua
    entry.data = (void*)gwlua_lua_math_lua;
    entry.size = gwlua_lua_math_lua_len;
    break;
  case 0x63f0376dU: // messages.lua
    entry.data = (void*)gwlua_lua_messages_lua;
    entry.size = gwlua_lua_messages_lua_len;
    break;
  case 0x3f6b838eU: // registry.lua
    entry.data = (void*)gwlua_lua_registry_lua;
    entry.size = gwlua_lua_registry_lua_len;
    break;
  case 0xbda4fae8U: // stdctrls.lua
    entry.data = (void*)gwlua_lua_stdctrls_lua;
    entry.size = gwlua_lua_stdctrls_lua_len;
    break;
  case 0x1106d565U: // sysutils.lua
    entry.data = (void*)gwlua_lua_sysutils_lua;
    entry.size = gwlua_lua_sysutils_lua_len;
    break;
  case 0x675996c0U: // windows.lua
    entry.data = (void*)gwlua_lua_windows_lua;
    entry.size = gwlua_lua_windows_lua_len;
    break;
    
    
  default:
    if ( gwrom_find( &entry, state->rom, name ) != GWROM_OK )
    {
      return 0;
    }
    
    break;
  }
  
  lua_pushlstring( L, (char*)entry.data, entry.size );
  return 1;
}

static int l_bsread( lua_State* L )
{
  void* bs = lua_touserdata( L, lua_upvalueindex( 1 ) );
  const char* string;
  size_t size;
  
  string = bsread( L, bs, &size );
  
  if ( string )
  {
    lua_pushlstring( L, string, size );
    return 1;
  }
  
  free( bs );
  return 0;
}

static int l_loadbs( lua_State* L )
{
  gwlua_t* state = get_state( L );
  const char* name = luaL_checkstring( L, 1 );
  gwrom_entry_t entry;
  
  if ( gwrom_find( &entry, state->rom, name ) == GWROM_OK )
  {
    void* bs = bsnew( entry.data );
    
    if ( bs )
    {
      lua_pushlightuserdata( L, bs );
      lua_pushcclosure( L, l_bsread, 1 );
      return 1;
    }
  }
  
  return 0;
}

void register_functions( lua_State* L, gwlua_t* state )
{
  static const luaL_Reg statics[] =
  {
    { "playsound",     l_playsound },
    { "stopsounds",    l_stopsounds },
    { "randomize",     l_randomize },
    { "random",        l_random },
    { "round",         l_round },
    { "now",           l_now },
    { "splittime",     l_splittime },
    { "inttostr",      l_inttostr },
    { "loadvalue",     l_loadvalue },
    { "savevalue",     l_savevalue },
    { "setbackground", l_setbackground },
    { "inputstate",    l_inputstate },
    { "loadbin",       l_loadbin },
    { "loadbs",        l_loadbs },
    { NULL, NULL }
  };
  
  lua_pushlightuserdata( L, (void*)state );
  luaL_setfuncs( L, statics, 1 );
  
  // module
  
  if ( luaL_loadbufferx( L, (const char*)gwlua_lua_system_lua, gwlua_lua_system_lua_len, "system.lua", "t" ) != LUA_OK )
  {
    lua_error( L );
    return;
  }
  
  // module chunk
  
  lua_call( L, 0, 1 );
  
  // module function
  
  lua_pushvalue( L, -2 );
  
  // module function module
  
  lua_call( L, 1, 0 );
  
  // module
}
