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

}

HNMDARecord::~HNMDARecord()
{

}

void 
HNMDARecord::setManagementState( HNMDR_MGMT_STATE_T value )
{
    m_mgmtState = value;
}

#if 0
void 
HNMDARecord::setOwnershipState( HNMDR_OWNER_STATE_T value )
{
    ownershipState = value;
}
#endif

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
    else if( HNMDR_MGMT_STATE_OWNER_CLAIM == m_mgmtState )
        return "OWNER_CLAIM";
    else if( HNMDR_MGMT_STATE_OWNER_AFFIRM == m_mgmtState )
        return "OWNER_AFFIRM";
    else if( HNMDR_MGMT_STATE_DISAPPEARING == m_mgmtState )
        return "DISAPPEARING";
    else if( HNMDR_MGMT_STATE_OFFLINE == m_mgmtState )
        return "OFFLINE";
    else if( HNMDR_MGMT_STATE_NOTSET == m_mgmtState )
        return "NOTSET";

    return "ERROR";
}

#if 0
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
#endif

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
    // Check if the record is existing, or if this is a new discovery.
    std::map< std::string, HNMDARecord >::iterator it = mdrMap.find( record.getCRC32ID() );

    if( it == mdrMap.end() )
    {
        // This is a new record
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
HNManagedDeviceArbiter::getDeviceListCopy( std::vector< HNMDARecord > &deviceList )
{
    for( std::map< std::string, HNMDARecord >::iterator it = mdrMap.begin(); it != mdrMap.end(); it++ )
    {    
        deviceList.push_back( it->second );
    }
}

HNMDL_RESULT_T 
HNManagedDeviceArbiter::lookupConnectionInfo( std::string crc32ID, HMDAR_ADDRTYPE_T preferredType, HNMDARAddress &connInfo )
{
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
    device.setManagementState( nextState );

    if( minValue < m_monitorWaitTime )
        m_monitorWaitTime = minValue;
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
            std::cout << "  Device - crc32: " << it->second.getCRC32ID() << "  type: " << it->second.getDeviceType() << "   state: " <<  it->second.getManagementStateStr() << std::endl;

            switch( it->second.getManagementState() )
            {
                // Added via Avahi Discovery
                case HNMDR_MGMT_STATE_DISCOVERED:
                    setNextMonitorState( it->second, HNMDR_MGMT_STATE_OPT_INFO, 0 );
                break;

                // Added from config file, prior association
                case HNMDR_MGMT_STATE_RECOVERED:
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
                break;

                // Device is waiting to be claimed 
                case HNMDR_MGMT_STATE_UNCLAIMED:
                break;

                // Device is currently owner by other manager
                case HNMDR_MGMT_STATE_OTHER_MGR:
                break;

                // Device is active, responding to period health checks
                case HNMDR_MGMT_STATE_ACTIVE:
                break;

                // REST write to establish device management
                case HNMDR_MGMT_STATE_OWNER_CLAIM:
                break;

                // REST write to reassert management configuration settings.
                case HNMDR_MGMT_STATE_OWNER_AFFIRM:
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
            }
        }
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

HNMDL_RESULT_T
HNManagedDeviceArbiter::updateDeviceOperationalInfo( HNMDARecord &device )
{
    Poco::URI uri;
    HNMDARAddress dcInfo;

    if( device.findPreferredConnection( HMDAR_ADDRTYPE_IPV4, dcInfo ) != HNMDL_RESULT_SUCCESS )
    {
        return HNMDL_RESULT_FAILURE;
    }

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

    std::string body;
    Poco::StreamCopier::copyToString( rs, body );
    std::cout << body << std::endl;

    return HNMDL_RESULT_SUCCESS;
}



