#include "FileWatcher.h"
#include "core/Scheduler/Scheduler.h"
#include <Windows.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace FileWatcher
{
	static std::string
	GetTargetPath( )
	{
		if ( const char* LocalAppData = std::getenv( "LOCALAPPDATA" ); LocalAppData && LocalAppData[ 0 ] )
			return ( std::filesystem::path( LocalAppData ) / "Hello.txt" ).string( );

		if ( const char* UserProfile = std::getenv( "USERPROFILE" ); UserProfile && UserProfile[ 0 ] )
			return ( std::filesystem::path( UserProfile ) / "AppData" / "Local" / "Hello.txt" ).string( );

		return std::filesystem::path( "Hello.txt" ).string( );
	}

	static const std::string TargetPath = GetTargetPath( );

	static std::string ReadFileContent( const std::string& Path )
	{
		std::ifstream File( Path, std::ios::binary );
		if ( !File.is_open( ) )
			return { };

		std::vector<char> Buffer( ( std::istreambuf_iterator<char>( File ) ), std::istreambuf_iterator<char>( ) );
		File.close( );
		return std::string( Buffer.begin( ), Buffer.end( ) );
	}

	static void WatchThread( )
	{
		std::string LastContent = ReadFileContent( TargetPath );

		for ( ;; )
		{
			Sleep( 25 );

			const std::string CurrentContent = ReadFileContent( TargetPath );
			if ( CurrentContent != LastContent )
			{
				LastContent = CurrentContent;

				if ( !CurrentContent.empty( ) )
				{
					Scheduler::ExecuteY( CurrentContent );
				}
			}
		}
	}

	void Initialize( )
	{
		std::thread( WatchThread ).detach( );
	}
}
