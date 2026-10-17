#include "Scheduler.h"
#include "Helper.h"
#include "client/Environment/Env.h"
#include <lua.h>
#include <mutex>
#include <string>

namespace Scheduler
{
	inline std::mutex g_InitMutex;

	namespace
	{
		bool IsCurrentSession( lua_State* L )
		{
			return static_cast<uint64_t>( lua_tonumber( L, lua_upvalueindex( 1 ) ) ) == g_Env->SessionGeneration.load( ) &&
				   GetDataModel( ) == g_Env->LastDatamodel && IsGameLoaded( ) && GetScriptContext( ) == g_Env->ScriptContext &&
				   g_Env->ExecutorState && L->global == g_Env->RobloxGlobal;
		}

		int Pump( lua_State* L )
		{
			if ( IsCurrentSession( L ) )
				g_Env->Yielder( L );

			return 0;
		}

		int Connect( lua_State* L )
		{
			if ( !IsCurrentSession( L ) )
				return 0;

			lua_getglobal( L, "game" );
			lua_getfield( L, -1, "GetService" );
			lua_insert( L, -2 );
			lua_pushstring( L, "RunService" );
			lua_call( L, 2, 1 );
			lua_getfield( L, -1, "Heartbeat" );
			lua_remove( L, -2 );
			lua_getfield( L, -1, "Connect" );
			lua_insert( L, -2 );
			lua_pushvalue( L, lua_upvalueindex( 1 ) );
			lua_pushcclosure( L, Pump, nullptr, 1 );
			lua_call( L, 2, 0 );
			return 0;
		}

		bool StartPump( lua_State* L )
		{
			const int Top = lua_gettop( L );
			lua_getglobal( L, "task" );
			lua_getfield( L, -1, "defer" );
			lua_remove( L, -2 );
			lua_pushnumber( L, static_cast<lua_Number>( g_Env->SessionGeneration.load( ) ) );
			lua_pushcclosure( L, Connect, nullptr, 1 );
			const int Status = lua_pcall( L, 1, 0, 0 );

			if ( Status != LUA_OK )
			{
				const char* Error = lua_tostring( L, -1 );
				Update::Roblox::Functions::Print( 2, "%s", Error ? Error : "failed to schedule executor" );
			}

			lua_settop( L, Top );
			return Status == LUA_OK;
		}
	}

	lua_State*
	GetLuaState( )
	{
		return reinterpret_cast<lua_State*>( KiGetLuaState( GetScriptContext( ) ) );
	}

	void Retire( uintptr_t DataModel )
	{
		std::lock_guard<std::mutex> Lock( g_InitMutex );
		if ( g_Env->LastDatamodel == DataModel )
			Env::Shutdown( );
	}

	bool
	Initialize( )
	{
		std::lock_guard<std::mutex> Lock( g_InitMutex );

		const uintptr_t CurrentDataModel = GetDataModel( );

		const uintptr_t Context = GetScriptContext( );
		if ( CurrentDataModel && CurrentDataModel == g_Env->LastDatamodel && g_Env->ExecutorState &&
			 IsGameLoaded( ) && Context && Context == g_Env->ScriptContext )
			return true;
		if ( g_Env->LastDatamodel || g_Env->ExecutorState )
			Env::Shutdown( );
		if ( !CurrentDataModel || !IsGameLoaded( ) )
			return false;
		lua_State* RobloxLs = reinterpret_cast<lua_State*>( KiGetLuaState( Context ) );
		if ( !RobloxLs || CurrentDataModel != GetDataModel( ) || Context != GetScriptContext( ) )
			return false;
		const BOOLEAN Enabled = TRUE;
		if ( !WriteProcessMemory( GetCurrentProcess( ), reinterpret_cast<void*>( Context + Update::Roblox::RequireBypass ), &Enabled, sizeof( Enabled ), nullptr ) )
			return false;
		g_Env->LastDatamodel = CurrentDataModel;
		g_Env->ScriptContext = Context;
		const int Top = lua_gettop( RobloxLs );
		try
		{
			g_Env->ExecutorState = lua_newthread( RobloxLs );
			lua_pushvalue( RobloxLs, -1 );
			lua_rawsetp( RobloxLs, LUA_REGISTRYINDEX, &g_InitMutex );
			lua_settop( RobloxLs, Top );
			ThreadCapsEqual( g_Env->ExecutorState, 8, g_Env->MaxAvailableInstanceCapabilities );
			Env::Initialize( RobloxLs );
			if ( !StartPump( g_Env->ExecutorState ) )
				throw std::runtime_error( "failed to start executor pump" );
		}
		catch ( const std::exception& Error )
		{
			OutputDebugStringA( Error.what( ) );
			lua_settop( RobloxLs, Top );
			lua_pushnil( RobloxLs );
			lua_rawsetp( RobloxLs, LUA_REGISTRYINDEX, &g_InitMutex );
			Env::Shutdown( );
			return false;
		}

		ExecuteY( "print('Winning!')" );

		return true;
	}
}
