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

#include "Poco/Thread.h"
#include "Poco/Runnable.h"

#include "HNSCGISink.h"

#define MAXEVENTS  8

// Helper class for running HNSCGISink  
// proxy loop as an independent thread
class HNSCGIRunner : public Poco::Runnable
{
    private:
        Poco::Thread m_thread;
        HNSCGISink   *m_abObj;

    public:  
        HNSCGIRunner( HNSCGISink *value )
        {
            m_abObj = value;
        }

        void startThread()
        {
            m_thread.start( *this );
        }

        void killThread()
        {
            m_abObj->killSCGILoop();
            m_thread.join();
        }

        virtual void run()
        {
            m_abObj->runSCGILoop();
        }

};

HNSCGISink::HNSCGISink()
{
    m_instanceName = "default";
    m_runMonitor = false;
    m_thelp = NULL;
}

HNSCGISink::~HNSCGISink()
{

}

#if 0
HNMDL_RESULT_T 
HNSCGISink::notifyDiscoverAdd( HNMDARecord &record )
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
HNSCGISink::notifyDiscoverRemove( HNMDARecord &record )
{


}
#endif

void 
HNSCGISink::debugPrint()
{
    printf( "=== SCGI Sink ===\n" );

#if 0
    for( std::map< std::string, HNMDARecord >::iterator it = mdrMap.begin(); it != mdrMap.end(); it++ )
    {
        it->second.debugPrint( 2 );
    }
#endif
}



void
HNSCGISink::start( std::string instance )
{
    int error;

    std::cout << "HNSCGISink::start()" << std::endl;

    m_instanceName = instance;

    // Allocate the thread helper
    m_thelp = new HNSCGIRunner( this );
    if( !m_thelp )
    {
        //cleanup();
        return;
    }

    m_runMonitor = true;

    // Start up the event loop
    ( (HNSCGIRunner*) m_thelp )->startThread();
}

void 
HNSCGISink::runSCGILoop()
{
    std::cout << "HNSCGISink::runMonitoringLoop()" << std::endl;

    // Initialize for event loop
    m_epollFD = epoll_create1( 0 );
    if( m_epollFD == -1 )
    {
        //log.error( "ERROR: Failure to create epoll event loop: %s", strerror(errno) );
        return;
    }

    // Buffer where events are returned 
    m_events = (struct epoll_event *) calloc( MAXEVENTS, sizeof m_event );

    // Open Unix named socket for requests
    openSCGISocket();

    // The listen loop 
    while( m_runMonitor == true )
    {
        int n;
        int i;
        struct tm newtime;
        time_t ltime;

        // Check for events
        n = epoll_wait( m_epollFD, m_events, MAXEVENTS, 2000 );

        std::cout << "HNSCGISink::monitor wakeup" << std::endl;

        // EPoll error
        if( n < 0 )
        {
            // If we've been interrupted by an incoming signal, continue, wait for socket indication
            if( errno == EINTR )
                continue;

            // Handle error
            //log.error( "ERROR: Failure report by epoll event loop: %s", strerror( errno ) );
            return;
        }
 
        // If it was a timeout then continue to next loop
        // skip socket related checks.
        if( n == 0 )
            continue;

        // Socket event
        for( i = 0; i < n; i++ )
	    {
            if( m_acceptFD == m_events[i].data.fd )
	        {
                // New client connections
	            if( (m_events[i].events & EPOLLERR) || (m_events[i].events & EPOLLHUP) || (!(m_events[i].events & EPOLLIN)) )
	            {
                    /* An error has occured on this fd, or the socket is not ready for reading (why were we notified then?) */
                    syslog( LOG_ERR, "accept socket closed - restarting\n" );
                    close (m_events[i].data.fd);
	                continue;
	            }

                processNewClientConnections();
                continue;
            }
            else
            {
                // Client request
	            if( (m_events[i].events & EPOLLERR) || (m_events[i].events & EPOLLHUP) || (!(m_events[i].events & EPOLLIN)) )
	            {
                    // An error has occured on this fd, or the socket is not ready for reading (why were we notified then?)
                    closeClientConnection( m_events[i].data.fd );

	                continue;
	            }

                // Handle a request from a client.
                processClientRequest( m_events[i].data.fd );
            }
        }
    }

    std::cout << "HNSCGISink::monitor exit" << std::endl;
}

void
HNSCGISink::shutdown()
{
    if( !m_thelp )
    {
        //cleanup();
        return;
    }

    // End the event loop
    ( (HNSCGIRunner*) m_thelp )->killThread();

    delete ( (HNSCGIRunner*) m_thelp );
    m_thelp = NULL;
}

void 
HNSCGISink::killSCGILoop()
{
    m_runMonitor = false;    
}

HNSS_RESULT_T
HNSCGISink::addSocketToEPoll( int sfd )
{
    int flags, s;

    flags = fcntl( sfd, F_GETFL, 0 );
    if( flags == -1 )
    {
        syslog( LOG_ERR, "Failed to get socket flags: %s", strerror(errno) );
        return HNSS_RESULT_FAILURE;
    }

    flags |= O_NONBLOCK;
    s = fcntl( sfd, F_SETFL, flags );
    if( s == -1 )
    {
        syslog( LOG_ERR, "Failed to set socket flags: %s", strerror(errno) );
        return HNSS_RESULT_FAILURE; 
    }

    m_event.data.fd = sfd;
    m_event.events = EPOLLIN | EPOLLET;
    s = epoll_ctl( m_epollFD, EPOLL_CTL_ADD, sfd, &m_event );
    if( s == -1 )
    {
        return HNSS_RESULT_FAILURE;
    }

    return HNSS_RESULT_SUCCESS;
}

HNSS_RESULT_T
HNSCGISink::removeSocketFromEPoll( int sfd )
{
    int s;

    s = epoll_ctl( m_epollFD, EPOLL_CTL_DEL, sfd, NULL );
    if( s == -1 )
    {
        return HNSS_RESULT_FAILURE;
    }

    return HNSS_RESULT_SUCCESS;
}

HNSS_RESULT_T
HNSCGISink::openSCGISocket()
{
    struct sockaddr_un addr;
    char str[512];

    // Clear address structure - UNIX domain addressing
    // addr.sun_path[0] cleared to 0 by memset() 
    memset( &addr, 0, sizeof(struct sockaddr_un) );  
    addr.sun_family = AF_UNIX;                     

    // Socket with name /tmp/hnode2-scgi-<instanceName>.sock
    sprintf( str, "/tmp/hnode2-scgi-%s.sock", m_instanceName.c_str() );
    strncpy( &addr.sun_path[0], str, strlen(str) );

    m_acceptFD = socket( AF_UNIX, SOCK_STREAM, 0 );
    if( m_acceptFD == -1 )
    {
        syslog( LOG_ERR, "Opening daemon listening socket failed (%s).", strerror(errno) );
        return HNSS_RESULT_FAILURE;
    }

    if( bind( m_acceptFD, (struct sockaddr *) &addr, sizeof( sa_family_t ) + strlen( str ) + 1 ) == -1 )
    {
        syslog( LOG_ERR, "Failed to bind socket to @%s (%s).", str, strerror(errno) );
        return HNSS_RESULT_FAILURE;
    }

    if( listen( m_acceptFD, 4 ) == -1 )
    {
        syslog( LOG_ERR, "Failed to listen on socket for @%s (%s).", str, strerror(errno) );
        return HNSS_RESULT_FAILURE;
    }

    return addSocketToEPoll( m_acceptFD );
}


HNSS_RESULT_T
HNSCGISink::processNewClientConnections( )
{
    uint8_t buf[16];

    // There are pending connections on the listening socket.
    while( 1 )
    {
        struct sockaddr in_addr;
        socklen_t in_len;
        int infd;

        in_len = sizeof in_addr;
        infd = accept( m_acceptFD, &in_addr, &in_len );
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
                return HNSS_RESULT_FAILURE;
            }
        }

        syslog( LOG_ERR, "Adding client - sfd: %d", infd );

        m_clientSet.insert( infd );

        addSocketToEPoll( infd );
    }

    return HNSS_RESULT_SUCCESS;
}

                    
HNSS_RESULT_T
HNSCGISink::closeClientConnection( int clientFD )
{
    m_clientSet.erase( clientFD );

    removeSocketFromEPoll( clientFD );

    close( clientFD );

    syslog( LOG_ERR, "Closed client - sfd: %d", clientFD );

    return HNSS_RESULT_SUCCESS;
}

HNSS_RESULT_T
HNSCGISink::processClientRequest( int cfd )
{
    syslog( LOG_ERR, "Process client request - sfd: %d", cfd );
    
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
        return HNSS_RESULT_SUCCESS;
    }
    else if( result != HNSWDP_RESULT_SUCCESS )
    {
        log.error( "ERROR: Failed while receiving packet header." );
        return HNSS_RESULT_FAILURE;
    } 

    log.info( "Pkt - type: %d  status: %d  msglen: %d", packet.getType(), packet.getResult(), packet.getMsgLen() );

    // Read any payload portion of the packet
    result = packet.rcvPayload( cfd );
    if( result != HNSWDP_RESULT_SUCCESS )
    {
        log.error( "ERROR: Failed while receiving packet payload." );
        return HNSS_RESULT_FAILURE;
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

                return HNSS_RESULT_SUCCESS;
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

                return HNSS_RESULT_SUCCESS;
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

                    return HNSS_RESULT_SUCCESS;
                }
            }
            catch( Poco::Exception ex )
            {
                log.error( "ERROR: Schedule State request malformed - parse failure: %s", ex.displayText().c_str() );

                // Send error packet.
                HNSWDPacketDaemon opacket( HNSWD_PTYPE_SCH_STATE_RSP, HNSWD_RCODE_FAILURE, error );
                opacket.sendAll( cfd );

                return HNSS_RESULT_SUCCESS;
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

                return HNSS_RESULT_SUCCESS;
            }

            // Send success response
            HNSWDPacketDaemon opacket( HNSWD_PTYPE_SCH_STATE_RSP, HNSWD_RCODE_SUCCESS, empty );
            opacket.sendAll( cfd );

            return HNSS_RESULT_SUCCESS;
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
    return HNSS_RESULT_SUCCESS;
}



