#include <unistd.h>

#include <iostream>

#include "Poco/Thread.h"
#include "Poco/Runnable.h"

#include "HNManagedDeviceArbiter.h"

// Helper class for running HNManagedDeviceArbiter 
// monitoring loop as an independent thread
class HNMDARunner : public Poco::Runnable
{
    private:
        Poco::Thread   thread;
        HNManagedDeviceArbiter *abObj;

    public:  
        HNMDARunner( HNManagedDeviceArbiter *value )
        {
            abObj = value;
        }

        void startThread()
        {
            thread.start( *this );
        }

        void killThread()
        {
            abObj->killMonitoringLoop();
            thread.join();
        }

        virtual void run()
        {
            abObj->runMonitoringLoop();
        }

};

HNMDARecord::HNMDARecord()
{

}

HNMDARecord::~HNMDARecord()
{

}

void 
HNMDARecord::setDiscoveryState( HNMDR_DISC_STATE_T value )
{
    discoveryState = value;
}

void 
HNMDARecord::setOwnershipState( HNMDR_OWNER_STATE_T value )
{
    ownershipState = value;
}

HNMDR_DISC_STATE_T  
HNMDARecord::getDiscoveryState()
{
    return discoveryState;
}

std::string
HNMDARecord::getDiscoveryStateStr()
{
    if( HNMDR_DISC_STATE_NEW == discoveryState )
        return "HNMDR_DISC_STATE_NEW";
    else if( HNMDR_DISC_STATE_NOTSET == discoveryState )
        return "HNMDR_DISC_STATE_NOTSET";

    return "ERROR";
}

HNMDR_OWNER_STATE_T 
HNMDARecord::getOwnershipState()
{
    return ownershipState;
}

std::string
HNMDARecord::getOwnershipStateStr()
{
    if( HNMDR_OWNER_STATE_UNKNOWN == ownershipState )
        return "HNMDR_OWNER_STATE_UNKNOWN";
    else if( HNMDR_OWNER_STATE_NOTSET == ownershipState )
        return "HNMDR_OWNER_STATE_NOTSET";

    return "ERROR";
}

void 
HNMDARecord::setDiscoveryID( std::string value )
{
    discID = value;
}

void 
HNMDARecord::setDeviceType( std::string value )
{
    devType = value;
}

void 
HNMDARecord::setDeviceVersion( std::string value )
{
    devVersion = value;
}

void 
HNMDARecord::setHNodeIDFromStr( std::string value )
{
    hnodeID.setFromStr( value );
}

void 
HNMDARecord::setName( std::string value )
{
    name = value;
}

void 
HNMDARecord::setBaseIPv4URL( std::string value )
{
    baseIPv4URL = value;
}

void 
HNMDARecord::setBaseIPv6URL( std::string value )
{
    baseIPv6URL = value;
}

void
HNMDARecord::setBaseSelfURL( std::string value )
{
    baseSelfURL = value;
}

std::string 
HNMDARecord::getDiscoveryID()
{
    return discID;
}

std::string 
HNMDARecord::getDeviceType()
{
    return devType;
}

std::string 
HNMDARecord::getDeviceVersion()
{
    return devVersion;
}

std::string 
HNMDARecord::getHNodeIDStr()
{
    std::string rtnStr;
    hnodeID.getStr( rtnStr );
    return rtnStr;
}

std::string 
HNMDARecord::getName()
{
    return name;
}

std::string 
HNMDARecord::getBaseIPv4URL()
{
    return baseIPv4URL;
}

std::string 
HNMDARecord::getBaseIPv6URL()
{
    return baseIPv6URL;
}

std::string
HNMDARecord::getBaseSelfURL()
{
    return baseSelfURL;
}

std::string 
HNMDARecord::getCRC32ID()
{
    return hnodeID.getCRC32AsHexStr();
}

void 
HNMDARecord::debugPrint( uint offset )
{
    printf( "%*.*s%s  %s  %s\n", offset, offset, " ", hnodeID.getCRC32AsHexStr().c_str(), getDiscoveryStateStr().c_str(), getOwnershipStateStr().c_str() );
 
    offset += 2;
    printf( "%*.*sname: %s\n", offset, offset, " ", getName().c_str() );
    printf( "%*.*shnodeID: %s\n", offset, offset, " ", getHNodeIDStr().c_str() );
    printf( "%*.*sdeviceType: %s (version: %s)\n", offset, offset, " ", getDeviceType().c_str(), getDeviceVersion().c_str() );
    printf( "%*.*sdiscID: %s\n", offset, offset, " ", getDiscoveryID().c_str() );
 
#if 0
        std::string discID;
        HNodeID     hnodeID;
        std::string devType;
        std::string devVersion;
        std::string name;

        std::string baseIPv4URL;
        std::string baseIPv6URL;
        std::string baseSelfURL;
#endif
}

HNManagedDeviceArbiter::HNManagedDeviceArbiter()
{
    runMonitor = false;
    thelp = NULL;
}

HNManagedDeviceArbiter::~HNManagedDeviceArbiter()
{

}

HNMDL_RESULT_T 
HNManagedDeviceArbiter::notifyDiscoverAdd( HNMDARecord &record )
{
    // Check if the record is existing, or if this is a new discovery.
    std::map< std::string, HNMDARecord >::iterator it = mdrMap.find( record.getCRC32ID() );

    if( it == mdrMap.end() )
    {
        // This is a new record
        record.setDiscoveryState( HNMDR_DISC_STATE_NEW );
        record.setOwnershipState( HNMDR_OWNER_STATE_UNKNOWN );

        mdrMap.insert( std::pair< std::string, HNMDARecord >( record.getCRC32ID(), record ) );
    }
    else
    {
        // This is an existing record
        // Check if updates are appropriate.

    }

    return HNMDL_RESULT_SUCCESS;
}

HNMDL_RESULT_T 
HNManagedDeviceArbiter::notifyDiscoverRemove( HNMDARecord &record )
{

    return HNMDL_RESULT_SUCCESS;
}

void 
HNManagedDeviceArbiter::debugPrint()
{
    printf( "=== Managed Device Arbiter ===\n" );

    for( std::map< std::string, HNMDARecord >::iterator it = mdrMap.begin(); it != mdrMap.end(); it++ )
    {
        it->second.debugPrint( 2 );
    }
}



void
HNManagedDeviceArbiter::start()
{
    int error;

    std::cout << "HNManagedDeviceArbiter::start()" << std::endl;

    // Allocate the thread helper
    thelp = new HNMDARunner( this );
    if( !thelp )
    {
        //cleanup();
        return;
    }

    runMonitor = true;

    // Start up the event loop
    ( (HNMDARunner*) thelp )->startThread();
}

void 
HNManagedDeviceArbiter::runMonitoringLoop()
{
    std::cout << "HNManagedDeviceArbiter::runMonitoringLoop()" << std::endl;

    // Run the main loop
    while( runMonitor == true )
    {
        sleep( 10 );
        std::cout << "HNManagedDeviceArbiter::monitor wakeup" << std::endl;
    }

    std::cout << "HNManagedDeviceArbiter::monitor exit" << std::endl;
}

void
HNManagedDeviceArbiter::shutdown()
{
    if( !thelp )
    {
        //cleanup();
        return;
    }

    // End the event loop
    ( (HNMDARunner*) thelp )->killThread();

    delete ( (HNMDARunner*) thelp );
    thelp = NULL;
}

void 
HNManagedDeviceArbiter::killMonitoringLoop()
{
    runMonitor = false;    
}



