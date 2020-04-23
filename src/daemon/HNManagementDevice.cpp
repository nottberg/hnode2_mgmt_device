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
    helpFormatter.setHeader("HNode2 Switch Daemon.");
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


    // Initialize for event loop
    epollFD = epoll_create1( 0 );
    if( epollFD == -1 )
    {
        //log.error( "ERROR: Failure to create epoll event loop: %s", strerror(errno) );
        return Application::EXIT_SOFTWARE;
    }

    // Buffer where events are returned 
    events = (struct epoll_event *) calloc( MAXEVENTS, sizeof event );

    // Open Unix named socket for requests
    openListenerSocket( HN_MGMTDAEMON_DEVICE_NAME, instanceName );

    //log.info( "Entering hnode2 switch daemon event loop" );

    // Start the HNode Device
    hnDevice.start();

    // Start the Managed Device Arbiter
    arbiter.start();

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
            if( acceptFD == events[i].data.fd )
	        {
                // New client connections
	            if( (events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (!(events[i].events & EPOLLIN)) )
	            {
                    /* An error has occured on this fd, or the socket is not ready for reading (why were we notified then?) */
                    syslog( LOG_ERR, "accept socket closed - restarting\n" );
                    close (events[i].data.fd);
	                continue;
	            }

                processNewClientConnections();
                continue;
            }
            else if( discoverFD == events[i].data.fd )
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
            else
            {
                // Client request
	            if( (events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (!(events[i].events & EPOLLIN)) )
	            {
                    // An error has occured on this fd, or the socket is not ready for reading (why were we notified then?)
                    closeClientConnection( events[i].data.fd );

	                continue;
	            }

                // Handle a request from a client.
                processClientRequest( events[i].data.fd );
            }
        }
    }

    avBrowser.shutdown();
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

HNMD_RESULT_T
HNManagementDevice::addSignalSocket( int sfd )
{
    signalFD = sfd;
    
    return addSocketToEPoll( signalFD );
}

HNMD_RESULT_T
HNManagementDevice::openListenerSocket( std::string deviceName, std::string instanceName )
{
    struct sockaddr_un addr;
    char str[512];

    // Clear address structure - UNIX domain addressing
    // addr.sun_path[0] cleared to 0 by memset() 
    memset( &addr, 0, sizeof(struct sockaddr_un) );  
    addr.sun_family = AF_UNIX;                     

    // Abstract socket with name @<deviceName>-<instanceName>
    sprintf( str, "hnode2-%s-%s", deviceName.c_str(), instanceName.c_str() );
    strncpy( &addr.sun_path[1], str, strlen(str) );

    acceptFD = socket( AF_UNIX, SOCK_SEQPACKET, 0 );
    if( acceptFD == -1 )
    {
        syslog( LOG_ERR, "Opening daemon listening socket failed (%s).", strerror(errno) );
        return HNMD_RESULT_FAILURE;
    }

    if( bind( acceptFD, (struct sockaddr *) &addr, sizeof( sa_family_t ) + strlen( str ) + 1 ) == -1 )
    {
        syslog( LOG_ERR, "Failed to bind socket to @%s (%s).", str, strerror(errno) );
        return HNMD_RESULT_FAILURE;
    }

    if( listen( acceptFD, 4 ) == -1 )
    {
        syslog( LOG_ERR, "Failed to listen on socket for @%s (%s).", str, strerror(errno) );
        return HNMD_RESULT_FAILURE;
    }

    return addSocketToEPoll( acceptFD );
}


HNMD_RESULT_T
HNManagementDevice::processNewClientConnections( )
{
    uint8_t buf[16];

    // There are pending connections on the listening socket.
    while( 1 )
    {
        struct sockaddr in_addr;
        socklen_t in_len;
        int infd;

        in_len = sizeof in_addr;
        infd = accept( acceptFD, &in_addr, &in_len );
        if( infd == -1 )
        {
            if( (errno == EAGAIN) || (errno == EWOULDBLOCK) )
            {
                // All requests processed
                break;
            }
            else
            {
                // Error while accepting
                syslog( LOG_ERR, "Failed to accept for @acrt5n1d_readings (%s).", strerror(errno) );
                return HNMD_RESULT_FAILURE;
            }
        }

        syslog( LOG_ERR, "Adding client - sfd: %d", infd );

        clientSet.insert( infd );

        addSocketToEPoll( infd );
    }

    return HNMD_RESULT_SUCCESS;
}

                    
HNMD_RESULT_T
HNManagementDevice::closeClientConnection( int clientFD )
{
    clientSet.erase( clientFD );

    removeSocketFromEPoll( clientFD );

    close( clientFD );

    syslog( LOG_ERR, "Closed client - sfd: %d", clientFD );

    return HNMD_RESULT_SUCCESS;
}

HNMD_RESULT_T
HNManagementDevice::processClientRequest( int cfd )
{
#if 0
    // One of the clients has sent us a message.
    HNSWDPacketDaemon packet;
    HNSWDP_RESULT_T   result;

    uint32_t          recvd = 0;

    // Note that a client request is being recieved.
    log.info( "Receiving client request from fd: %d", cfd );

    // Read the header portion of the packet
    result = packet.rcvHeader( cfd );
    if( result == HNSWDP_RESULT_NOPKT )
    {
        return HNMD_RESULT_SUCCESS;
    }
    else if( result != HNSWDP_RESULT_SUCCESS )
    {
        log.error( "ERROR: Failed while receiving packet header." );
        return HNMD_RESULT_FAILURE;
    } 

    log.info( "Pkt - type: %d  status: %d  msglen: %d", packet.getType(), packet.getResult(), packet.getMsgLen() );

    // Read any payload portion of the packet
    result = packet.rcvPayload( cfd );
    if( result != HNSWDP_RESULT_SUCCESS )
    {
        log.error( "ERROR: Failed while receiving packet payload." );
        return HNMD_RESULT_FAILURE;
    } 

    // Take any necessary action associated with the packet
    switch( packet.getType() )
    {
        // A request for status from the daemon
        case HNSWD_PTYPE_STATUS_REQ:
        {
            log.info( "Status request from client: %d", cfd );
            sendStatus = true;
        }
        break;

        // Request the daemon to reset itself.
        case HNSWD_PTYPE_RESET_REQ:
        {
            log.info( "Reset request from client: %d", cfd );

            // Start out with good health.
            // healthOK = true;

            // Reinitialize the underlying RTLSDR code.
            // demod.init();

            // Start reading time now.
            // gettimeofday( &lastReadingTS, NULL );

            sendStatus = true;
        }
        break;

        case HNSWD_PTYPE_HEALTH_REQ:
        {
            log.info( "Component Health request from client: %d", cfd );
            sendComponentHealthPacket( cfd );
        }
        break;

        // Request a manual sequence of switch activity.
        case HNSWD_PTYPE_USEQ_ADD_REQ:
        {
            HNSM_RESULT_T result;
            std::string msg;
            std::string error;
            struct tm newtime;
            time_t ltime;
 
            log.info( "Uniform sequence add request from client: %d", cfd );

            // Get the current time 
            ltime = time( &ltime );
            localtime_r( &ltime, &newtime );

            // Attempt to add the sequence
            packet.getMsg( msg );
            result = seqQueue.addUniformSequence( &newtime, msg, error );
            
            if( result != HNSM_RESULT_SUCCESS )
            {
                // Create the packet.
                HNSWDPacketDaemon opacket( HNSWD_PTYPE_USEQ_ADD_RSP, HNSWD_RCODE_FAILURE, error );

                // Send packet to requesting client
                opacket.sendAll( cfd );

                return HNMD_RESULT_SUCCESS;
            }
            
            seqQueue.debugPrint();

            // Create the packet.
            HNSWDPacketDaemon opacket( HNSWD_PTYPE_USEQ_ADD_RSP, HNSWD_RCODE_SUCCESS, error );

            // Send packet to requesting client
            opacket.sendAll( cfd );
        }
        break;

        case HNSWD_PTYPE_SEQ_CANCEL_REQ:
        {
            std::string msg;
            HNSM_RESULT_T result;
 
            log.info( "Cancel sequence request from client: %d", cfd );

            result = seqQueue.cancelSequences();
            
            if( result != HNSM_RESULT_SUCCESS )
            {
                std::string error;

                // Create the packet.
                HNSWDPacketDaemon opacket( HNSWD_PTYPE_SEQ_CANCEL_RSP, HNSWD_RCODE_FAILURE, error );

                // Send packet to requesting client
                opacket.sendAll( cfd );

                return HNMD_RESULT_SUCCESS;
            }
            
            seqQueue.debugPrint();

            // Create the packet.
            HNSWDPacketDaemon opacket( HNSWD_PTYPE_SEQ_CANCEL_RSP, HNSWD_RCODE_SUCCESS, msg );

            // Send packet to requesting client
            opacket.sendAll( cfd );
        }
        break;

        case HNSWD_PTYPE_SWINFO_REQ:
        {
            log.info( "Switch Info request from client: %d", cfd );
            sendSwitchInfoPacket( cfd );
        }
        break;

        case HNSWD_PTYPE_SCH_STATE_REQ:
        {
            pjs::Parser parser;
            std::string msg;
            std::string empty;
            std::string error;
            std::string newState;
            std::string inhDur;

            log.info( "Schedule State request from client: %d", cfd );

            // Get inbound request message
            packet.getMsg( msg );

            // Parse the json
            try
            {
                // Attempt to parse the json
                pdy::Var varRoot = parser.parse( msg );

                // Get a pointer to the root object
                pjs::Object::Ptr jsRoot = varRoot.extract< pjs::Object::Ptr >();

                newState = jsRoot->optValue( "state", empty );
                inhDur   = jsRoot->optValue( "inhibitDuration", empty );

                if( newState.empty() || inhDur.empty() )
                {
                    log.error( "ERROR: Schedule State request malformed." );

                    // Send error packet.
                    HNSWDPacketDaemon opacket( HNSWD_PTYPE_SCH_STATE_RSP, HNSWD_RCODE_FAILURE, error );
                    opacket.sendAll( cfd );

                    return HNMD_RESULT_SUCCESS;
                }
            }
            catch( Poco::Exception ex )
            {
                log.error( "ERROR: Schedule State request malformed - parse failure: %s", ex.displayText().c_str() );

                // Send error packet.
                HNSWDPacketDaemon opacket( HNSWD_PTYPE_SCH_STATE_RSP, HNSWD_RCODE_FAILURE, error );
                opacket.sendAll( cfd );

                return HNMD_RESULT_SUCCESS;
            }

            if( "disable" == newState )
                schMat.setStateDisabled();         
            else if( "enable" == newState )
                schMat.setStateEnabled();
            else if( "inhibit" == newState )
            {
                struct tm newtime;
                time_t ltime;
 
                // Get the current time 
                ltime = time( &ltime );
                localtime_r( &ltime, &newtime );

                schMat.setStateInhibited( &newtime, inhDur );
            }
            else
            {
                log.error( "ERROR: Schedule State request - request state is not supported: %s", newState.c_str() );

                // Send error packet.
                HNSWDPacketDaemon opacket( HNSWD_PTYPE_SCH_STATE_RSP, HNSWD_RCODE_FAILURE, error );
                opacket.sendAll( cfd );

                return HNMD_RESULT_SUCCESS;
            }

            // Send success response
            HNSWDPacketDaemon opacket( HNSWD_PTYPE_SCH_STATE_RSP, HNSWD_RCODE_SUCCESS, empty );
            opacket.sendAll( cfd );

            return HNMD_RESULT_SUCCESS;
        }
        break;

        // Unknown packet
        default:
        {
            log.warn( "Warning: RX of unsupported packet, discarding - type: %d", packet.getType() );
        }
        break;
    }
#endif
    return HNMD_RESULT_SUCCESS;
}



