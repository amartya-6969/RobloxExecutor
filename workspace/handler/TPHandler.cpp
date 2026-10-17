#include "TPHandler.h"
#include "core/Scheduler/Scheduler.h"
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <vector>

namespace TPHandler
{
	namespace
	{
		using JobStep = uintptr_t( __fastcall* )( uintptr_t Job, uintptr_t Stats );
		constexpr size_t StepSlot = 8;
		constexpr size_t MethodCount = StepSlot + 1;

		struct JobHook
		{
			uintptr_t DataModel = 0;
			uintptr_t Original = 0;
			uintptr_t Type = 0;
			std::array<uintptr_t, MethodCount> Methods{ };
		};

		static_assert( offsetof( JobHook, Methods ) == offsetof( JobHook, Type ) + sizeof( uintptr_t ) );
		std::atomic<uintptr_t> g_ActiveJob{ 0 };

		uintptr_t __fastcall StepHook( uintptr_t Job, uintptr_t Stats )
		{
			const uintptr_t Table = *reinterpret_cast<const uintptr_t*>( Job );
			const auto* Hook = reinterpret_cast<const JobHook*>( Table - offsetof( JobHook, Methods ) );
			const auto Original = reinterpret_cast<JobStep>( Hook->Original );
			if ( g_ActiveJob.load( std::memory_order_acquire ) == Job )
			{
				if ( GetDataModel( ) != Hook->DataModel || !IsGameLoaded( ) )
					Scheduler::Retire( Hook->DataModel );
				else
					Scheduler::Initialize( );
			}
			return Original( Job, Stats );
		}

		bool Install( uintptr_t Job, uintptr_t Original, uintptr_t Replacement )
		{
			__try
			{
				return InterlockedCompareExchangePointer( reinterpret_cast<void* volatile*>( Job ), reinterpret_cast<void*>( Replacement ), reinterpret_cast<void*>( Original ) ) == reinterpret_cast<void*>( Original );
			}
			__except ( EXCEPTION_EXECUTE_HANDLER )
			{
				return false;
			}
		}

		uintptr_t FindJob( )
		{
			constexpr char Prefix[] = "WaitingHybridScriptsJob";
			const uintptr_t Context = GetScriptContext( );
			if ( !Context )
				return 0;
			uintptr_t Scheduler = 0;
			uintptr_t Begin = 0;
			uintptr_t End = 0;
			if ( !Scheduler::Helpers::Read( Update::Roblox::TaskScheduler::Pointer, Scheduler ) || !Scheduler ||
				 !Scheduler::Helpers::Read( Scheduler + Update::Roblox::TaskScheduler::JobsStart, Begin ) ||
				 !Scheduler::Helpers::Read( Scheduler + Update::Roblox::TaskScheduler::JobsEnd, End ) ||
				 !Begin || End <= Begin || End - Begin > 65536 || ( End - Begin ) % ( 2 * sizeof( uintptr_t ) ) )
				return 0;
			for ( uintptr_t Cursor = Begin; Cursor < End; Cursor += 2 * sizeof( uintptr_t ) )
			{
				uintptr_t Job = 0;
				uintptr_t Name = 0;
				uintptr_t Owner = 0;
				char Buffer[ sizeof( Prefix ) ]{ };
				if ( Scheduler::Helpers::Read( Cursor, Job ) && Job &&
					 Scheduler::Helpers::Read( Job + Update::Roblox::TaskScheduler::JobTypeName, Name ) &&
					 Scheduler::Helpers::Read( Name, Buffer ) && !std::memcmp( Buffer, Prefix, sizeof( Prefix ) - 1 ) &&
					 Scheduler::Helpers::Read( Job + Update::Roblox::TaskScheduler::ScriptContext, Owner ) && Owner == Context )
					return Job;
			}
			return 0;
		}
	}

	void Initialize( )
	{
		std::vector<std::unique_ptr<JobHook>> Hooks;
		for ( ;; )
		{
			Sleep( 25 );
			const uintptr_t DataModel = GetDataModel( );
			if ( !DataModel || !IsGameLoaded( ) )
				continue;
			const uintptr_t Job = FindJob( );
			uintptr_t Table = 0;
			if ( !Job || !Scheduler::Helpers::Read( Job, Table ) || !Table )
				continue;
			bool Installed = false;
			for ( const auto& Hook : Hooks )
			{
				if ( Table == reinterpret_cast<uintptr_t>( Hook->Methods.data( ) ) )
				{
					g_ActiveJob.store( Job, std::memory_order_release );
					Installed = true;
					break;
				}
			}
			if ( Installed )
				continue;
			auto Hook = std::make_unique<JobHook>( );
			if ( !Scheduler::Helpers::Read( Table - sizeof( uintptr_t ), Hook->Type ) || !Scheduler::Helpers::Read( Table, Hook->Methods ) )
				continue;
			Hook->DataModel = DataModel;
			Hook->Original = Hook->Methods[ StepSlot ];
			Hook->Methods[ StepSlot ] = reinterpret_cast<uintptr_t>( &StepHook );
			if ( !Hook->Original || DataModel != GetDataModel( ) || Job != FindJob( ) )
				continue;
			const uintptr_t Replacement = reinterpret_cast<uintptr_t>( Hook->Methods.data( ) );
			Hooks.push_back( std::move( Hook ) );
			if ( Install( Job, Table, Replacement ) )
				g_ActiveJob.store( Job, std::memory_order_release );
			else
				Hooks.pop_back( );
		}
	}
}
