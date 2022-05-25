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

#include <hnode2/HNodeDevice.h>

#include "HNManagementDevice.h"

using namespace Poco::Util;

namespace pjs = Poco::JSON;
namespace pdy = Poco::Dynamic;

// Forward declaration
extern const std::string g_HNode2MgmtRest;

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

    HNDEndpoint hndEP;

    hndEP.setDispatch( "hnode2Mgmt", this );
    hndEP.setOpenAPIJson( g_HNode2MgmtRest ); 

    hnDevice.addEndpoint( hndEP );

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

    // Hook the browser into the event loop
    int discoverFD = avBrowser.getEventQueue().getEventFD();
   
    if( addSocketToEPoll( discoverFD ) !=  HNMD_RESULT_SUCCESS )
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

                            //void setBaseIPv4URL( std::string value );
                            //void setBaseIPv6URL( std::string value );
                            //void setBaseSelfURL( std::string value );

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
        }
    }

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
        syslog( LOG_ERR, "Failed to get socket flags: %s", strerror(errno) );
        return HNMD_RESULT_FAILURE;
    }

    flags |= O_NONBLOCK;
    s = fcntl( sfd, F_SETFL, flags );
    if( s == -1 )
    {
        syslog( LOG_ERR, "Failed to set socket flags: %s", strerror(errno) );
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

void 
HNManagementDevice::dispatchProxyRequest( HNProxyRequest *request )
{

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
      },
  }
}
)";


