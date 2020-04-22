#include "HNManagedDeviceArbiter.h"

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

HNMDR_OWNER_STATE_T 
HNMDARecord::getOwnershipState()
{
    return ownershipState;
}

std::string 
HNMDARecord::getCRC32ID()
{
    return hnodeID.getCRC32AsHexStr();
}

HNManagedDeviceArbiter::HNManagedDeviceArbiter()
{

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


}


