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

#include "Poco/Thread.h"
#include "Poco/Runnable.h"

#include "HNSCGISink.h"

#define MAXEVENTS  8

HNSCGISinkClient::HNSCGISinkClient( uint fd, HNSCGISink *parent )
: m_curRR( fd ), m_ofilebuf( fd, (std::ios::out|std::ios::binary) ), m_ifilebuf( fd, (std::ios::in|std::ios::binary) ), m_ostream( &m_ofilebuf ), m_istream( &m_ifilebuf )
{
    m_fd      = fd;

    //__gnu_cxx::stdio_filebuf<char> filebuf( m_fd, std::ios::in ); // 1
    //std:fstream m_fstream (&filebuf); // 2

    m_parent  = parent;
    m_rxState = HNSCGI_SS_IDLE;

    m_curRR.getRequest().setContentSource( this );
    m_curRR.getResponse().setContentSink( this );
}

HNSCGISinkClient::~HNSCGISinkClient()
{

}       

void
HNSCGISinkClient::setRxParseState( HNSC_SS_T newState )
{
    printf( "setRxParseState - newState: %u\n", newState );

    //m_rxQueue.resetParseState();
    m_rxState = newState;
}

HNSS_RESULT_T 
HNSCGISinkClient::readNetStrStart()
{
    char c;

    while( true )
    {
        // Attempt to read a character
        ssize_t bytesRead = read( m_fd, &c, 1 );

        // Check for error case
        if( bytesRead < 0 )
            return HNSS_RESULT_PARSE_ERR;

        // If a character is not available then return to waiting.
        if( bytesRead == 0 )
            return HNSS_RESULT_PARSE_WAIT;

        // If the character is a quote, discard it.
        if( c == '"' )
            continue;

        // If the character is a colon, then
        // convert the partial string to a number 
        // and move on.
        if( c == ':' )
        {
            // Convert number string to an integer
            m_expHdrLen = strtol( m_partialStr.c_str(), NULL, 0 );
        
            // Clear the partial string
            m_partialStr.clear();

            // Move to next parsing phase.
            return HNSS_RESULT_PARSE_COMPLETE;
        }

        // Append the character to a string
        m_partialStr += c;
    }

    return HNSS_RESULT_FAILURE;
}

HNSS_RESULT_T 
HNSCGISinkClient::fillRequestHeaderBuffer()
{
    char *bufPtr = (m_headerBuf + m_rcvHdrLen);
    uint bytesLeft = (m_expHdrLen - m_rcvHdrLen);

    // Attempt to read a the rest of the header buffer data.
    ssize_t bytesRead = read( m_fd, bufPtr, bytesLeft );

    std::cout << "fillRequestHeaderBuffer - bytesLeft: " << bytesLeft << "  bytesRead: " << bytesRead << "  totalRead: " << m_rcvHdrLen << "  totalExp: " << m_expHdrLen << std::endl;

    // Check for error case
    if( bytesRead < 0 )
        return HNSS_RESULT_PARSE_ERR;

    // Account for bytes just read.
    m_rcvHdrLen += bytesRead;

    // Check if reading is complete.
    if( m_rcvHdrLen == m_expHdrLen )
    {
        std::cout << "fillRequestHeaderBuffer - complete" << std::endl;
        return HNSS_RESULT_PARSE_COMPLETE;
    }

    // Still need to read more data
    return HNSS_RESULT_PARSE_WAIT;
}

HNSS_RESULT_T 
HNSCGISinkClient::extractHeaderPairsFromBuffer()
{
    std::string curHdrName;
    std::string curHdrValue;

    char *bufPtr = m_headerBuf;
    char *endPtr = (m_headerBuf + m_rcvHdrLen );
    
    bool parsingName = true;
    for( ;bufPtr != endPtr; bufPtr++ )
    {
        // Get the current character
        char c = *bufPtr;

        // Ignore quote characters
        if( c == '"' )
            continue;
        
        // If we encounter a null then that is the end of the current string.
        if( c == '\0' )
        {
            // Check what value was being collected
            if( parsingName == true )
            {
                // Finished with name, switch to collecting value;
                parsingName = false;
            }
            else
            {
                // Finished with header and value.  Commit the strings to the header map
                m_curRR.getRequest().addSCGIRequestHeader( curHdrName, curHdrValue );

                // Clear the collected strings.
                curHdrName.clear();
                curHdrValue.clear();

                // Start with name again
                parsingName = true;
            }

            // Next iteration
            continue;
        }

        // Add this character to the appropriate string
        if( parsingName == true )
            curHdrName += c;
        else
            curHdrValue += c;
    }

    return HNSS_RESULT_PARSE_COMPLETE;
}

HNSS_RESULT_T 
HNSCGISinkClient::consumeNetStrComma()
{
    char c;

    // Attempt to read a character
    ssize_t bytesRead = read( m_fd, &c, 1 );

    // Check for error case
    if( bytesRead < 0 )
        return HNSS_RESULT_PARSE_ERR;

    // If a character is not available then return to waiting.
    if( bytesRead == 0 )
        return HNSS_RESULT_PARSE_WAIT;

    // Check that the character is a comma, otherwise error
    if( c == ',' )
        return HNSS_RESULT_PARSE_COMPLETE;

    return HNSS_RESULT_PARSE_ERR;
}

HNSS_RESULT_T
HNSCGISinkClient::readRequestHeaders()
{
    HNSS_RESULT_T result = HNSS_RESULT_PARSE_ERR;

    //printf( "rxNextParse - %u\n", m_rxState );
    
    // Handle the data
    switch( m_rxState )
    {
        // Start of parsing a new request
        case HNSCGI_SS_IDLE:
        {   
            m_expHdrLen = 0;
            m_rcvHdrLen = 0;
                        
            setRxParseState( HNSCGI_SS_HDR_NSTR_LEN );
            return HNSS_RESULT_PARSE_CONTINUE;             
        }
        break;
    
        // Get the length of the header netstring
        case HNSCGI_SS_HDR_NSTR_LEN:
        {
            result = readNetStrStart();
            
            switch( result )
            {
                case HNSS_RESULT_PARSE_COMPLETE:
                    printf( "HDR_NSTR Len: %u\n", m_expHdrLen );
                    
                    m_headerBuf  = (char *) malloc( m_expHdrLen );
                    m_rcvHdrLen = 0;

                    setRxParseState( HNSCGI_SS_HDR_ACCUMULATE );
                    return HNSS_RESULT_PARSE_CONTINUE; 
                break;
                
                case HNSS_RESULT_PARSE_WAIT:
                break;
                
                case HNSS_RESULT_PARSE_ERR:
                    setRxParseState( HNSCGI_SS_ERROR ); 
                    return HNSS_RESULT_PARSE_ERR;
                break;
            }            
        }
        break;
        
        // Read the header data into the allocated buffer
        case HNSCGI_SS_HDR_ACCUMULATE:
            result = fillRequestHeaderBuffer();

            switch( result )
            {
                case HNSS_RESULT_PARSE_COMPLETE:
                    setRxParseState( HNSCGI_SS_HDR_EXTRACT_PAIRS );
                    return HNSS_RESULT_PARSE_CONTINUE; 
                break;
                
                case HNSS_RESULT_PARSE_WAIT:
                break;
                
                case HNSS_RESULT_PARSE_ERR:
                    setRxParseState( HNSCGI_SS_ERROR ); 
                    return HNSS_RESULT_PARSE_ERR;
                break;
            }            
        break;

        // Extract the header and value pairs from the buffer
        case HNSCGI_SS_HDR_EXTRACT_PAIRS:
        {
            result = extractHeaderPairsFromBuffer();
    
            switch( result )
            {
                case HNSS_RESULT_PARSE_COMPLETE:
                    free( m_headerBuf );
                    m_headerBuf = NULL;
                    m_rcvHdrLen = 0;

                    setRxParseState( HNSCGI_SS_HDR_NSTR_COMMA );
                    return HNSS_RESULT_PARSE_CONTINUE; 
                break;
                
                case HNSS_RESULT_PARSE_ERR:
                    setRxParseState( HNSCGI_SS_ERROR ); 
                    return HNSS_RESULT_PARSE_ERR;
                break;
            }            
            
        }
        break;
        
        // Find and strip off the header netstring trailing comma
        case HNSCGI_SS_HDR_NSTR_COMMA:
        {
            result = consumeNetStrComma();
            
            switch( result )
            {
                case HNSS_RESULT_PARSE_COMPLETE:
                    //printf( "HDR_NSTR End:\n");
                    m_curRR.getRequest().setHeaderDone( true );

                    setRxParseState( HNSCGI_SS_HDR_DONE );
                    
                    return HNSS_RESULT_REQUEST_READY;
                break;
                
                case HNSS_RESULT_PARSE_WAIT:
                break;
                
                case HNSS_RESULT_PARSE_ERR:
                    setRxParseState( HNSCGI_SS_ERROR ); 
                    return HNSS_RESULT_PARSE_ERR;
                break;
            }            
        }        
        break;
                
        // An error occurred during processing.
        case HNSCGI_SS_ERROR:
        default:
        break;

    }

    return HNSS_RESULT_SUCCESS;
}

HNSS_RESULT_T 
HNSCGISinkClient::recvData()
{
    HNSS_RESULT_T result;
    
    // Ignore while waiting for payload 
    // processing.
    if( m_rxState == HNSCGI_SS_HDR_DONE )
        return HNSS_RESULT_SUCCESS;

    // Attempt parsing until we run out of data,
    // parse all of the request headers, or encounter an error.
    result = HNSS_RESULT_PARSE_CONTINUE;
    while( result == HNSS_RESULT_PARSE_CONTINUE )
    {
        result = readRequestHeaders();    
    }
    
    switch( result )
    {
        case HNSS_RESULT_PARSE_ERR:
        {
            printf( "ERROR: Rx parsing\n");
            return HNSS_RESULT_FAILURE;
        }
        break;
            
        case HNSS_RESULT_PARSE_WAIT:
        {
            printf( "Wait for more data\n");    
        }

        // If all of the headers for a request
        // have been received then proceed with 
        // dispatch.
        case HNSS_RESULT_REQUEST_READY:
        {
            printf( "Request Ready\n");
            m_curRR.getRequest().debugPrint();
            return HNSS_RESULT_REQUEST_READY;
        }

    }

    return HNSS_RESULT_SUCCESS;
}

#if 0
HNSS_RESULT_T 
HNSCGISinkClient::sendData( std::ostream &outStream )
{
    HNSS_RESULT_T result;
    //char rtnBuf[1024];

    //sprintf( rtnBuf, "Status: 200 OK\r\nContent-Type: text/plain\r\n\r\n42" );

    ssize_t bytesWritten = send( m_fd, rtnBuf, strlen(rtnBuf), 0 );
 
    printf( "HNSCGISinkClient::sendData - fd: %u  bytesWritten: %lu\n", m_fd, bytesWritten );
    
    return HNSS_RESULT_SUCCESS;
}
#endif

void
HNSCGISinkClient::finish()
{
    printf( "Finishing client %d\n", m_fd );
    close( m_fd );
}

HNProxyHTTPReqRsp*
HNSCGISinkClient::getReqRsp()
{
    return &m_curRR;
}

std::istream*
HNSCGISinkClient::getSourceStreamRef()
{
    return &m_istream;
}

std::ostream*
HNSCGISinkClient::getSinkStreamRef()
{
    std::cout << "HNSCGISinkClient::getSinkStreamRef - m_osteam, m_fd: " << m_fd << std::endl;
    return &m_ostream;
}


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
    m_parentRequestQueue = NULL;
    m_instanceName = "default";
    m_runMonitor = false;
    m_thelp = NULL;
}

HNSCGISink::~HNSCGISink()
{

}

void 
HNSCGISink::setParentRequestQueue( HNSigSyncQueue *parentRequestQueue )
{
    m_parentRequestQueue = parentRequestQueue;
}

HNSigSyncQueue* 
HNSCGISink::getProxyResponseQueue()
{
    return &m_proxyResponseQueue;
}

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

    // Initialize the proxyResponseQueue
    // and add it to the epoll loop
    m_proxyResponseQueue.init();
    int proxyQFD = m_proxyResponseQueue.getEventFD();
    addSocketToEPoll( proxyQFD );

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
            else if( proxyQFD == m_events[i].data.fd )
            {
                while( m_proxyResponseQueue.getPostedCnt() )
                {
                    HNProxyHTTPReqRsp *response = (HNProxyHTTPReqRsp *) m_proxyResponseQueue.aquireRecord();

                    std::map< int, HNSCGISinkClient* >::iterator it = m_clientMap.find( response->getParentTag() );
                    if( it == m_clientMap.end() )
                    {
                        syslog( LOG_ERR, "ERROR: Could not find client record - sfd: %d", response->getParentTag() );
                        //return HNSS_RESULT_FAILURE;
                    }
    
                    std::cout << "HNSCGISink::Received proxy response" << std::endl;

                    HNPRR_RESULT_T status = response->getResponse().sendSCGIResponseHeaders();

                    while( status == HNPRR_RESULT_MSG_CONTENT )
                    {
                        status = response->getResponse().xferContentChunk( 4096 );
                    }

                    it->second->finish();
                    m_clientMap.erase(it);
                }
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
        syslog( LOG_ERR, "HNSCGISink - Failed to get socket flags: %s", strerror(errno) );
        return HNSS_RESULT_FAILURE;
    }

    flags |= O_NONBLOCK;
    s = fcntl( sfd, F_SETFL, flags );
    if( s == -1 )
    {
        syslog( LOG_ERR, "HNSCGISink - Failed to set socket flags: %s", strerror(errno) );
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

    // Since the socket is bound to a fs path, try a unlink first to clean up any leftovers.
    unlink( str );
    
    // Attempt to create the new unix socket.
    m_acceptFD = socket( AF_UNIX, SOCK_STREAM, 0 );
    if( m_acceptFD == -1 )
    {
        syslog( LOG_ERR, "Opening daemon listening socket failed (%s).", strerror(errno) );
        return HNSS_RESULT_FAILURE;
    }

    // Bind it to the path in the filesystem
    if( bind( m_acceptFD, (struct sockaddr *) &addr, sizeof( sa_family_t ) + strlen( str ) + 1 ) == -1 )
    {
        syslog( LOG_ERR, "Failed to bind socket to @%s (%s).", str, strerror(errno) );
        return HNSS_RESULT_FAILURE;
    }

    // Accept connections.
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

        HNSCGISinkClient *client = new HNSCGISinkClient( infd, this );
        m_clientMap.insert( std::pair< int, HNSCGISinkClient* >( infd, client ) );

        addSocketToEPoll( infd );
    }

    return HNSS_RESULT_SUCCESS;
}

                    
HNSS_RESULT_T
HNSCGISink::closeClientConnection( int clientFD )
{
    m_clientMap.erase( clientFD );

    removeSocketFromEPoll( clientFD );

    close( clientFD );

    syslog( LOG_ERR, "Closed client - sfd: %d", clientFD );

    return HNSS_RESULT_SUCCESS;
}

HNSS_RESULT_T
HNSCGISink::processClientRequest( int cfd )
{
    HNSS_RESULT_T result;

    syslog( LOG_ERR, "Process client data - sfd: %d", cfd );
    
    // Find the client record
    std::map< int, HNSCGISinkClient* >::iterator it = m_clientMap.find( cfd );
    if( it == m_clientMap.end() )
    {
        syslog( LOG_ERR, "ERROR: Could not find client record - sfd: %d", cfd );
        return HNSS_RESULT_FAILURE;
    }
    
    // Attempt to receive data for current request
    result = it->second->recvData();
    switch( result )
    {
        case HNSS_RESULT_FAILURE:
        {
            syslog( LOG_ERR, "ERROR: Failed while receiving data - sfd: %d", cfd );
            return HNSS_RESULT_FAILURE;
        }
        break;

        case HNSS_RESULT_REQUEST_READY:
        {
            queueProxyRequest( it->second->getReqRsp() );
            return HNSS_RESULT_SUCCESS;
        }
        break;

    }

#if 0
    case HNSS_RESULT_CLIENT_DONE:
    {
        printf( "Finishing client %d\n", cfd);
        close(cfd);
        m_clientMap.erase(it);
        return HNSS_RESULT_SUCCESS;
    }
#endif

    // Check if action should be taken for any requests
    
    return HNSS_RESULT_SUCCESS;
}

void 
HNSCGISink::queueProxyRequest( HNProxyHTTPReqRsp *reqPtr )
{
    if( m_parentRequestQueue == NULL )
        return;

    m_parentRequestQueue->postRecord( reqPtr );
}

#if 0
void 
HNSCGISink::markForSend( uint fd )
{
    // FIXME:  Temporary, this should be scheduled through the epoll loop instead.

    // Find the client record
    std::map< int, HNSCGISinkClient >::iterator it = m_clientMap.find( fd );
    if( it == m_clientMap.end() )
    {
        syslog( LOG_ERR, "ERROR: Could not find client record - sfd: %d", fd );
        //return HNSS_RESULT_FAILURE;
    }
    
    // Attempt to receive data for current request
    if( it->second.sendData() == HNSS_RESULT_FAILURE )
    {
        syslog( LOG_ERR, "ERROR: Failed while sending data - sfd: %d", fd );
        //return HNSS_RESULT_FAILURE;
    }

}
#endif


