#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/socket.h>

#include <syslog.h>

#include <iostream>
#include <sstream>

#include <jwt.h>

#include <Poco/Util/ServerApplication.h>
#include <Poco/Util/Option.h>
#include <Poco/Util/OptionSet.h>
#include <Poco/Util/HelpFormatter.h>
#include <Poco/Checksum.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/URI.h>
#include <Poco/StreamCopier.h>

#include <hnode2/HNodeDevice.h>

#include "HNManagementDevice.h"

using namespace Poco::Util;

namespace pjs = Poco::JSON;
namespace pdy = Poco::Dynamic;

// Forward declaration
extern const std::string g_HNode2MgmtRest;
extern const std::string g_HNode2ProxyMgmtAPI;

void 
HNManagementDevice::defineOptions( OptionSet& options )
{
    ServerApplication::defineOptions( options );

    options.addOption(
              Option("help", "h", "display help").required(false).repeatable(false));

    options.addOption(
              Option("debug","d", "Enable debug logging").required(false).repeatable(false));

    options.addOption(
              Option("instance", "", "Specify the instance name of this daemon.").required(false).repeatable(false).argument("name"));

}

void 
HNManagementDevice::handleOption( const std::string& name, const std::string& value )
{
    ServerApplication::handleOption( name, value );
    if( "help" == name )
        _helpRequested = true;
    else if( "debug" == name )
        _debugLogging = true;
    else if( "instance" == name )
    {
         _instancePresent = true;
         _instance = value;
    }
}

void 
HNManagementDevice::displayHelp()
{
    HelpFormatter helpFormatter(options());
    helpFormatter.setCommand(commandName());
    helpFormatter.setUsage("[options]");
    helpFormatter.setHeader("HNode2 Management Daemon.");
    helpFormatter.format(std::cout);
}

int 
HNManagementDevice::main( const std::vector<std::string>& args )
{
    HNAvahiBrowser avBrowser( HNODE_DEVICE_AVAHI_TYPE );

    if( _helpRequested )
    {
        displayHelp();
        return Application::EXIT_OK;
    }

    // Move me to before option processing.
    if( _instancePresent )
        m_instanceName = _instance;
    else
        m_instanceName = HNODE_MGMT_DEF_INSTANCE;

    // Check if the config file already exists, or should be initialized
    std::cout << "Looking for config file" << std::endl;
    if( configExists() == false )
    {
        initConfig();
    }

    // Pull in the device configuration file info
    readConfig();

    // Check if we are running as a daemon
    //if( config().getBool("application.runAsDaemon", false ) )
    //{
        // Configure where logging should end up
        //log.setDaemon( true );
    //}

    // Enable debug logging if requested
    if( _debugLogging == true )
    {
       //log.setLevelLimit( HNDL_LOG_LEVEL_ALL );
       //log.debug("Debug logging has been enabled.");
    }

    // Indicate startup
    //log.info( "Starting hnode2 switch daemon init" );

    // Setup HNode Device
    m_hnodeDev.setDeviceType( HNODE_MGMT_DEVTYPE );
    m_hnodeDev.setInstance( m_instanceName );
    m_hnodeDev.setName("mg1");
    m_hnodeDev.setPort(8400);

    HNDEndpoint hndEP;

    hndEP.setDispatch( "hnode2Mgmt", this );
    hndEP.setOpenAPIJson( g_HNode2MgmtRest ); 

    m_hnodeDev.addEndpoint( hndEP );
 
    // Setup the decoder for proxy requests that will be handled locally.
    registerProxyEndpointsFromOpenAPI( g_HNode2ProxyMgmtAPI );

    // Tell the arbiter our CRC32ID so we can filter self discovery
    m_arbiter.setSelfInfo( m_hnodeDev.getHNodeIDStr() );

    // Setup the queue for requests from the SCGI interface
    m_scgiRequestQueue.init();

    reqsink.setParentRequestQueue( &m_scgiRequestQueue );

    // Setup the queue for responses from the Proxy interface
    m_proxyResponseQueue.init();

    m_proxySeq.setParentResponseQueue( &m_proxyResponseQueue );

    // Initialize for event loop
    epollFD = epoll_create1( 0 );
    if( epollFD == -1 )
    {
        //log.error( "ERROR: Failure to create epoll event loop: %s", strerror(errno) );
        return Application::EXIT_SOFTWARE;
    }

    // Buffer where events are returned 
    events = (struct epoll_event *) calloc( MAXEVENTS, sizeof event );

    // Start accepting device notifications
    m_hnodeDev.setNotifySink( this );

    // Start the HNode Device
    m_hnodeDev.start();

    // Start the Managed Device Arbiter
    m_arbiter.start();

    // Start processing requests from the browser via SCGI
    reqsink.start( m_instanceName );

    // Start the AvahiBrowser component
    avBrowser.start();

    // Start the proxy sequencer
    m_proxySeq.start();

    // Hook the browser into the event loop
    int discoverFD = avBrowser.getEventQueue().getEventFD();
   
    if( addSocketToEPoll( discoverFD ) !=  HNMD_RESULT_SUCCESS )
    {
        return Application::EXIT_SOFTWARE;
    }

    // Hook the SCGI Request Queue into the event loop
    int scgiQFD = m_scgiRequestQueue.getEventFD();
   
    if( addSocketToEPoll( scgiQFD ) !=  HNMD_RESULT_SUCCESS )
    {
        return Application::EXIT_SOFTWARE;
    }

    // Hook the Proxy Sequencer Response Queue into the event loop
    int proxyQFD = m_proxyResponseQueue.getEventFD();
   
    if( addSocketToEPoll( proxyQFD ) !=  HNMD_RESULT_SUCCESS )
    {
        return Application::EXIT_SOFTWARE;
    }

    // The event loop 
    quit = false;
    while( quit == false )
    {
        int n;
        int i;
        struct tm newtime;
        time_t ltime;

        // Check for events
        n = epoll_wait( epollFD, events, MAXEVENTS, 2000 );

        // EPoll error
        if( n < 0 )
        {
            // If we've been interrupted by an incoming signal, continue, wait for socket indication
            if( errno == EINTR )
                continue;

            // Handle error
            //log.error( "ERROR: Failure report by epoll event loop: %s", strerror( errno ) );
            return Application::EXIT_SOFTWARE;
        }

        std::cout << "HNManagementDevice::monitor wakeup" << std::endl;

        // Check these critical tasks everytime
        // the event loop wakes up.
 
        // If it was a timeout then continue to next loop
        // skip socket related checks.
        if( n == 0 )
            continue;

        // Socket event
        for( i = 0; i < n; i++ )
	    {
            if( discoverFD == events[i].data.fd )
            {
                // Avahi Browser Event
                while( avBrowser.getEventQueue().getPostedCnt() )
                {
                    HNAvahiBrowserEvent *event = (HNAvahiBrowserEvent*) avBrowser.getEventQueue().aquireRecord();

                    std::cout << "=== Discover Event ===" << std::endl;
                    event->debugPrint();

                    switch( event->getEventType() )
                    {
                        case HNAB_EVTYPE_ADD:
                        {
                            HNMDARecord notifyRec;

                            notifyRec.setDiscoveryID( event->getName() );
                            notifyRec.setDeviceType( event->getTxtValue( "devType" ) );
                            notifyRec.setHNodeIDFromStr( event->getTxtValue( "hnodeID" ) );
                            notifyRec.setName( event->getTxtValue( "name" ) );

                            notifyRec.addAddressInfo( event->getHostname(), event->getAddress(), event->getPort() );

                            HNMDL_RESULT_T result = m_arbiter.notifyDiscoverAdd( notifyRec );
                            if( result != HNMDL_RESULT_SUCCESS )
                            {
                                // Note error
                            }
                        }
                        break;

                        case HNAB_EVTYPE_REMOVE:
                        break;
                    }

                    m_arbiter.debugPrint();

                    avBrowser.getEventQueue().releaseRecord( event );
                }
            }
            else if( scgiQFD == events[i].data.fd )
            {
                while( m_scgiRequestQueue.getPostedCnt() )
                {
                    HNSCGIRR *proxyRR = (HNSCGIRR *) m_scgiRequestQueue.aquireRecord();

                    std::cout << "HNManagementDevice::Received proxy request" << std::endl;

                    HNProxyTicket *proxyTicket = checkForProxyRequest( proxyRR );

                    if( proxyTicket != NULL )
                    {
                        std::cout << "Proxy request to device: " << proxyTicket->getCRC32ID() << std::endl;
                        m_proxySeq.getRequestQueue()->postRecord( proxyTicket );
                        continue;
                    }

                    HNOperationData *opData = mapProxyRequest( proxyRR );

                    if( opData == NULL )
                    {
                        proxyRR->getRspMsg().configAsNotFound();
                        reqsink.getProxyResponseQueue()->postRecord( proxyRR );
                        continue;
                    }

                    std::cout << "Local management device request: " << opData->getOpID() << std::endl;
                    handleLocalSCGIRequest( proxyRR, opData );
                    reqsink.getProxyResponseQueue()->postRecord( proxyRR );
                    delete opData;
                }
            }
            else if( proxyQFD == events[i].data.fd )
            {
                while( m_proxyResponseQueue.getPostedCnt() )
                {
                    HNProxyTicket *proxyTicket = (HNProxyTicket *) m_proxyResponseQueue.aquireRecord();

                    std::cout << "HNManagementDevice::Received proxy response" << std::endl;

                    HNSCGIRR *proxyRR = proxyTicket->getRR();

                    std::cout << "Deleting HNProxyTicket: " << proxyTicket << std::endl;

                    delete proxyTicket;

                    reqsink.getProxyResponseQueue()->postRecord( proxyRR );
                }
            }            
        }
    }

    m_proxySeq.shutdown();
    avBrowser.shutdown();
    reqsink.shutdown();
    m_arbiter.shutdown();
    //m_hnodeDev.shutdown();

    waitForTerminationRequest();

    std::cout << "Server teminated" << std::endl;

    return Application::EXIT_OK;
}


HNMD_RESULT_T
HNManagementDevice::addSocketToEPoll( int sfd )
{
    int flags, s;

    flags = fcntl( sfd, F_GETFL, 0 );
    if( flags == -1 )
    {
        syslog( LOG_ERR, "HNManagementDevice - Failed to get socket flags: %s", strerror(errno) );
        return HNMD_RESULT_FAILURE;
    }

    flags |= O_NONBLOCK;
    s = fcntl( sfd, F_SETFL, flags );
    if( s == -1 )
    {
        syslog( LOG_ERR, "HNManagementDevice - Failed to set socket flags: %s", strerror(errno) );
        return HNMD_RESULT_FAILURE; 
    }

    event.data.fd = sfd;
    event.events = EPOLLIN | EPOLLET;
    s = epoll_ctl( epollFD, EPOLL_CTL_ADD, sfd, &event );
    if( s == -1 )
    {
        return HNMD_RESULT_FAILURE;
    }

    return HNMD_RESULT_SUCCESS;
}

HNMD_RESULT_T
HNManagementDevice::removeSocketFromEPoll( int sfd )
{
    int s;

    s = epoll_ctl( epollFD, EPOLL_CTL_DEL, sfd, NULL );
    if( s == -1 )
    {
        return HNMD_RESULT_FAILURE;
    }

    return HNMD_RESULT_SUCCESS;
}

bool 
HNManagementDevice::configExists()
{
    HNodeConfigFile cfgFile;

    return cfgFile.configExists( HNODE_MGMT_DEVTYPE, m_instanceName );
}

HNMD_RESULT_T
HNManagementDevice::initConfig()
{
    HNodeConfigFile cfgFile;
    HNodeConfig     cfg;

    m_hnodeDev.initConfigSections( cfg );

    cfg.debugPrint(2);

    std::cout << "Saving config..." << std::endl;
    if( cfgFile.saveConfig( HNODE_MGMT_DEVTYPE, m_instanceName, cfg ) != HNC_RESULT_SUCCESS )
    {
        std::cout << "ERROR: Could not save initial configuration." << std::endl;
        return HNMD_RESULT_FAILURE;
    }

    return HNMD_RESULT_SUCCESS;
}

HNMD_RESULT_T
HNManagementDevice::readConfig()
{
    HNodeConfigFile cfgFile;
    HNodeConfig     cfg;

    if( configExists() == false )
        return HNMD_RESULT_FAILURE;

    std::cout << "Loading config..." << std::endl;

    if( cfgFile.loadConfig( HNODE_MGMT_DEVTYPE, m_instanceName, cfg ) != HNC_RESULT_SUCCESS )
    {
        std::cout << "ERROR: Could not load saved configuration." << std::endl;
        return HNMD_RESULT_FAILURE;
    }

    std::cout << "cl1" << std::endl;

    m_hnodeDev.readConfigSections( cfg );

    std::cout << "Config loaded" << std::endl;

    return HNMD_RESULT_SUCCESS;
}

HNMD_RESULT_T
HNManagementDevice::updateConfig()
{
    HNodeConfigFile cfgFile;
    HNodeConfig     cfg;

    cfg.debugPrint(2);

    std::cout << "Saving config..." << std::endl;
    if( cfgFile.saveConfig( HNODE_MGMT_DEVTYPE, m_instanceName, cfg ) != HNC_RESULT_SUCCESS )\
    {
        std::cout << "ERROR: Could not save configuration." << std::endl;
        return HNMD_RESULT_FAILURE;
    }
    std::cout << "Config saved" << std::endl;

    return HNMD_RESULT_SUCCESS;
}

void
HNManagementDevice::hndnConfigChange( HNodeDevice *parent )
{
    std::cout << "HNManagementDevice::hndnConfigChange() - entry" << std::endl;
}

void 
HNManagementDevice::dispatchEP( HNodeDevice *parent, HNOperationData *opData )
{
    //HNIDActionRequest action;

    std::cout << "HNManagementDevice::dispatchEP() - entry" << std::endl;
    std::cout << "  dispatchID: " << opData->getDispatchID() << std::endl;
    std::cout << "  opID: " << opData->getOpID() << std::endl;
    //std::cout << "  thread: " << std::this_thread::get_id() << std::endl;

    std::string opID = opData->getOpID();

    // GET "/hnode2/mgmt/status"
    if( "getStatus" == opID )
    {
        //action.setType( HNID_AR_TYPE_IRRSTATUS );
    }
    else
    {
        // Send back not implemented
        opData->responseSetStatusAndReason( HNR_HTTP_NOT_IMPLEMENTED );
        opData->responseSend();
        return;
    }

    //  FIXME - add real response code, Request was successful
    opData->responseSetStatusAndReason( HNR_HTTP_OK );

    // Return to caller
    opData->responseSend();
}

HNRestPath *
HNManagementDevice::addProxyPath( std::string dispatchID, std::string operationID, HNRestDispatchInterface *dispatchInf )
{
    HNRestPath newPath;
    HNRestPath *pathPtr;

    // Add new element
    m_proxyPathList.push_back( newPath );

    // Get pointer
    pathPtr = &m_proxyPathList.back();
    
    pathPtr->init( dispatchID, operationID, dispatchInf );

    return pathPtr;
}

void 
HNManagementDevice::registerProxyEndpointsFromOpenAPI( std::string openAPIJson )
{
    HNRestPath *path;

    // Invoke the json parser
    try
    {
        // Attempt to parse the provided openAPI input
        pjs::Parser parser;
        pdy::Var varRoot = parser.parse( openAPIJson );

        // Get a pointer to the root object
        pjs::Object::Ptr jsRoot = varRoot.extract< pjs::Object::Ptr >();

        // Make sure the "paths" field exists, otherwise nothing to do
        if( jsRoot->isObject( "paths" ) == false )
            return;

        // Extract the paths object
        pjs::Object::Ptr jsPathList = jsRoot->getObject( "paths" );

        // Iterate through the fields, each field represents a path
        for( pjs::Object::ConstIterator pit = jsPathList->begin(); pit != jsPathList->end(); pit++ )
        {
            std::vector< std::string > pathStrs;

            // Parse the extracted uri
            Poco::URI uri( pit->first );

            // Break it into tokens by '/'
            uri.getPathSegments( pathStrs );

            std::cout << "regend - uri: " << uri.toString() << std::endl;
            std::cout << "regend - segcnt: " << pathStrs.size() << std::endl;

            // Turn the iterator into a object pointer
            pjs::Object::Ptr jsPath = pit->second.extract< pjs::Object::Ptr >();

            // Iterate through the fields, each field represents a supported HTTP Method
            for( pjs::Object::ConstIterator mit = jsPath->begin(); mit != jsPath->end(); mit++ )
            {
                std::cout << "regend - method: " << mit->first << std::endl;

                // Get a pointer to the method object
                pjs::Object::Ptr jsMethod = mit->second.extract< pjs::Object::Ptr >();

                // The method object must contain a operationID field
                // which will be used by the dispatch Callback to know
                // the request that was made.
                std::string opID = jsMethod->getValue< std::string >( "operationId" );
                std::cout << "regend - opID: " << opID << std::endl;

                // We have everything we need so now the new path can be added to the 
                // http factory class
                path = addProxyPath( "HNManagementDevice", opID, NULL );

                // Record the method for this specific path record
                path->setMethod( mit->first );

                // Build up the path element array, taking into account url based parameters
                for( std::vector< std::string >::iterator sit = pathStrs.begin(); sit != pathStrs.end(); sit++ )
                {
                    std::cout << "PathComp: " << *sit << std::endl;
                    // Check if this is a parameter or a regular path element.
                    if( (sit->front() == '{') && (sit->back() == '}') )
                    {
                        // Add a parameter capture
                        std::string paramName( (sit->begin() + 1), (sit->end() - 1) );
                        std::cout << "regend - paramName: " << paramName << std::endl;
                        path->addPathElement( HNRPE_TYPE_PARAM, paramName );
                    }
                    else
                    {
                        // Add a regular path element
                        path->addPathElement( HNRPE_TYPE_PATH, *sit );
                    }
                } 
            }
        }

    }
    catch( Poco::Exception ex )
    {
        std::cerr << "registerProxyEndpointsFromOpenAPI - json error: " << ex.displayText() << std::endl;
        return;
    }
}

HNProxyTicket* 
HNManagementDevice::checkForProxyRequest( HNSCGIRR *reqRR )
{
    std::vector< std::string > pathStrs;
    Poco::URI uri( reqRR->getReqMsg().getURI() );

    std::cout << "checkForProxyRequest - method: " << reqRR->getReqMsg().getMethod() << std::endl;
    std::cout << "checkForProxyRequest - URI: " << uri.toString() << std::endl;

    // Break the uri into segments
    uri.getPathSegments( pathStrs );

    // Make sure we have at least the correct number of segments
    // to represent a device proxy request.  Then check that
    // url is of the form /hnode2/mgmt/device-proxy/{crc32ID}/*
    if( pathStrs.size() < 4 )
        return NULL;

    if( (pathStrs[0] != "hnode2") || (pathStrs[1] != "mgmt") || (pathStrs[2] != "device-proxy") )
        return NULL;

    // Grab the CRC32ID and try to look up the device. 
    std::string crc32ID = pathStrs[3];

    HNMDARAddress dcInfo;
    HNMDL_RESULT_T result = m_arbiter.lookupConnectionInfo( crc32ID, HMDAR_ADDRTYPE_IPV4, dcInfo );
    if( result != HNMDL_RESULT_SUCCESS )
    {
        std::cout << "WARNING: Proxy failed to lookup device: " << crc32ID << std::endl;
        return NULL;
    }

    // Allocate and fillout a ProxyTicket for return.
    HNProxyTicket *rtnTicket = new HNProxyTicket( reqRR );
    std::cout << "Allocated new HNProxyTicket: " << rtnTicket << std::endl;

    rtnTicket->setCRC32ID( crc32ID );
    rtnTicket->setAddress( dcInfo.getAddress() );
    rtnTicket->setPort( dcInfo.getPort() );
    rtnTicket->setQueryStr( uri.getRawQuery() );

    // Extract the remainder of the path for 
    // use when constructing the proxy request
    pathStrs.erase( pathStrs.begin(), pathStrs.begin()+4);
    rtnTicket->buildProxyPath( pathStrs );

    return rtnTicket;
}

HNOperationData*
HNManagementDevice::mapProxyRequest( HNSCGIRR *reqRR )
{
    HNOperationData *opData = NULL;
    std::vector< std::string > pathStrs;

    Poco::URI uri( reqRR->getReqMsg().getURI() );

    std::cout << "mapProxyRequest - method: " << reqRR->getReqMsg().getMethod() << std::endl;
    std::cout << "mapProxyRequest - URI: " << uri.toString() << std::endl;

    // Break the uri into segments
    uri.getPathSegments( pathStrs );

    // Check it this is a local request that the managment node should handle
    for( std::vector< HNRestPath >::iterator it = m_proxyPathList.begin(); it != m_proxyPathList.end(); it++ )
    {
        std::cout << "Check handler: " << it->getOpID() << std::endl;
        opData = it->checkForHandler( reqRR->getReqMsg().getMethod(), pathStrs );
        if( opData != NULL )
            return opData;
    }

    std::cout << "Warning: No handler found." << std::endl;

    // Should return default error handler instead?
    return NULL;
}

HNMD_RESULT_T
HNManagementDevice::generateJWT( std::string username, uint duration, std::string &token )
{
	time_t iat = time(NULL);
    int ret = 0;
    jwt_t *jwt = NULL;

    // Create a new jwt structure
    ret = jwt_new( &jwt );
    if( (ret != 0) || (jwt == NULL) ) 
    {
        fprintf( stderr, "invalid jwt\n" );
        return HNMD_RESULT_FAILURE;
    }

    // Add the username to the payload
    ret = jwt_add_grant( jwt, "username", username.c_str() );
    if( ret != 0 ) 
    {
        fprintf( stderr, "Add grant is invalid\n" );
        return HNMD_RESULT_FAILURE;
    }
    
    // Add the role to the payload
    ret = jwt_add_grant( jwt, "role", "ROLE_ADMIN" );
    if( ret != 0 ) 
    {
        fprintf( stderr, "Add grant is invalid\n" );
        return HNMD_RESULT_FAILURE;
    }

    // Add the issurer to the payload
    std::string iss = HNODE_MGMT_DEVTYPE;
    iss += "-";
    iss += m_instanceName;    
    iss += "-";
    iss += m_hnodeDev.getHNodeIDCRC32Str();
    ret = jwt_add_grant( jwt, "iss", iss.c_str() );
    if( ret != 0 ) 
    {
        fprintf( stderr, "Add grant is invalid\n" );
        return HNMD_RESULT_FAILURE;
    }

    // Add the time when token was issued.
    ret = jwt_add_grant_int( jwt, "iat", iat );
    if( ret != 0 ) 
    {
        fprintf( stderr, "Add grant is invalid\n" );
        return HNMD_RESULT_FAILURE;
    }

    // Add the experation time for the token.
    ret = jwt_add_grant_int( jwt, "exp", (iat + duration) );
    if( ret != 0 ) 
    {
        fprintf( stderr, "Add grant is invalid\n" );
        return HNMD_RESULT_FAILURE;
    }

    // Encode the token and sign via HMAC
    std::string skey = "TestSecretKeyStringForHMAC";
    ret = jwt_set_alg( jwt, JWT_ALG_HS256, (const unsigned char *) skey.c_str(), skey.size() );
    if( ret < 0 ) 
    {
        fprintf( stderr, "jwt incorrect algorithm\n" );
        return HNMD_RESULT_FAILURE;
    }

    char *out = jwt_dump_str( jwt, 1 );
    printf( "%s\n\n", out );
    free( out );

    out = jwt_encode_str( jwt );
    token = out;
    free( out );
    
    jwt_free( jwt );

    return HNMD_RESULT_SUCCESS;
}

void 
HNManagementDevice::handleLocalSCGIRequest( HNSCGIRR *reqRR, HNOperationData *opData )
{
    std::cout << "HNManagementDevice::handleLocalProxyRequest() - entry" << std::endl;
    std::cout << "  dispatchID: " << opData->getDispatchID() << std::endl;
    std::cout << "  opID: " << opData->getOpID() << std::endl;

    std::string opID = opData->getOpID();

    // POST /hnode2/mgmt/auth
    if( "createAuthToken" == opID )
    {
        std::cout << "== createAuthToken request ==" << std::endl;

        // Pull the content portion into the local buffer.
        reqRR->getReqMsg().readContentToLocal();
       
        std::string body;
        Poco::StreamCopier::copyToString( reqRR->getReqMsg().getLocalInputStream(), body );
        std::cout << body << std::endl;

        std::string jwtStr;
        HNMD_RESULT_T result = generateJWT( "admin", (12*60*60), jwtStr );
        switch( result )
        {
            case HNMD_RESULT_NOT_AUTHORIZED:
#if 0           
            reqRR->getRspMsg().configAsInternalServerError();
            return;
#endif              
            break;

            case HNMD_RESULT_SUCCESS:
            {
                reqRR->getRspMsg().setStatusCode(200);
                reqRR->getRspMsg().setReason("OK");
                reqRR->getRspMsg().setContentType("application/json");

                std::ostream &msg = reqRR->getRspMsg().useLocalContentSource();
                pjs::Object jsRoot;

                jsRoot.set( "username", "admin" );
                jsRoot.set( "accessToken", jwtStr );

                pjs::Array jsRoles;
                jsRoles.add("ROLE_ADMIN");
                jsRoot.set( "roles", jsRoles );

                // Render into a json string.
                try {
                    pjs::Stringifier::stringify( jsRoot, msg );
                } catch( ... ) {
                    // Send back not implemented
                    reqRR->getRspMsg().configAsInternalServerError();
                    return;
                }

                reqRR->getRspMsg().finalizeLocalContent();
                return;
            }
            break;

            default:
            break;
        }

        // Failure
        reqRR->getRspMsg().configAsInternalServerError();
        return;            
    }    
    // GET "/hnode2/mgmt/status"
    else if( "getStatus" == opID )
    {
        std::ostream &msg = reqRR->getRspMsg().useLocalContentSource();
        pjs::Object jsRoot;

        jsRoot.set( "state", "enable" );
        jsRoot.set( "test2", "00:00:00" );

        // Render into a json string.
        try {
            pjs::Stringifier::stringify( jsRoot, msg );
        } catch( ... ) {
            // Send back not implemented
            reqRR->getRspMsg().configAsInternalServerError();
            return;
        }

        reqRR->getRspMsg().finalizeLocalContent();
        reqRR->getRspMsg().setContentType("application/json");
        reqRR->getRspMsg().setStatusCode(200);
        reqRR->getRspMsg().setReason("OK");
        return;
    }
    else if( "getDeviceInventory" == opID )
    {
        std::ostream &msg = reqRR->getRspMsg().useLocalContentSource();
        pjs::Object jsRoot;
        pjs::Array jsOwnedArray;
        pjs::Array jsUnclaimedArray;
        pjs::Array jsOtherOwnerArray;
        pjs::Array jsUnavailableArray;

        std::vector< HNMDARecord > deviceList;
        m_arbiter.getDeviceListCopy( deviceList );


        for( std::vector< HNMDARecord >::iterator dit = deviceList.begin(); dit != deviceList.end(); dit++ )
        {
            pjs::Object jsDevice;
            pjs::Array  jsAddrArray;

            // Don't report the self device information here, 
            // do it via the local status request or similar.
            if( dit->getManagementState() == HNMDR_MGMT_STATE_SELF )
                continue;

            jsDevice.set( "name", dit->getName() );
            jsDevice.set( "hnodeID", dit->getHNodeIDStr() );
            jsDevice.set( "deviceType", dit->getDeviceType() );
            jsDevice.set( "deviceVersion", dit->getDeviceVersion() );
            jsDevice.set( "discID", dit->getDiscoveryID() );
            jsDevice.set( "crc32ID", dit->getCRC32ID() );
            jsDevice.set( "mgmtState", dit->getManagementStateStr() );
            std::vector< HNMDARAddress > addrList;
            dit->getAddressList( addrList );
            for( std::vector< HNMDARAddress >::iterator ait = addrList.begin(); ait != addrList.end(); ait++ )
            {
                pjs::Object jsAddress;

                jsAddress.set( "type", ait->getTypeAsStr() );
                jsAddress.set( "dnsName", ait->getDNSName() );
                jsAddress.set( "address", ait->getAddress() );
                jsAddress.set( "port", ait->getPort() );

                jsAddrArray.add( jsAddress );
            }

            jsDevice.set( "addresses", jsAddrArray );

            switch( dit->getOwnershipState() )
            {
                case HNMDR_OWNER_STATE_MINE: 
                    jsOwnedArray.add( jsDevice );
                break;

                case HNMDR_OWNER_STATE_AVAILABLE:
                    jsUnclaimedArray.add( jsDevice );
                break;

                // All other states are reported as unavailable
                case HNMDR_OWNER_STATE_OTHER:
                default:
                {
                    // Default to unavailable
                    jsUnavailableArray.add( jsDevice );
                }
            }
        }

        // Report the owned, unclaimed, other-owner, and unavailable arrays
        jsRoot.set( "ownedDevices", jsOwnedArray );
        jsRoot.set( "unclaimedDevices", jsUnclaimedArray );
        jsRoot.set( "unavailableDevices", jsUnavailableArray );

        // Render into a json string.
        try {
            pjs::Stringifier::stringify( jsRoot, msg );
        } catch( ... ) {
            // Send back not implemented
            reqRR->getRspMsg().configAsInternalServerError();
            return;
        }

        reqRR->getRspMsg().finalizeLocalContent();
        reqRR->getRspMsg().setContentType("application/json");
        reqRR->getRspMsg().setStatusCode(200);
        reqRR->getRspMsg().setReason("OK");
        return;
    }
    else if( "getDeviceMgmtStatus" == opID )
    {
        std::string devCRC32ID;

        if( opData->getParam( "devCRC32ID", devCRC32ID ) == true )
        {
            opData->responseSetStatusAndReason( HNR_HTTP_INTERNAL_SERVER_ERROR );
            opData->responseSend();
            return; 
        }

        std::cout << "=== Get Device Mgmt Status Request (id: " << devCRC32ID << ") ===" << std::endl;

        std::ostream &msg = reqRR->getRspMsg().useLocalContentSource();
        pjs::Object jsRoot;

        HNMDARecord device;
        if( m_arbiter.getDeviceCopy( devCRC32ID, device ) != HNMDL_RESULT_SUCCESS )
        {
            opData->responseSetStatusAndReason( HNR_HTTP_INTERNAL_SERVER_ERROR );
            opData->responseSend();
            return; 
        }

        jsRoot.set( "name", device.getName() );
        jsRoot.set( "hnodeID", device.getHNodeIDStr() );
        jsRoot.set( "deviceType", device.getDeviceType() );
        jsRoot.set( "deviceVersion", device.getDeviceVersion() );
        jsRoot.set( "discID", device.getDiscoveryID() );
        jsRoot.set( "crc32ID", device.getCRC32ID() );
        jsRoot.set( "mgmtState", device.getManagementStateStr() );

        pjs::Array  jsAddrArray;
        std::vector< HNMDARAddress > addrList;
        device.getAddressList( addrList );
        for( std::vector< HNMDARAddress >::iterator ait = addrList.begin(); ait != addrList.end(); ait++ )
        {
            pjs::Object jsAddress;

            jsAddress.set( "type", ait->getTypeAsStr() );
            jsAddress.set( "dnsName", ait->getDNSName() );
            jsAddress.set( "address", ait->getAddress() );
            jsAddress.set( "port", ait->getPort() );

            jsAddrArray.add( jsAddress );
        }

        jsRoot.set( "addresses", jsAddrArray );

        // Render into a json string.
        try {
            pjs::Stringifier::stringify( jsRoot, msg );
        } catch( ... ) {
            // Send back not implemented
            reqRR->getRspMsg().configAsInternalServerError();
            return;
        }

        reqRR->getRspMsg().finalizeLocalContent();
        reqRR->getRspMsg().setContentType("application/json");
        reqRR->getRspMsg().setStatusCode(200);
        reqRR->getRspMsg().setReason("OK");
        return;
    }
    else if( "postDeviceMgmtCommand" == opID )
    {
        std::string devCRC32ID;

        if( opData->getParam( "devCRC32ID", devCRC32ID ) == true )
        {
            reqRR->getRspMsg().configAsInternalServerError();
            return; 
        }

        std::cout << "=== Post Device Mgmt Command (id: " << devCRC32ID << ") ===" << std::endl;

        // Pull the content portion into the local buffer.
        reqRR->getReqMsg().readContentToLocal();

        std::istream *bodyStream = &reqRR->getReqMsg().getLocalInputStream();

        std::cout << "stream state: " << bodyStream->rdstate() << std::endl;
        std::cout << "stream 1char: " << bodyStream->peek() << std::endl;

        // Parse the command request, erroring if it isn't ok
        if( m_arbiter.setDeviceMgmtCmdFromJSON( devCRC32ID, bodyStream ) != HNMDL_RESULT_SUCCESS )
        {
            reqRR->getRspMsg().configAsInternalServerError();
            return;
        }

        // Kick off execution of the new command request
        if( m_arbiter.startDeviceMgmtCmd( devCRC32ID ) != HNMDL_RESULT_SUCCESS )
        {
            reqRR->getRspMsg().configAsInternalServerError();
            return; 
        }
        
        reqRR->getRspMsg().setStatusCode(200);
        reqRR->getRspMsg().setReason("OK");
        return;
    }

    // Send back not implemented
    reqRR->getRspMsg().configAsNotFound();
    return;
}

const std::string g_HNode2MgmtRest = R"(
{
  "openapi": "3.0.0",
  "info": {
    "description": "",
    "version": "1.0.0",
    "title": ""
  },
  "paths": {
      "/hnode2/mgmt/status": {
        "get": {
          "summary": "Get management node device status.",
          "operationId": "getStatus",
          "responses": {
            "200": {
              "description": "successful operation",
              "content": {
                "application/json": {
                  "schema": {
                    "type": "array"
                  }
                }
              }
            },
            "400": {
              "description": "Invalid status value"
            }
          }
        }
      }
  }
}
)";

const std::string g_HNode2ProxyMgmtAPI = R"(
{
  "openapi": "3.0.0",
  "info": {
    "description": "",
    "version": "1.0.0",
    "title": ""
  },
  "paths": {
      "/hnode2/mgmt/auth/signin": {
        "post": {
          "summary": "Request a new authentication token",
          "operationId": "createAuthToken",
          "responses": {
            "200": {
              "description": "successful operation",
              "content": {
                "application/json": {
                  "schema": {
                    "type": "object"
                  }
                }
              }
            },
            "400": {
              "description": "Invalid status value"
            }
          }            
        }
      },

      "/hnode2/mgmt/status": {
        "get": {
          "summary": "Get management node device status.",
          "operationId": "getStatus",
          "responses": {
            "200": {
              "description": "successful operation",
              "content": {
                "application/json": {
                  "schema": {
                    "type": "array"
                  }
                }
              }
            },
            "400": {
              "description": "Invalid status value"
            }
          }
        }
      },
      "/hnode2/mgmt/device-inventory": {
        "get": {
          "summary": "Get management node remote device inventory.",
          "operationId": "getDeviceInventory",
          "responses": {
            "200": {
              "description": "successful operation",
              "content": {
                "application/json": {
                  "schema": {
                    "type": "array"
                  }
                }
              }
            },
            "400": {
              "description": "Invalid status value"
            }
          }
        }
      },
      "/hnode2/mgmt/device-inventory/{devCRC32ID}": {
        "get": {
          "summary": "Get managment status for specific remote device.",
          "operationId": "getDeviceMgmtStatus",
          "responses": {
            "200": {
              "description": "successful operation",
              "content": {
                "application/json": {
                  "schema": {
                    "type": "array"
                  }
                }
              }
            },
            "400": {
              "description": "Invalid status value"
            }
          }
        }
      },
      "/hnode2/mgmt/device-inventory/{devCRC32ID}/command": {
        "post": {
          "summary": "Request a change to device management state.",
          "operationId": "postDeviceMgmtCommand",
          "responses": {
            "200": {
              "description": "successful operation",
              "content": {
                "application/json": {
                  "schema": {
                    "type": "array"
                  }
                }
              }
            },
            "400": {
              "description": "Invalid status value"
            }
          }
        }        
      }        
  }
}
)";
