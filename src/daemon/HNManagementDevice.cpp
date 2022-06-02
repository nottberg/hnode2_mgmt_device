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

#include "Poco/Util/ServerApplication.h"
#include "Poco/Util/Option.h"
#include "Poco/Util/OptionSet.h"
#include "Poco/Util/HelpFormatter.h"
#include "Poco/Checksum.h"
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include "Poco/URI.h"

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

#define HN_MGMTDAEMON_DEVICE_NAME   "hnode2-management-device"
#define HN_MGMTDAEMON_DEF_INSTANCE  "default"

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
        instanceName = _instance;
    else
        instanceName = HN_MGMTDAEMON_DEF_INSTANCE;

    // Check if we are running as a daemon
    if( config().getBool("application.runAsDaemon", false ) )
    {
        // Configure where logging should end up
        //log.setDaemon( true );
    }

    // Enable debug logging if requested
    if( _debugLogging == true )
    {
       //log.setLevelLimit( HNDL_LOG_LEVEL_ALL );
       //log.debug("Debug logging has been enabled.");
    }

    // Indicate startup
    //log.info( "Starting hnode2 switch daemon init" );

    // Setup HNode Device
    HNodeDevice hnDevice( HN_MGMTDAEMON_DEVICE_NAME, instanceName );

    hnDevice.setName("mg1");
    hnDevice.setPort(8400);

    HNDEndpoint hndEP;

    hndEP.setDispatch( "hnode2Mgmt", this );
    hndEP.setOpenAPIJson( g_HNode2MgmtRest ); 

    hnDevice.addEndpoint( hndEP );
 
    // Setup the decoder for proxy requests that will be handled locally.
    registerProxyEndpointsFromOpenAPI( g_HNode2ProxyMgmtAPI );

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

    // Start the HNode Device
    hnDevice.start();

    // Start the Managed Device Arbiter
    arbiter.start();

    // Start processing requests from the browser via SCGI
    reqsink.start( instanceName );

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

                            HNMDL_RESULT_T result = arbiter.notifyDiscoverAdd( notifyRec );
                            if( result != HNMDL_RESULT_SUCCESS )
                            {
                                // Note error
                            }
                        }
                        break;

                        case HNAB_EVTYPE_REMOVE:
                        break;
                    }

                    arbiter.debugPrint();

                    avBrowser.getEventQueue().releaseRecord( event );
                }
            }
            else if( scgiQFD == events[i].data.fd )
            {
                while( m_scgiRequestQueue.getPostedCnt() )
                {
                    HNProxyHTTPReqRsp *proxyRR = (HNProxyHTTPReqRsp *) m_scgiRequestQueue.aquireRecord();

                    std::cout << "HNManagementDevice::Received proxy request" << std::endl;

                    HNProxyTicket *proxyTicket = checkForProxyRequest( proxyRR );

                    if( proxyTicket != NULL )
                    {
                        std::cout << "Proxy request to device: " << std::endl;
                        m_proxySeq.getRequestQueue()->postRecord( proxyTicket );
                        continue;
                    }

                    HNOperationData *opData = mapProxyRequest( proxyRR );

                    if( opData == NULL )
                    {
                        proxyRR->getResponse().configAsNotFound();
                        reqsink.getProxyResponseQueue()->postRecord( proxyRR );
                    }
                    else
                    {
                        std::cout << "Local management device request: " << opData->getOpID() << std::endl;
                        handleLocalSCGIRequest( proxyRR, opData );
                        reqsink.getProxyResponseQueue()->postRecord( proxyRR );
                    }

                    delete opData;
                }
            }
            else if( proxyQFD == events[i].data.fd )
            {
                while( m_proxyResponseQueue.getPostedCnt() )
                {
                    HNProxyTicket *proxyTicket = (HNProxyTicket *) m_proxyResponseQueue.aquireRecord();

                    std::cout << "HNManagementDevice::Received proxy response" << std::endl;

                    HNProxyHTTPReqRsp *proxyRR = proxyTicket->getRR();

                    delete proxyTicket;

                    reqsink.getProxyResponseQueue()->postRecord( proxyRR );
                }
            }            
        }
    }

    m_proxySeq.shutdown();
    avBrowser.shutdown();
    reqsink.shutdown();
    arbiter.shutdown();
    //hnDevice.shutdown();

    waitForTerminationRequest();

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

    //std::cout << "Start Action - client: " << action.getType() << "  thread: " << std::this_thread::get_id() << std::endl;

    // Submit the action and block for response
    //m_actionQueue.postAndWait( &action );

    //std::cout << "Finish Action - client" << "  thread: " << std::this_thread::get_id() << std::endl;

#if 0
    // Determine what happened
    switch( action.getStatus() )
    {
        case HNRW_RESULT_SUCCESS:
        {
            std::string cType;
            std::string objID;


            // See if response content should be generated
            if( action.hasRspContent( cType ) )
            {
                // Set response content type
                opData->responseSetChunkedTransferEncoding( true );
                opData->responseSetContentType( cType );

                // Render any response content
                std::ostream& ostr = opData->responseSend();
            
                if( action.generateRspContent( ostr ) == true )
                {
                    opData->responseSetStatusAndReason( HNR_HTTP_INTERNAL_SERVER_ERROR );
                    opData->responseSend();
                    return;
                }
            }

            // Check if a new object was created.
            if( action.hasNewObject( objID ) )
            {
                // Object was created return info
                opData->responseSetCreated( objID );
                opData->responseSetStatusAndReason( HNR_HTTP_CREATED );
            }
            else
            {
#endif
                // Request was successful
                opData->responseSetStatusAndReason( HNR_HTTP_OK );
#if 0
            }
        }
        break;

        case HNRW_RESULT_FAILURE:
            opData->responseSetStatusAndReason( HNR_HTTP_INTERNAL_SERVER_ERROR );
        break;

        case HNRW_RESULT_TIMEOUT:
            opData->responseSetStatusAndReason( HNR_HTTP_INTERNAL_SERVER_ERROR );
        break;
    }
#endif
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
HNManagementDevice::checkForProxyRequest( HNProxyHTTPReqRsp *reqRR )
{
    std::vector< std::string > pathStrs;
    Poco::URI uri( reqRR->getRequest().getURI() );

    std::cout << "checkForProxyRequest - method: " << reqRR->getRequest().getMethod() << std::endl;
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
    HNMDL_RESULT_T result = arbiter.lookupConnectionInfo( crc32ID, HMDAR_ADDRTYPE_IPV4, dcInfo );
    if( result != HNMDL_RESULT_SUCCESS )
    {
        return NULL;
    }

    // Allocate and fillout a ProxyTicket for return.
    HNProxyTicket *rtnTicket = new HNProxyTicket( reqRR );

    rtnTicket->setCRC32ID( crc32ID );
    rtnTicket->setAddress( dcInfo.getAddress() );
    rtnTicket->setPort( dcInfo.getPort() );
    // rtnTicket->setProxyPrefix();

    return rtnTicket;
}

HNOperationData*
HNManagementDevice::mapProxyRequest( HNProxyHTTPReqRsp *reqRR )
{
    HNOperationData *opData = NULL;
    std::vector< std::string > pathStrs;

    Poco::URI uri( reqRR->getRequest().getURI() );

    std::cout << "mapProxyRequest - method: " << reqRR->getRequest().getMethod() << std::endl;
    std::cout << "mapProxyRequest - URI: " << uri.toString() << std::endl;

    // Break the uri into segments
    uri.getPathSegments( pathStrs );

    // Check it this is a local request that the managment node should handle
    for( std::vector< HNRestPath >::iterator it = m_proxyPathList.begin(); it != m_proxyPathList.end(); it++ )
    {
        std::cout << "Check handler: " << it->getOpID() << std::endl;
        opData = it->checkForHandler( reqRR->getRequest().getMethod(), pathStrs );
        if( opData != NULL )
            return opData;
    }

    // Should return default error handler instead?
    return NULL;
}

void 
HNManagementDevice::handleLocalSCGIRequest( HNProxyHTTPReqRsp *reqRR, HNOperationData *opData )
{
    //HNIDActionRequest action;

    std::cout << "HNManagementDevice::handleLocalProxyRequest() - entry" << std::endl;
    std::cout << "  dispatchID: " << opData->getDispatchID() << std::endl;
    std::cout << "  opID: " << opData->getOpID() << std::endl;

    std::string opID = opData->getOpID();

    // GET "/hnode2/mgmt/status"
    if( "getStatus" == opID )
    {
        std::ostream &msg = reqRR->getResponse().useLocalContentSource();
        pjs::Object jsRoot;

        jsRoot.set( "state", "enable" );
        jsRoot.set( "test2", "00:00:00" );

        // Render into a json string.
        try {
            pjs::Stringifier::stringify( jsRoot, msg );
        } catch( ... ) {
            // Send back not implemented
            reqRR->getResponse().configAsInternalServerError();
            return;
        }

        reqRR->getResponse().finalizeLocalContent();
        reqRR->getResponse().setContentType("application/json");
        reqRR->getResponse().setStatusCode(200);
        reqRR->getResponse().setReason("OK");
        return;
    }
    else if( "getDeviceInventory" == opID )
    {
        std::ostream &msg = reqRR->getResponse().useLocalContentSource();
        pjs::Object jsRoot;
        pjs::Array jsDevArray;

        std::vector< HNMDARecord > deviceList;
        arbiter.getDeviceListCopy( deviceList );
        for( std::vector< HNMDARecord >::iterator dit = deviceList.begin(); dit != deviceList.end(); dit++ )
        {
            pjs::Object jsDevice;
            pjs::Array  jsAddrArray;

            jsDevice.set( "name", dit->getName() );
            jsDevice.set( "hnodeID", dit->getHNodeIDStr() );
            jsDevice.set( "deviceType", dit->getDeviceType() );
            jsDevice.set( "deviceVersion", dit->getDeviceVersion() );
            jsDevice.set( "discID", dit->getDiscoveryID() );
            jsDevice.set( "crc32ID", dit->getCRC32ID() );

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

            jsDevArray.add( jsDevice );
        }

        jsRoot.set( "devices", jsDevArray );

        // Render into a json string.
        try {
            pjs::Stringifier::stringify( jsRoot, msg );
        } catch( ... ) {
            // Send back not implemented
            reqRR->getResponse().configAsInternalServerError();
            return;
        }

        reqRR->getResponse().finalizeLocalContent();
        reqRR->getResponse().setContentType("application/json");
        reqRR->getResponse().setStatusCode(200);
        reqRR->getResponse().setReason("OK");
        return;
    }

    // Send back not implemented
    reqRR->getResponse().configAsNotFound();
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
      }      
  }
}
)";
