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
        return "update_device_fields";
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
    else if( "update_device_fields" == value )
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

std::string
HNMDMgmtCmd::getName()
{
    return m_name;
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

void
HNMDMgmtCmd::getUpdateFieldsJSON( std::ostream &os )
{
    pjs::Object jsRoot;

    if( m_fieldMask == HNMDC_FLDMASK_NONE )
        return;

    if( m_fieldMask & HNMDC_FLDMASK_NAME )
    {
        jsRoot.set( "name", getName() );
    }

    // Render into a json string.
    try {
        pjs::Stringifier::stringify( jsRoot, os );
    } catch( Poco::Exception& ex ) {
        std::cerr << ex.displayText() << std::endl;
    }
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


HNMDServiceEndpoint::HNMDServiceEndpoint()
{
    m_visited = false;
}

HNMDServiceEndpoint::~HNMDServiceEndpoint()
{

}

void
HNMDServiceEndpoint::clear()
{
    m_rootURI.clear();
}

void
HNMDServiceEndpoint::setType( std::string type )
{
    m_type = type;
}

std::string
HNMDServiceEndpoint::getType()
{
    return m_type;
}

void
HNMDServiceEndpoint::setVersion( std::string version )
{
    m_version = version;
}

std::string
HNMDServiceEndpoint::getVersion()
{
    return m_version;
}

void
HNMDServiceEndpoint::setRootURIFromStr( std::string uri )
{
    std::cout << "HNMDServiceEndpoint::setRootURIFromStr - uri: " << uri << std::endl;
    m_rootURI = uri;
}

std::string
HNMDServiceEndpoint::getRootURIAsStr()
{
    return m_rootURI;
}

void
HNMDServiceEndpoint::setVisited( bool value )
{
    m_visited = value;
}

bool
HNMDServiceEndpoint::getVisited()
{
    return m_visited;
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

    m_discID       = srcObj.m_discID;
    m_hnodeID      = srcObj.m_hnodeID;
    m_devType      = srcObj.m_devType;
    m_devVersion   = srcObj.m_devVersion;
    m_instance     = srcObj.m_instance;
    m_name         = srcObj.m_name;

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
    else if( HNMDR_MGMT_STATE_SRV_PROVIDE_INFO == m_mgmtState )
        return "SRV_PROVIDE_INFO";
    else if( HNMDR_MGMT_STATE_SRV_MAPPING_INFO == m_mgmtState )
        return "SRV_MAPPING_INFO";
    else if( HNMDR_MGMT_STATE_SRV_MAP_UPDATE == m_mgmtState )
        return "SRV_MAP_UPDATE";
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
    m_discID = value;
}

void 
HNMDARecord::setDeviceType( std::string value )
{
    m_devType = value;
}

void 
HNMDARecord::setDeviceVersion( std::string value )
{
    m_devVersion = value;
}

void 
HNMDARecord::setHNodeIDFromStr( std::string value )
{
    m_hnodeID.setFromStr( value );
}

void 
HNMDARecord::setInstance( std::string value )
{
    m_instance = value;
}

void 
HNMDARecord::setName( std::string value )
{
    m_name = value;
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
    return m_discID;
}

std::string 
HNMDARecord::getDeviceType()
{
    return m_devType;
}

std::string 
HNMDARecord::getDeviceVersion()
{
    return m_devVersion;
}

std::string 
HNMDARecord::getHNodeIDStr()
{
    std::string rtnStr;
    m_hnodeID.getStr( rtnStr );
    return rtnStr;
}

std::string 
HNMDARecord::getInstance()
{
    return m_instance;
}

std::string 
HNMDARecord::getName()
{
    return m_name;
}

std::string 
HNMDARecord::getCRC32ID()
{
    return m_hnodeID.getCRC32AsHexStr();
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
HNMDARecord::startSrvProviderUpdates()
{
    std::cout << "startSrvProviderUpdates: " << m_srvMapProvided.size() << std::endl;

    // Go through each service provider and mark as not visited
    std::map< std::string, HNMDServiceEndpoint >::iterator it;
    for( it = m_srvMapProvided.begin(); it != m_srvMapProvided.end(); it++ )
    {
        it->second.setVisited( false );
    }
}

HNMDServiceEndpoint&
HNMDARecord::updateSrvProvider( std::string srvType, bool &added )
{
    std::cout << "updateSrvProvider - size: " << m_srvMapProvided.size() << "  type: " << srvType << std::endl;

    added = false;

    // Check if the record already exists.
    std::map< std::string, HNMDServiceEndpoint>::iterator it = m_srvMapProvided.find( srvType );

    // If it does exist, return the existing record
    if( it != m_srvMapProvided.end() )
    {
        // Mark the record as being visited
        // so that the map can be cleaned up
        // at the end if a service goes away
        it->second.setVisited( true );

        // Return the reference
        return it->second;
    }

    added = true;

    // The record didn't exist before so create a new one.
    HNMDServiceEndpoint tmpEP;

    tmpEP.setType( srvType );
    tmpEP.setVisited(true);

    std::pair< std::map<std::string, HNMDServiceEndpoint>::iterator, bool > rstPair = 
        m_srvMapProvided.insert( std::pair< std::string, HNMDServiceEndpoint >( srvType, tmpEP ) );

    return rstPair.first->second;
}

void
HNMDARecord::abandonSrvProviderUpdates()
{
    // The update operation encountered an exception, clean up if necessary
}

bool
HNMDARecord::completeSrvProviderUpdates()
{
    bool changed = false;

    std::cout << "completeSrvProviderUpdates: " << m_srvMapProvided.size() << std::endl;

    // Go through each service provider, if it wasn't visited then get rid of it.
    std::map< std::string, HNMDServiceEndpoint >::iterator it;
    for( it = m_srvMapProvided.begin(); it != m_srvMapProvided.end(); )
    {
        std::cout << "completeSrvProviderUpdates - type: " << it->first << "  visited: " << it->second.getVisited() << std::endl;

        if( it->second.getVisited() == false )
        {
            changed = true;
            m_srvMapProvided.erase( it++ );
        }
        else
            it++;
    }

    return changed;
}

void
HNMDARecord::getSrvProviderTSList( std::vector< std::string > &srvTypesList )
{
    srvTypesList.clear();

    std::cout << "getSrvProviderTSList - size: " << m_srvMapProvided.size() << std::endl;
    
    std::map< std::string, HNMDServiceEndpoint >::iterator it;
    for( it = m_srvMapProvided.begin(); it != m_srvMapProvided.end(); it++ )
        srvTypesList.push_back( it->second.getType() );
}

void
HNMDARecord::startSrvMappingUpdates()
{
    std::cout << "startSrvMappingUpdates: " << m_srvMapDesired.size() << std::endl;

    // Go through each service mapping and mark as not visited
    std::map< std::string, HNMDServiceEndpoint >::iterator it;
    for( it = m_srvMapDesired.begin(); it != m_srvMapDesired.end(); it++ )
    {
        it->second.setVisited( false );
    }
}

HNMDServiceEndpoint&
HNMDARecord::updateSrvMapping( std::string srvType, bool &added )
{
    std::cout << "updateSrvMapping - size: " << m_srvMapDesired.size() << "  type: " << srvType << std::endl;

    added = false;

    // Check if the record already exists.
    std::map< std::string, HNMDServiceEndpoint>::iterator it = m_srvMapDesired.find( srvType );

    // If it does exist, return the existing record
    if( it != m_srvMapDesired.end() )
    {
        // Mark the record as being visited
        // so that the map can be cleaned up
        // at the end if a service goes away
        it->second.setVisited( true );

        // Return the reference
        return it->second;
    }

    added = true;
    
    // The record didn't exist before so create a new one.
    HNMDServiceEndpoint tmpEP;

    tmpEP.setType( srvType );
    tmpEP.setVisited(true);

    std::pair< std::map<std::string, HNMDServiceEndpoint>::iterator, bool > rstPair = 
        m_srvMapDesired.insert( std::pair< std::string, HNMDServiceEndpoint >( srvType, tmpEP ) );

    return rstPair.first->second;
}

void
HNMDARecord::abandonSrvMappingUpdates()
{
    // The update operation encountered an exception, clean up if necessary
}

bool
HNMDARecord::completeSrvMappingUpdates()
{
    bool changed = false;

    std::cout << "completeSrvMappingUpdates: " << m_srvMapDesired.size() << std::endl;

    // Go through each service mapping, if it wasn't visited then get rid of it.
    std::map< std::string, HNMDServiceEndpoint >::iterator it;
    for( it = m_srvMapDesired.begin(); it != m_srvMapDesired.end(); )
    {
        std::cout << "completeSrvMappingUpdates - type: " << it->first << "  visited: " << it->second.getVisited() << std::endl;

        if( it->second.getVisited() == false )
        {
            changed = true;
            m_srvMapDesired.erase( it++ );
        }
        else
            it++;
    }

    return changed;
}

void
HNMDARecord::getSrvMappingTSList( std::vector< std::string > &srvTypesList )
{
    srvTypesList.clear();

    std::cout << "getSrvMappingTSList - size: " << m_srvMapDesired.size() << std::endl;
    
    std::map< std::string, HNMDServiceEndpoint >::iterator it;
    for( it = m_srvMapDesired.begin(); it != m_srvMapDesired.end(); it++ )
        srvTypesList.push_back( it->second.getType() );
}

void 
HNMDARecord::debugPrint( uint offset )
{
    printf( "%*.*s%s  %s\n", offset, offset, " ", m_hnodeID.getCRC32AsHexStr().c_str(), getManagementStateStr().c_str() );
 
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

HNMDServiceDevRef::HNMDServiceDevRef()
{

}

HNMDServiceDevRef::~HNMDServiceDevRef()
{

}

void
HNMDServiceDevRef::setDevName( std::string value )
{
    m_name = value;
}

std::string
HNMDServiceDevRef::getDevName()
{
    return m_name;
}

void
HNMDServiceDevRef::setDevCRC32ID( std::string value )
{
    m_crc32ID = value;
}

std::string
HNMDServiceDevRef::getDevCRC32ID()
{
    return m_crc32ID;
}

HNMDServiceInfo::HNMDServiceInfo()
{

}

HNMDServiceInfo::~HNMDServiceInfo()
{

}

void
HNMDServiceInfo::setSrvType( std::string value )
{
    m_srvType = value;
}

std::string 
HNMDServiceInfo::getSrvType()
{
    return m_srvType;
}

std::vector< HNMDServiceDevRef >& 
HNMDServiceInfo::getDeviceListRef()
{
    return m_devRefList;
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
        {
            record.setManagementState( HNMDR_MGMT_STATE_SELF );

            // The health and logging sinks are both built into the management
            // node itself.
            // HNMDServiceEndpoint srvRec;
            // HNMDARAddress addrInfo;

            // if( record.findPreferredConnection( HMDAR_ADDRTYPE_IPV4, addrInfo ) == HNMDL_RESULT_SUCCESS )
            // {
                //srvRec.setRootURIFromStr( addrInfo.getURL("http", "hnode2/mgmt/healthSink") );
                //m_serviceMap.insert( std::pair<std::string, HNMDServiceEndpoint>("health", srvRec ));

                //srvRec.setRootURIFromStr( addrInfo.getURL("http", "hnode2/mgmt/logSink") );
                //m_serviceMap.insert( std::pair<std::string, HNMDServiceEndpoint>("logging", srvRec ));
            // }
        }
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
                            setNextMonitorState( it->second, HNMDR_MGMT_STATE_SRV_PROVIDE_INFO, 0 );
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

                // REST read for services provided
                case HNMDR_MGMT_STATE_SRV_PROVIDE_INFO:
                    if( updateDeviceServicesProvideInfo( it->second ) != HNMDL_RESULT_SUCCESS )
                        setNextMonitorState( it->second, HNMDR_MGMT_STATE_OFFLINE, 10 );
                    else
                        setNextMonitorState( it->second, HNMDR_MGMT_STATE_SRV_MAPPING_INFO, 0 );
                break;

                // REST read for desired services and current mappings
                case HNMDR_MGMT_STATE_SRV_MAPPING_INFO:
                    if( updateDeviceServicesMappingInfo( it->second ) != HNMDL_RESULT_SUCCESS )
                        setNextMonitorState( it->second, HNMDR_MGMT_STATE_OFFLINE, 10 );
                    else
                        setNextMonitorState( it->second, HNMDR_MGMT_STATE_ACTIVE, 0 );
                break;
                
                // REST put to update desired service mappings
                case HNMDR_MGMT_STATE_SRV_MAP_UPDATE:
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
                        setNextMonitorState( it->second, HNMDR_MGMT_STATE_OPT_INFO, 2 );
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
        
    // Track any updates
    bool changed = false;

    // {
    //   "crc32ID" : "4900fb4e",
    //   "deviceType" : "hnode2-irrigation-device",
    //   "hnodeID" : "29:72:e2:ae:db:90:11:ec:89:a1:b8:27:eb:52:da:55",
    //   "instance" : "default",
    //   "name" : "InitialName",
    //   "version" : "2.0.0"
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

        if( jsRoot->has( "deviceType" ) )
        {
            std::string dtype = jsRoot->getValue<std::string>( "deviceType" );
            if( device.getDeviceType() != dtype )
            {   
                changed = true;
                device.setDeviceType( dtype );
            }
        }

        if( jsRoot->has( "instance" ) )
        {
            std::string inst = jsRoot->getValue<std::string>( "instance" );
            if( device.getInstance() != inst )
            {   
                changed = true;
                device.setInstance( inst );
            }
        }

        if( jsRoot->has( "name" ) )
        {
            std::string name = jsRoot->getValue<std::string>( "name" );
            std::cout << "Device Info returned name: " << name << std::endl;
            if( device.getName() != name )
            {   
                changed = true;
                device.setName( name );
            }
        }

        if( jsRoot->has( "version" ) )
        {
            std::string version = jsRoot->getValue<std::string>( "version" );
            if( device.getDeviceVersion() != version )
            {   
                changed = true;
                device.setDeviceVersion( version );
            }
        }

#if 0
        if( jsRoot->has( "desiredServices" ) )
        {
            uint newDServFlags = HNMDR_DSERV_NOTSET;
            pjs::Array::Ptr jsSvcList = jsRoot->getArray( "desiredServices" );

            for( uint index = 0; index < jsSvcList->size(); index++ )
            {
                std::string value = jsSvcList->getElement<std::string>( index );
                std::cout << "Device Info - DServ: " << value << std::endl;
                if( "health" == value )
                    newDServFlags |= HNMDR_DSERV_HEALTH;
                else if( "event" == value )
                    newDServFlags |= HNMDR_DSERV_EVENT;
                else if( "logging" == value )
                    newDServFlags |= HNMDR_DSERV_LOGGING;
                else if( "data" == value )
                    newDServFlags |= HNMDR_DSERV_DATA;
                else if( "key-value" == value )
                    newDServFlags |= HNMDR_DSERV_KEYVALUE;
            }
            
            device.setDesiredServices( (HNMDR_DSERV_T) newDServFlags );
        }
#endif

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

    if( changed == true )
    {
        std::cout << "Device values changed." << std::endl;
    }

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

    // Format the update parameters
    std::ostream &os = session.sendRequest( request );
    device.getDeviceMgmtCmdRef().getUpdateFieldsJSON( os );
 
    std::istream& rs = session.receiveResponse( response );
    std::cout << response.getStatus() << " " << response.getReason() << std::endl;

    if( response.getStatus() != Poco::Net::HTTPResponse::HTTP_OK )
    {
        return HNMDL_RESULT_FAILURE;
    }

    return HNMDL_RESULT_SUCCESS;
}

HNMDL_RESULT_T
HNManagedDeviceArbiter::updateDeviceServicesProvideInfo( HNMDARecord &device )
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
    uri.setPath( "/hnode2/device/services/provided" );

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
        
    // Track any updates
    bool changed = false;

    // [
    //   {
    //     "type" : "hnst-health-src",
    //     "version" : "1.0.0",
    //     "uri" : ""
    //   }
    // ]
    // Parse the json body of the request
    try
    {
        device.lockForUpdate();

        // Attempt to parse the json    
        pjs::Parser parser;
        pdy::Var varRoot = parser.parse( rs );

        // Note the start of potential service list updates
        device.startSrvProviderUpdates();

        // Get a pointer to the root object
        pjs::Array::Ptr jsRoot = varRoot.extract< pjs::Array::Ptr >();

        for( uint i = 0; i < jsRoot->size(); i++ )
        {
            pjs::Object::Ptr jsSrvObj = jsRoot->getObject( i );

            if( jsSrvObj.isNull() )
                continue;

            if( jsSrvObj->has( "type" ) )
            {
                std::string stype = jsSrvObj->getValue<std::string>( "type" );

                std::cout << "updateDeviceServicesProvideInfo - type: " << stype << std::endl;

                // Get a record object reference for this service type.
                // If it hasn't been seen before, a new record will be created.
                bool added = false;
                HNMDServiceEndpoint &srvRef = device.updateSrvProvider( stype, added );

                if( added == true )
                    changed = true;

                if( jsSrvObj->has( "version" ) )
                {
                    std::string version = jsSrvObj->getValue<std::string>( "version" );
                    if( srvRef.getVersion() != version )
                    {
                        srvRef.setVersion( version );
                        changed = true;
                    }
                }

                if( jsSrvObj->has( "uri" ) )
                {
                    std::string uri = jsSrvObj->getValue<std::string>( "uri" );
                    if( srvRef.getRootURIAsStr() != uri )
                    {
                        srvRef.setRootURIFromStr( uri );
                        changed = true;
                    }
                }

            }
            else
            {
                // Mandatory service type field missing, skip to next record
                continue;
            }
            
        }

        // Done with updates to services provided list
        if( device.completeSrvProviderUpdates() == true )
            changed = true;

        device.unlockForUpdate();

    }
    catch( Poco::Exception ex )
    {
        // Done with updates to services provided list
        device.abandonSrvProviderUpdates();

        device.unlockForUpdate();

        std::cout << "HNMDMgmtCmd::updateDeviceServicesProvideInfo exception: " << ex.displayText() << std::endl;
        // Request body was not understood
        return HNMDL_RESULT_FAILURE;
    }

    // If anything changed then update the by-service-type lookup maps
    // in the arbiter as well.
    if( changed == true )
    {
        std::cout << "Device list service providers changed." << std::endl;
        rebuildSrvProviderMap();
    }

    return HNMDL_RESULT_SUCCESS;
}

void 
HNManagedDeviceArbiter::rebuildSrvProviderMap()
{
    // Clear the old map since it will be rebuilt
    m_providerMap.clear();

    // Walk through each device
    std::map< std::string, HNMDARecord >::iterator it;
    for( it = mdrMap.begin(); it != mdrMap.end(); it++ )
    {
        // Get a list of provided endpoint type strings
        std::vector< std::string > srvTypes;

        std::cout << "rebuildSrvProviderMap - device: " << it->second.getCRC32ID() << std::endl;

        it->second.getSrvProviderTSList( srvTypes );

        std::cout << "rebuildSrvProviderMap - tscnt: " << srvTypes.size() << std::endl;

        // Add this device to the srvProviderMap for each service it provides
        for( std::vector< std::string >::iterator sit = srvTypes.begin(); sit != srvTypes.end(); sit++ )
        {
            std::cout << "rebuildSrvProviderMap - tsstr: " << *sit << std::endl;

            std::map< std::string, std::vector< HNMDARecord* > >::iterator mit;
            mit = m_providerMap.find( *sit );
            
            if( mit != m_providerMap.end() )
            {
                std::cout << "rebuildSrvProviderMap - add-new" << std::endl;
                mit->second.push_back( &(it->second) );
            }
            else
            {
                std::cout << "rebuildSrvProviderMap - add-tail" << std::endl;
                std::vector< HNMDARecord* > tmpList;
                tmpList.push_back( &(it->second) );
                m_providerMap.insert( std::pair< std::string, std::vector< HNMDARecord* > >( *sit, tmpList ) );
            }
        }    
    }
}

void
HNManagedDeviceArbiter::buildSrvProviderInfoList( std::vector< HNMDServiceInfo > &srvList )
{
    // Start with a clean slate
    srvList.clear();

    // Walk through each service
    std::map< std::string, std::vector< HNMDARecord * > >::iterator it;
    for( it = m_providerMap.begin(); it != m_providerMap.end(); it++ )
    {
        HNMDServiceInfo srvInfo;

        srvInfo.setSrvType( it->first );

        srvList.push_back( srvInfo );

        for( std::vector< HNMDARecord * >::iterator dit = it->second.begin(); dit != it->second.end(); dit++ )
        {
            HNMDServiceDevRef devRef;

            devRef.setDevName( (*dit)->getName() );
            devRef.setDevCRC32ID( (*dit)->getCRC32ID() );

            srvList.back().getDeviceListRef().push_back( devRef );
        }
    }
}

HNMDL_RESULT_T
HNManagedDeviceArbiter::updateDeviceServicesMappingInfo( HNMDARecord &device )
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
    uri.setPath( "/hnode2/device/services/mappings" );

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
        
    // Track any updates
    bool changed = false;

    // [
    //   {
    //     "type" : "hnst-health-src",
    //     "version" : "1.0.0",
    //     "uri" : ""
    //   }
    // ]
    // Parse the json body of the request
    try
    {
        device.lockForUpdate();

        // Attempt to parse the json    
        pjs::Parser parser;
        pdy::Var varRoot = parser.parse( rs );

        // Note the start of potential service list updates
        device.startSrvMappingUpdates();

        // Get a pointer to the root object
        pjs::Array::Ptr jsRoot = varRoot.extract< pjs::Array::Ptr >();

        for( uint i = 0; i < jsRoot->size(); i++ )
        {
            pjs::Object::Ptr jsSrvObj = jsRoot->getObject( i );

            if( jsSrvObj.isNull() )
                continue;

            if( jsSrvObj->has( "type" ) )
            {
                std::string stype = jsSrvObj->getValue<std::string>( "type" );

                std::cout << "updateDeviceServicesMappingInfo - type: " << stype << std::endl;

                // Get a record object reference for this service type.
                // If it hasn't been seen before, a new record will be created.
                bool added = false;
                HNMDServiceEndpoint &srvRef = device.updateSrvMapping( stype, added );

                if( added == true )
                    changed = true;

                if( jsSrvObj->has( "version" ) )
                {
                    std::string version = jsSrvObj->getValue<std::string>( "version" );
                    if( srvRef.getVersion() != version )
                    {
                        srvRef.setVersion( version );
                        changed = true;
                    }
                }

                if( jsSrvObj->has( "uri" ) )
                {
                    std::string uri = jsSrvObj->getValue<std::string>( "uri" );
                    if( srvRef.getRootURIAsStr() != uri )
                    {
                        srvRef.setRootURIFromStr( uri );
                        changed = true;
                    }
                }

            }
            else
            {
                // Mandatory service type field missing, skip to next record
                continue;
            }
            
        }

        // Done with updates to services provided list
        if( device.completeSrvMappingUpdates() == true )
            changed = true;

        device.unlockForUpdate();

    }
    catch( Poco::Exception ex )
    {
        // Done with updates to services provided list
        device.abandonSrvMappingUpdates();

        device.unlockForUpdate();

        std::cout << "HNMDMgmtCmd::updateDeviceServicesMappingInfo exception: " << ex.displayText() << std::endl;
        // Request body was not understood
        return HNMDL_RESULT_FAILURE;
    }

    // If anything changed then update the by-service-type lookup maps
    // in the arbiter as well.
    if( changed == true )
    {
        std::cout << "Device list service mappings changed." << std::endl;
        rebuildSrvMappings();
    }

    return HNMDL_RESULT_SUCCESS;
}

void 
HNManagedDeviceArbiter::rebuildSrvMappings()
{
    // Clear the old map since it will be rebuilt
    m_servicesMap.clear();
    
    // Walk through each device
    std::map< std::string, HNMDARecord >::iterator it;
    for( it = mdrMap.begin(); it != mdrMap.end(); it++ )
    {
        // Get a list of provided endpoint type strings
        std::vector< std::string > srvTypes;

        std::cout << "rebuildSrvMapping - device: " << it->second.getCRC32ID() << std::endl;

        it->second.getSrvMappingTSList( srvTypes );

        std::cout << "rebuildSrvMapping - tscnt: " << srvTypes.size() << std::endl;

        // Add this device to the srvMappings for each service it provides
        for( std::vector< std::string >::iterator sit = srvTypes.begin(); sit != srvTypes.end(); sit++ )
        {
            std::cout << "rebuildSrvMapping - tsstr: " << *sit << std::endl;

            std::map< std::string, std::vector< HNMDARecord* > >::iterator mit;
            mit = m_servicesMap.find( *sit );
            
            if( mit != m_servicesMap.end() )
            {
                std::cout << "rebuildSrvMapping - add-new" << std::endl;
                mit->second.push_back( &(it->second) );
            }
            else
            {
                std::cout << "rebuildSrvMapping - add-tail" << std::endl;
                std::vector< HNMDARecord* > tmpList;
                tmpList.push_back( &(it->second) );
                m_servicesMap.insert( std::pair< std::string, std::vector< HNMDARecord* > >( *sit, tmpList ) );
            }
        }    
    }
}

void
HNManagedDeviceArbiter::buildSrvMappingInfoList( std::vector< HNMDServiceInfo > &srvList )
{
    // Start with a clean slate
    srvList.clear();

    std::cout << "buildSrvMappingInfoList - size: " << m_servicesMap.size() << std::endl;

    // Walk through each service
    std::map< std::string, std::vector< HNMDARecord * > >::iterator it;
    for( it = m_servicesMap.begin(); it != m_servicesMap.end(); it++ )
    {
        HNMDServiceInfo srvInfo;

        srvInfo.setSrvType( it->first );

        srvList.push_back( srvInfo );

        std::cout << "buildSrvMappingInfoList - size2: " << it->second.size() << std::endl;

        for( std::vector< HNMDARecord * >::iterator dit = it->second.begin(); dit != it->second.end(); dit++ )
        {
            HNMDServiceDevRef devRef;

            devRef.setDevName( (*dit)->getName() );
            devRef.setDevCRC32ID( (*dit)->getCRC32ID() );

            srvList.back().getDeviceListRef().push_back( devRef );
        }
    }
}
