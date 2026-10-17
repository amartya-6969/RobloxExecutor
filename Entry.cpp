/* With Love By Dyn <3 */

#include "utils/FileWatcher.h"
#include "handler/TPHandler.h"
#include "core/Scheduler/Scheduler.h"
#include "client/Environment/Libraries/Raknet/Raknet.h"
#include <Windows.h>
#include <thread>

BOOL APIENTRY
DllMain( HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved )
{
	if ( ul_reason_for_call == DLL_PROCESS_ATTACH )
	{
		std::thread( [ ]
		{
		    Raknet::Bootstrap( ); /* pending support to be not automatic */
			TPHandler::Initialize( );
		} ).detach( );
		std::thread( FileWatcher::Initialize ).detach( );
	}

	return TRUE;
}
