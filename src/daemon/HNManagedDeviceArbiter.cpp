#include <unistd.h>

#include <iostream>
#include <regex>

#include <Poco/Thread.h>
#include <Poco/Runnable.h>
#include <Poco/StreamCopier.h>
#include <Poco/String.h>
#include <Poco/StringTokenizer.h>

#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <Poco/Net/IPAddress.h>
#include <Poco/Net/NetException.h>
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/URI.h>

#include "HNManagedDeviceArbiter.h"

namespace pjs = Poco::JSON;
namespace pdy = Poco::Dynamic;
namespace pns = Poco::Net;

HNMDMgmtCmd::HNMDMgmtCmd()
{
    m_execState = HNMDC_EXEC_STATE_IDLE;
    m_cmdType   = HNMDC_CMDTYPE_NOTSET;
    m_fieldMask = HNMDC_FLDMASK_NONE;
}

HNMDMgmtCmd::~HNMDMgmtCmd()
{

}

void
HNMDMgmtCmd::clear()
{
    m_execState = HNMDC_EXEC_STATE_IDLE;
    m_cmdType   = HNMDC_CMDTYPE_NOTSET;
    m_fieldMask = HNMDC_FLDMASK_NONE;
}

HNMDC_CMDTYPE_T
HNMDMgmtCmd::getType()
{
    return m_cmdType;
}

std::string
HNMDMgmtCmd::getTypeAsStr()
{
    if( HNMDC_CMDTYPE_CLAIMDEV == m_cmdType )
        return "claim";
    else if( HNMDC_CMDTYPE_RELEASEDEV == m_cmdType )
        return "release";
    else if( HNMDC_CMDTYPE_SETDEVPARAMS == m_cmdType )
        return "set_device_parameters";
    else if( HNMDC_CMDTYPE_NOTSET == m_cmdType )
        return "notset";

    return "unknown";
}

bool
HNMDMgmtCmd::setTypeFromStr( std::string value )
{
    if( "claim" == value )
    {
        m_cmdType = HNMDC_CMDTYPE_CLAIMDEV;
        return false;
    }
    else if( "release" == value )
    {
        m_cmdType = HNMDC_CMDTYPE_RELEASEDEV;
        return false;
    }
    else if( "set_device_parameters" == value )
    {
        m_cmdType = HNMDC_CMDTYPE_SETDEVPARAMS;
        return false;
    }

    m_cmdType = HNMDC_CMDTYPE_NOTSET;
    return true;
}

void
HNMDMgmtCmd::setName( std::string value )
{
    m_name = value;
    m_fieldMask |= HNMDC_FLDMASK_NAME;
}

uint
HNMDMgmtCmd::getFieldMask()
{
    return m_fieldMask;
}

HNMDL_RESULT_T
HNMDMgmtCmd::setFromJSON( std::istream *bodyStream )
{
    // Clear things to start
    clear();

    // Parse the json body of the request
    try
    {
        // Attempt to parse the json    
        pjs::Parser parser;
        pdy::Var varRoot = parser.parse( *bodyStream );

        // Get a pointer to the root object
        pjs::Object::Ptr jsRoot = varRoot.extract< pjs::Object::Ptr >();

        // All requests must include a command field
        if( jsRoot->has( "command" ) == false )
            return HNMDL_RESULT_FAILURE;

        // Set the command type
        if( setTypeFromStr( jsRoot->getValue<std::string>( "command" ) ) == true )
        {
            return HNMDL_RESULT_FAILURE;
        }

        if( jsRoot->has( "name" ) )
        {
            setName( jsRoot->getValue<std::string>( "name" ) );
        }
    }
    catch( Poco::Exception ex )
    {
        std::cout << "HNMDMgmtCmd::setFromJSON exception: " << ex.displayText() << std::endl;
        // Request body was not understood
        return HNMDL_RESULT_FAILURE;
    }

    // Done
    return HNMDL_RESULT_SUCCESS;
}

HNMDARAddress::HNMDARAddress()
{
    m_type = HMDAR_ADDRTYPE_NOTSET;
}

HNMDARAddress::~HNMDARAddress()
{

}

void 
HNMDARAddress::setAddressInfo( std::string dnsName, std::string address, uint16_t port )
{
    m_dnsName = dnsName;
    m_address = address;
    m_port = port;

    // Use Poco to do IPAddress validation, etc.
    try 
    {
        pns::IPAddress trialAddr( address );

        switch( trialAddr.family() )
        {
            case pns::AddressFamily::IPv4:
                if( trialAddr.isLoopback() )
                {
                    m_type = HNDAR_ADDRTYPE_LOOPBACK_IPV4;
                    return;
                }
                else if( trialAddr.isMulticast() || trialAddr.isBroadcast() )
                {
                    m_type = HNDAR_ADDRTYPE_CAST_IPV4;
                    return;
                }
                else if( trialAddr.isLinkLocal() || trialAddr.isSiteLocal() )
                {
                    m_type = HMDAR_ADDRTYPE_IPV4;
                    return;
                }
                else
                {
                    m_type = HNDAR_ADDRTYPE_INET_IPV4;
                    return;
                }
            break;

            case pns::AddressFamily::IPv6:
                if( trialAddr.isLoopback() )
                {
                    m_type = HNDAR_ADDRTYPE_LOOPBACK_IPV6;
                    return;
                }
                else if( trialAddr.isMulticast() || trialAddr.isBroadcast() )
                {
                    m_type = HNDAR_ADDRTYPE_CAST_IPV6;
                    return;
                }
                else if( trialAddr.isLinkLocal() || trialAddr.isSiteLocal() )
                {
                    m_type = HMDAR_ADDRTYPE_IPV6;
                    return;
                }
                else
                {
                    m_type = HNDAR_ADDRTYPE_INET_IPV6;
                    return;
                }
            break;
        }
    } 
    catch( pns::InvalidAddressException ex )
    {
    }

    // Could not parse the address string
    m_type = HMDAR_ADDRTYPE_UNKNOWN;
    return;
}

HMDAR_ADDRTYPE_T
HNMDARAddress::getType()
{
    return m_type;
}

const char* gHNMDARAddressTypeStrings[] =
{
   "not-set",       // HMDAR_ADDRTYPE_NOTSET,
   "unknown",       // HMDAR_ADDRTYPE_UNKNOWN,
   "ipv4",          // HMDAR_ADDRTYPE_IPV4,
   "loopback_ipv4", // HNDAR_ADDRTYPE_LOOPBACK_IPV4,
   "cast_ipv4",     // HNDAR_ADDRTYPE_CAST_IPV4,
   "inet_ipv4",     // HNDAR_ADDRTYPE_INET_IPV4,
   "ipv6",          // HMDAR_ADDRTYPE_IPV6,
   "loopback_ipv6", // HNDAR_ADDRTYPE_LOOPBACK_IPV6,
   "cast_ipv6",     // HNDAR_ADDRTYPE_CAST_IPV6,
   "inet_ipv6"      // HNDAR_ADDRTYPE_INET_IPV6 
};

std::string
HNMDARAddress::getTypeAsStr()
{
    return gHNMDARAddressTypeStrings[ m_type ];
}

std::string
HNMDARAddress::getDNSName()
{
    return m_dnsName;
}

std::string
HNMDARAddress::getAddress()
{
    return m_address;
}

uint16_t
HNMDARAddress::getPort()
{
    return m_port;
}

std::string
HNMDARAddress::getURL( std::string protocol, std::string path )
{
    std::string url;

    return url;
}

void 
HNMDARAddress::debugPrint( uint offset )
{
    offset += 2;
    printf( "%*.*stype: %s  address: %s  port: %u  dnsName: %s\n", offset, offset, " ", getTypeAsStr().c_str(), m_address.c_str(), m_port, m_dnsName.c_str() );
}

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
    m_mgmtState = HNMDR_MGMT_STATE_NOTSET;
    m_ownerState = HNMDR_OWNER_STATE_NOTSET;

    m_deviceMutex = NULL;
}

HNMDARecord::HNMDARecord( const HNMDARecord &srcObj )
{
    m_mgmtState   = srcObj.m_mgmtState;
    m_ownerState  = srcObj.m_ownerState;

    discID       = srcObj.discID;
    hnodeID      = srcObj.hnodeID;
    devType      = srcObj.devType;
    devVersion   = srcObj.devVersion;
    name         = srcObj.name;

    m_addrList   = srcObj.m_addrList;

    m_mgmtCmd    = srcObj.m_mgmtCmd;

    // Do not copy over a mutex as they are unique
    // to each object.
    m_deviceMutex = NULL;
}

HNMDARecord::~HNMDARecord()
{
    if( m_deviceMutex )
    {
        delete m_deviceMutex;
        m_deviceMutex = NULL;
    }
}

void
HNMDARecord::lockForUpdate()
{
    if( m_deviceMutex == NULL )
        m_deviceMutex = new std::mutex();

    m_deviceMutex->lock();
}

void
HNMDARecord::unlockForUpdate()
{
    if( m_deviceMutex )
        m_deviceMutex->unlock();
}

void 
HNMDARecord::setManagementState( HNMDR_MGMT_STATE_T value )
{
    m_mgmtState = value;
}

void 
HNMDARecord::setOwnershipState( HNMDR_OWNER_STATE_T value )
{
    m_ownerState = value;
}

HNMDR_MGMT_STATE_T  
HNMDARecord::getManagementState()
{
    return m_mgmtState;
}

std::string
HNMDARecord::getManagementStateStr()
{
    if( HNMDR_MGMT_STATE_ACTIVE == m_mgmtState )
        return "ACTIVE";
    else if( HNMDR_MGMT_STATE_SELF == m_mgmtState )
        return "SELF";
    else if( HNMDR_MGMT_STATE_DISCOVERED == m_mgmtState )
        return "DISCOVERED";
    else if( HNMDR_MGMT_STATE_RECOVERED == m_mgmtState )
        return "RECOVERED";
    else if( HNMDR_MGMT_STATE_OPT_INFO == m_mgmtState )
        return "OPT_INFO";
    else if( HNMDR_MGMT_STATE_OWNER_INFO == m_mgmtState )
        return "OWNER_INFO";
    else if( HNMDR_MGMT_STATE_UNCLAIMED == m_mgmtState )
        return "UNCLAIMED";
    else if( HNMDR_MGMT_STATE_OTHER_MGR == m_mgmtState )
        return "OTHER_MGR";
    else if( HNMDR_MGMT_STATE_DISAPPEARING == m_mgmtState )
        return "DISAPPEARING";
    else if( HNMDR_MGMT_STATE_OFFLINE == m_mgmtState )
        return "OFFLINE";
    else if( HNMDR_MGMT_STATE_EXEC_CMD == m_mgmtState )
        return "EXEC_CMD";
    else if( HNMDR_MGMT_STATE_NOT_AVAILABLE == m_mgmtState )
        return "NOT_AVAILABLE";
    else if( HNMDR_MGMT_STATE_NOTSET == m_mgmtState )
        return "NOTSET";

    return "ERROR";
}

HNMDR_OWNER_STATE_T 
HNMDARecord::getOwnershipState()
{
    return m_ownerState;
}

std::string
HNMDARecord::getOwnershipStateStr()
{
    if( HNMDR_OWNER_STATE_UNKNOWN == m_ownerState )
        return "UNKNOWN";
    else if( HNMDR_OWNER_STATE_MINE == m_ownerState )
        return "MINE";
    else if( HNMDR_OWNER_STATE_OTHER == m_ownerState )
        return "OTHER";
    else if( HNMDR_OWNER_STATE_AVAILABLE == m_ownerState )
        return "AVAILABLE";
    else if( HNMDR_OWNER_STATE_UNAVAILABLE == m_ownerState )
        return "UNAVAILABLE";
    else if( HNMDR_OWNER_STATE_NOTSET == m_ownerState )
        return "NOTSET";

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
HNMDARecord::addAddressInfo( std::string dnsName, std::string address, uint16_t port )
{
    for( std::vector< HNMDARAddress >::iterator it = m_addrList.begin(); it != m_addrList.end(); it++ )
    {
        // Check if we are updating and address we already know about.
        if( it->getAddress() == address )
        {
            it->setAddressInfo( dnsName, address, port );
            return;
        }
    }

    HNMDARAddress newAddr;
    newAddr.setAddressInfo( dnsName, address, port );
    m_addrList.push_back( newAddr );
}

void
HNMDARecord::setOwnerID( HNodeID &ownerID )
{
    std::string idStr;
    ownerID.getStr( idStr );
    m_ownerHNodeID.setFromStr( idStr );
}

void
HNMDARecord::clearOwnerID()
{
    m_ownerHNodeID.clear();
}

void 
HNMDARecord::getAddressList( std::vector< HNMDARAddress > &addrList )
{
    for( std::vector< HNMDARAddress >::iterator it = m_addrList.begin(); it != m_addrList.end(); it++ )
    {
        addrList.push_back( *it );
    }
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
HNMDARecord::getCRC32ID()
{
    return hnodeID.getCRC32AsHexStr();
}

HNMDL_RESULT_T 
HNMDARecord::findPreferredConnection( HMDAR_ADDRTYPE_T preferredType, HNMDARAddress &connInfo )
{
    for( std::vector< HNMDARAddress >::iterator it = m_addrList.begin(); it != m_addrList.end(); it++ )
    {
        if( it->getType() == preferredType )
        {
            connInfo.setAddressInfo( it->getDNSName(), it->getAddress(), it->getPort() );

            return HNMDL_RESULT_SUCCESS;
        }
    }

    return HNMDL_RESULT_FAILURE;
}

HNMDL_RESULT_T 
HNMDARecord::updateRecord( HNMDARecord &newRecord )
{   
    setDiscoveryID( newRecord.getDiscoveryID() );
    setDeviceType( newRecord.getDeviceType() );
    setDeviceVersion( newRecord.getDeviceVersion() );
    setHNodeIDFromStr( newRecord.getHNodeIDStr() );
    setName( newRecord.getName() );
 
    std::vector< HNMDARAddress > newAddrList;
    newRecord.getAddressList( newAddrList );
    for( std::vector< HNMDARAddress >::iterator it = newAddrList.begin(); it != newAddrList.end(); it++ )
    {
        addAddressInfo( it->getDNSName(), it->getAddress(), it->getPort() );
    }

    return HNMDL_RESULT_SUCCESS;
}

HNMDL_RESULT_T
HNMDARecord::setMgmtCmdFromJSON( std::istream *bodyStream )
{
    return m_mgmtCmd.setFromJSON( bodyStream );
}

HNMDMgmtCmd& 
HNMDARecord::getDeviceMgmtCmdRef()
{
    return m_mgmtCmd;
}

void 
HNMDARecord::debugPrint( uint offset )
{
    printf( "%*.*s%s  %s\n", offset, offset, " ", hnodeID.getCRC32AsHexStr().c_str(), getManagementStateStr().c_str() );
 
    offset += 2;
    printf( "%*.*sname: %s\n", offset, offset, " ", getName().c_str() );
    printf( "%*.*shnodeID: %s\n", offset, offset, " ", getHNodeIDStr().c_str() );
    printf( "%*.*sdeviceType: %s (version: %s)\n", offset, offset, " ", getDeviceType().c_str(), getDeviceVersion().c_str() );
    printf( "%*.*sdiscID: %s\n", offset, offset, " ", getDiscoveryID().c_str() );
 
    for( std::vector< HNMDARAddress >::iterator it = m_addrList.begin(); it != m_addrList.end(); it++ )
    {
        it->debugPrint(offset);
    }
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
    // Scope lock
    std::lock_guard<std::mutex> guard( m_mapMutex );

    // Check if the record is existing, or if this is a new discovery.
    std::map< std::string, HNMDARecord >::iterator it = mdrMap.find( record.getCRC32ID() );

    if( it == mdrMap.end() )
    {
        // This is a new record
        if( record.getCRC32ID() == m_selfHnodeID.getCRC32AsHexStr() )
            record.setManagementState( HNMDR_MGMT_STATE_SELF );
        else
            record.setManagementState( HNMDR_MGMT_STATE_DISCOVERED );

        mdrMap.insert( std::pair< std::string, HNMDARecord >( record.getCRC32ID(), record ) );
    }
    else
    {
        // Update the existing record with most recent information
        it->second.updateRecord( record );
    }

    return HNMDL_RESULT_SUCCESS;
}

HNMDL_RESULT_T 
HNManagedDeviceArbiter::notifyDiscoverRemove( HNMDARecord &record )
{
    // Scope lock
    std::lock_guard<std::mutex> guard( m_mapMutex );

    // Check if the record is existing, or if this is a new discovery.
    std::map< std::string, HNMDARecord >::iterator it = mdrMap.find( record.getCRC32ID() );

    if( it != mdrMap.end() )
    {
        // This is a new record
        it->second.setManagementState( HNMDR_MGMT_STATE_DISAPPEARING );
    }

    return HNMDL_RESULT_SUCCESS;
}

void 
HNManagedDeviceArbiter::setSelfInfo( std::string hnodeIDStr )
{
    m_selfHnodeID.setFromStr( hnodeIDStr );
}

std::string 
HNManagedDeviceArbiter::getSelfHNodeIDStr()
{
    std::string rtnStr;
    m_selfHnodeID.getStr( rtnStr );
    return rtnStr;
}

std::string 
HNManagedDeviceArbiter::getSelfCRC32ID()
{
    return m_selfHnodeID.getCRC32AsHexStr();
}

HNMDL_RESULT_T
HNManagedDeviceArbiter::getDeviceCopy( std::string crc32ID, HNMDARecord &device )
{
    // Scope lock
    std::lock_guard<std::mutex> guard( m_mapMutex );

    std::map< std::string, HNMDARecord >::iterator it = mdrMap.find( crc32ID );

    if( it == mdrMap.end() )
        return HNMDL_RESULT_FAILURE;

    device = it->second;

    return HNMDL_RESULT_SUCCESS;
}

void
HNManagedDeviceArbiter::getDeviceListCopy( std::vector< HNMDARecord > &deviceList )
{
    // Scope lock
    std::lock_guard<std::mutex> guard( m_mapMutex );

    for( std::map< std::string, HNMDARecord >::iterator it = mdrMap.begin(); it != mdrMap.end(); it++ )
    {    
        deviceList.push_back( it->second );
    }
}

HNMDL_RESULT_T 
HNManagedDeviceArbiter::lookupConnectionInfo( std::string crc32ID, HMDAR_ADDRTYPE_T preferredType, HNMDARAddress &connInfo )
{
    // Scope lock
    std::lock_guard<std::mutex> guard( m_mapMutex );

    // See if we have a record for the device
    std::map< std::string, HNMDARecord >::iterator it = mdrMap.find( crc32ID );

    if( it == mdrMap.end() )
        return HNMDL_RESULT_FAILURE;

    return it->second.findPreferredConnection( preferredType, connInfo );
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
HNManagedDeviceArbiter::setNextMonitorState( HNMDARecord &device, HNMDR_MGMT_STATE_T nextState, uint minValue )
{
    device.lockForUpdate();

    device.setManagementState( nextState );

    if( minValue < m_monitorWaitTime )
        m_monitorWaitTime = minValue;

    device.unlockForUpdate();
}

void 
HNManagedDeviceArbiter::runMonitoringLoop()
{
    std::cout << "HNManagedDeviceArbiter::runMonitoringLoop()" << std::endl;

    m_monitorWaitTime = 10;

    // Run the main loop
    while( runMonitor == true )
    {
        sleep( m_monitorWaitTime );
        m_monitorWaitTime = 10;

        std::cout << "HNManagedDeviceArbiter::monitor wakeup" << std::endl;

        // Walk through known devices and take any pending actions
        for( std::map< std::string, HNMDARecord >::iterator it = mdrMap.begin(); it != mdrMap.end(); it++ )
        {
            std::cout << "  Device - crc32: " << it->second.getCRC32ID() << "  type: " << it->second.getDeviceType() << "   state: " <<  it->second.getManagementStateStr() << "  ostate: " << it->second.getOwnershipStateStr() << std::endl;

            switch( it->second.getManagementState() )
            {
                // This record represents myself, the management node, just halt in this state
                case HNMDR_MGMT_STATE_SELF:
                    it->second.setOwnershipState( HNMDR_OWNER_STATE_MINE );
                    setNextMonitorState( it->second, HNMDR_MGMT_STATE_SELF, 10 );
                break;

                // Added via Avahi Discovery
                case HNMDR_MGMT_STATE_DISCOVERED:
                    it->second.setOwnershipState( HNMDR_OWNER_STATE_UNKNOWN );
                    setNextMonitorState( it->second, HNMDR_MGMT_STATE_OPT_INFO, 0 );
                break;

                // Added from local record of owned devices (from prior association )
                case HNMDR_MGMT_STATE_RECOVERED:
                    it->second.setOwnershipState( HNMDR_OWNER_STATE_MINE );
                    setNextMonitorState( it->second, HNMDR_MGMT_STATE_OPT_INFO, 0 );
                break;

                // REST read to aquire basic operating info
                case HNMDR_MGMT_STATE_OPT_INFO:
                    if( updateDeviceOperationalInfo( it->second ) != HNMDL_RESULT_SUCCESS )
                        setNextMonitorState( it->second, HNMDR_MGMT_STATE_OFFLINE, 10 );
                    else
                        setNextMonitorState( it->second, HNMDR_MGMT_STATE_OWNER_INFO, 0 );
                break;

                // REST read for current ownership
                case HNMDR_MGMT_STATE_OWNER_INFO:
                    if( updateDeviceOwnerInfo( it->second ) != HNMDL_RESULT_SUCCESS )
                    {
                        setNextMonitorState( it->second, HNMDR_MGMT_STATE_OFFLINE, 10 );
                        break;
                    }
                    
                    switch( it->second.getOwnershipState() )
                    {
                        case HNMDR_OWNER_STATE_MINE:
                            setNextMonitorState( it->second, HNMDR_MGMT_STATE_ACTIVE, 0 );
                        break;

                        case HNMDR_OWNER_STATE_OTHER:
                            setNextMonitorState( it->second, HNMDR_MGMT_STATE_OTHER_MGR, 0 );
                        break;

                        case HNMDR_OWNER_STATE_AVAILABLE:
                            setNextMonitorState( it->second, HNMDR_MGMT_STATE_UNCLAIMED, 0 );
                        break;

                        case HNMDR_OWNER_STATE_UNAVAILABLE:
                            setNextMonitorState( it->second, HNMDR_MGMT_STATE_NOT_AVAILABLE, 0 );
                        break;

                        case HNMDR_OWNER_STATE_NOTSET:
                        case HNMDR_OWNER_STATE_UNKNOWN:
                            setNextMonitorState( it->second, HNMDR_MGMT_STATE_OFFLINE, 10 );
                        break;
                    }
                break;

                // Device is waiting to be claimed 
                case HNMDR_MGMT_STATE_UNCLAIMED:
                break;

                // Device is not currently owned, but is not available for claiming
                case HNMDR_MGMT_STATE_NOT_AVAILABLE:
                break;

                // Device is currently owner by other manager
                case HNMDR_MGMT_STATE_OTHER_MGR:
                break;

                // Device is active, responding to period health checks
                case HNMDR_MGMT_STATE_ACTIVE:
                    setNextMonitorState( it->second, HNMDR_MGMT_STATE_ACTIVE, 10 );
                break;

                // Avahi notification that device is offline
                case HNMDR_MGMT_STATE_DISAPPEARING:
                break;

                // Recent attempts to contact device have been unsuccessful
                case HNMDR_MGMT_STATE_OFFLINE:
                break;

                // These should not occur in normal operation, something very wrong.
                case HNMDR_MGMT_STATE_NOTSET:
                default:
                break;

                // Perform the steps to execute a device command request
                case HNMDR_MGMT_STATE_EXEC_CMD:
                    if( executeDeviceMgmtCmd( it->second ) != HNMDL_RESULT_SUCCESS )
                        setNextMonitorState( it->second, HNMDR_MGMT_STATE_OFFLINE, 10 );
                    else
                        setNextMonitorState( it->second, HNMDR_MGMT_STATE_OWNER_INFO, 0 );
                break;
            }
        }
    }

    std::cout << "HNManagedDeviceArbiter::monitor exit" << std::endl;
}

HNMDL_RESULT_T 
HNManagedDeviceArbiter::setDeviceMgmtCmdFromJSON( std::string crc32ID, std::istream *bodyStream )
{
    // Scope lock
    std::lock_guard<std::mutex> guard( m_mapMutex );

    // Lookup the device
    std::map< std::string, HNMDARecord >::iterator it = mdrMap.find( crc32ID );

    if( it == mdrMap.end() )
    {
        return HNMDL_RESULT_FAILURE;
    }

    // Make sure a command isnt already running

    // Setup the new command
    return it->second.setMgmtCmdFromJSON( bodyStream );
}


HNMDL_RESULT_T
HNManagedDeviceArbiter::startDeviceMgmtCmd( std::string crc32ID )
{
    // Scope lock
    std::lock_guard<std::mutex> guard( m_mapMutex );

    // Lookup the device
    std::map< std::string, HNMDARecord >::iterator it = mdrMap.find( crc32ID );

    if( it == mdrMap.end() )
    {
        return HNMDL_RESULT_FAILURE;
    }

    // Validate the request

    // Start the requests
    setNextMonitorState( it->second, HNMDR_MGMT_STATE_EXEC_CMD, 0 );

    return HNMDL_RESULT_SUCCESS;
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

HNMDL_RESULT_T
HNManagedDeviceArbiter::executeDeviceMgmtCmd( HNMDARecord &device )
{
    switch( device.getDeviceMgmtCmdRef().getType() )
    {
        case HNMDC_CMDTYPE_CLAIMDEV:
            return sendDeviceClaimRequest( device );
        break;

        case HNMDC_CMDTYPE_RELEASEDEV:
            return sendDeviceReleaseRequest( device );
        break;

        case HNMDC_CMDTYPE_SETDEVPARAMS:
            return sendDeviceSetParameters( device );
        break;
    }

    return HNMDL_RESULT_FAILURE;
}

HNMDL_RESULT_T
HNManagedDeviceArbiter::updateDeviceOperationalInfo( HNMDARecord &device )
{
    Poco::URI uri;
    HNMDARAddress dcInfo;

    device.lockForUpdate();

    if( device.findPreferredConnection( HMDAR_ADDRTYPE_IPV4, dcInfo ) != HNMDL_RESULT_SUCCESS )
    {
        device.unlockForUpdate();
        return HNMDL_RESULT_FAILURE;
    }

    device.unlockForUpdate();

    uri.setScheme( "http" );
    uri.setHost( dcInfo.getAddress() );
    uri.setPort( dcInfo.getPort() );
    uri.setPath( "/hnode2/device/info" );

    pns::HTTPClientSession session( uri.getHost(), uri.getPort() );
    pns::HTTPRequest request( pns::HTTPRequest::HTTP_GET, uri.getPathAndQuery(), pns::HTTPMessage::HTTP_1_1 );
    pns::HTTPResponse response;

    session.sendRequest( request );
    std::istream& rs = session.receiveResponse( response );
    std::cout << response.getStatus() << " " << response.getReason() << " " << response.getContentLength() << std::endl;

    if( response.getStatus() != Poco::Net::HTTPResponse::HTTP_OK )
    {
        return HNMDL_RESULT_FAILURE;
    }

    device.lockForUpdate();

    std::string body;
    Poco::StreamCopier::copyToString( rs, body );
    std::cout << body << std::endl;

    device.unlockForUpdate();

    return HNMDL_RESULT_SUCCESS;
}

HNMDL_RESULT_T
HNManagedDeviceArbiter::updateDeviceOwnerInfo( HNMDARecord &device )
{
    Poco::URI uri;
    HNMDARAddress dcInfo;

    device.lockForUpdate();

    if( device.findPreferredConnection( HMDAR_ADDRTYPE_IPV4, dcInfo ) != HNMDL_RESULT_SUCCESS )
    {
        device.unlockForUpdate();
        return HNMDL_RESULT_FAILURE;
    }

    device.unlockForUpdate();

    uri.setScheme( "http" );
    uri.setHost( dcInfo.getAddress() );
    uri.setPort( dcInfo.getPort() );
    uri.setPath( "/hnode2/device/owner" );

    pns::HTTPClientSession session( uri.getHost(), uri.getPort() );
    pns::HTTPRequest request( pns::HTTPRequest::HTTP_GET, uri.getPathAndQuery(), pns::HTTPMessage::HTTP_1_1 );
    pns::HTTPResponse response;

    session.sendRequest( request );
    std::istream& rs = session.receiveResponse( response );
    std::cout << response.getStatus() << " " << response.getReason() << " " << response.getContentLength() << std::endl;

    if( response.getStatus() != Poco::Net::HTTPResponse::HTTP_OK )
    {
        return HNMDL_RESULT_FAILURE;
    }

    // {
    // "isAvailable" : true,
    // "isOwned" : true,
    // "owner_crc32ID" : "2c1b21da",
    // "owner_hnodeID" : "54:cb:b3:de:e4:7f:11:ec:84:ac:d0:50:99:9c:b1:04"
    // }
    // Parse the json body of the request
    try
    {
        device.lockForUpdate();

        // Attempt to parse the json    
        pjs::Parser parser;
        pdy::Var varRoot = parser.parse( rs );

        // Get a pointer to the root object
        pjs::Object::Ptr jsRoot = varRoot.extract< pjs::Object::Ptr >();

        bool isAvailable = false;
        if( jsRoot->has( "isAvailable" ) )
        {
            isAvailable = jsRoot->getValue<bool>( "isAvailable" );
        }

        bool isOwned     = false;
        if( jsRoot->has( "isOwned" ) )
        {
            isOwned = jsRoot->getValue<bool>( "isOwned" );
        }

        HNodeID ownerHNID;
        if( jsRoot->has( "owner_hnodeID" ) )
        {
            ownerHNID.setFromStr( jsRoot->getValue<std::string>( "owner_hnodeID" ) );
        }

        std::cout << "updateDeviceOwnerInfo - isOwned: " << isOwned << "  isAvail: " << isAvailable << "  ownerCRC32: " << ownerHNID.getCRC32AsHexStr() << std::endl;

        // Determine how to update the device ownership information
        if( isOwned == true )
        {
            // Save away the owners hnodeID
            device.setOwnerID( ownerHNID );

            std::cout << "updateDeviceOwnerInfo - ownerCompare - " << m_selfHnodeID.getCRC32() << " : " << ownerHNID.getCRC32() << std::endl;

            // Check if we are the owner
            if( m_selfHnodeID.getCRC32() == ownerHNID.getCRC32() )
                device.setOwnershipState( HNMDR_OWNER_STATE_MINE );
            else
                device.setOwnershipState( HNMDR_OWNER_STATE_OTHER );
        }
        else if( isAvailable == true )
        {
            device.clearOwnerID();
            device.setOwnershipState( HNMDR_OWNER_STATE_AVAILABLE );
        }
        else
        {
            device.clearOwnerID();
            device.setOwnershipState( HNMDR_OWNER_STATE_UNAVAILABLE );
        }

        device.unlockForUpdate();
    }
    catch( Poco::Exception ex )
    {
        device.setOwnershipState( HNMDR_OWNER_STATE_UNKNOWN );
        device.unlockForUpdate();
        std::cout << "HNMDMgmtCmd::setFromJSON exception: " << ex.displayText() << std::endl;
        // Request body was not understood
        return HNMDL_RESULT_FAILURE;
    }

    return HNMDL_RESULT_SUCCESS;
}

HNMDL_RESULT_T
HNManagedDeviceArbiter::sendDeviceClaimRequest( HNMDARecord &device )
{
    Poco::URI uri;
    HNMDARAddress dcInfo;
    pjs::Object jsRoot;

    device.lockForUpdate();

    if( device.findPreferredConnection( HMDAR_ADDRTYPE_IPV4, dcInfo ) != HNMDL_RESULT_SUCCESS )
    {
        device.unlockForUpdate();
        return HNMDL_RESULT_FAILURE;
    }

    device.unlockForUpdate();

    uri.setScheme( "http" );
    uri.setHost( dcInfo.getAddress() );
    uri.setPort( dcInfo.getPort() );
    uri.setPath( "/hnode2/device/owner" );

    pns::HTTPClientSession session( uri.getHost(), uri.getPort() );
    pns::HTTPRequest request( pns::HTTPRequest::HTTP_PUT, uri.getPathAndQuery(), pns::HTTPMessage::HTTP_1_1 );
    pns::HTTPResponse response;

    request.setContentType( "application/json" );

    // Build the json payload to send
    std::ostream& os = session.sendRequest( request );

    std::string hnodeIDStr;
    m_selfHnodeID.getStr( hnodeIDStr );
    jsRoot.set( "owner_hnodeID", hnodeIDStr );

    // Render into a json string.
    try {
        pjs::Stringifier::stringify( jsRoot, os );
    } catch( Poco::Exception& ex ) {
        std::cerr << ex.displayText() << std::endl;
        return HNMDL_RESULT_FAILURE;
    }
 
    std::istream& rs = session.receiveResponse( response );
    std::cout << response.getStatus() << " " << response.getReason() << std::endl;

    if( response.getStatus() != Poco::Net::HTTPResponse::HTTP_OK )
    {
        return HNMDL_RESULT_FAILURE;
    }

    return HNMDL_RESULT_SUCCESS;
}

HNMDL_RESULT_T
HNManagedDeviceArbiter::sendDeviceReleaseRequest( HNMDARecord &device )
{
    Poco::URI uri;
    HNMDARAddress dcInfo;

    device.lockForUpdate();

    if( device.findPreferredConnection( HMDAR_ADDRTYPE_IPV4, dcInfo ) != HNMDL_RESULT_SUCCESS )
    {
        device.unlockForUpdate();
        return HNMDL_RESULT_FAILURE;
    }

    device.unlockForUpdate();

    uri.setScheme( "http" );
    uri.setHost( dcInfo.getAddress() );
    uri.setPort( dcInfo.getPort() );
    uri.setPath( "/hnode2/device/owner" );

    pns::HTTPClientSession session( uri.getHost(), uri.getPort() );
    pns::HTTPRequest request( pns::HTTPRequest::HTTP_DELETE, uri.getPathAndQuery(), pns::HTTPMessage::HTTP_1_1 );
    pns::HTTPResponse response;

    session.sendRequest( request );
 
    std::istream& rs = session.receiveResponse( response );
    std::cout << response.getStatus() << " " << response.getReason() << std::endl;

    if( response.getStatus() != Poco::Net::HTTPResponse::HTTP_OK )
    {
        return HNMDL_RESULT_FAILURE;
    }

    return HNMDL_RESULT_SUCCESS;
}

HNMDL_RESULT_T
HNManagedDeviceArbiter::sendDeviceSetParameters( HNMDARecord &device )
{
    Poco::URI uri;
    HNMDARAddress dcInfo;

    device.lockForUpdate();

    if( device.findPreferredConnection( HMDAR_ADDRTYPE_IPV4, dcInfo ) != HNMDL_RESULT_SUCCESS )
    {
        device.unlockForUpdate();
        return HNMDL_RESULT_FAILURE;
    }

    device.unlockForUpdate();

    uri.setScheme( "http" );
    uri.setHost( dcInfo.getAddress() );
    uri.setPort( dcInfo.getPort() );
    uri.setPath( "/hnode2/device/info" );

    pns::HTTPClientSession session( uri.getHost(), uri.getPort() );
    pns::HTTPRequest request( pns::HTTPRequest::HTTP_PUT, uri.getPathAndQuery(), pns::HTTPMessage::HTTP_1_1 );
    pns::HTTPResponse response;

    session.sendRequest( request );
 
    std::istream& rs = session.receiveResponse( response );
    std::cout << response.getStatus() << " " << response.getReason() << std::endl;

    if( response.getStatus() != Poco::Net::HTTPResponse::HTTP_OK )
    {
        return HNMDL_RESULT_FAILURE;
    }

    return HNMDL_RESULT_SUCCESS;
}
