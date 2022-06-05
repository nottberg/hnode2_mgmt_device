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
#include <Poco/StreamCopier.h>
#include "Poco/Net/HTTPClientSession.h"
#include "Poco/Net/HTTPRequest.h"
#include "Poco/Net/HTTPResponse.h"
#include "Poco/URI.h"

#include "HNMgmtProxy.h"

namespace pn  = Poco::Net;

#define MAXEVENTS  8


HNProxyTicket::HNProxyTicket( HNSCGIRR *parentRR )
{
    m_parentRR = parentRR;
}

HNProxyTicket::~HNProxyTicket()
{

}

void 
HNProxyTicket::setCRC32ID( std::string id )
{
    m_crc32ID = id;
}

void 
HNProxyTicket::setAddress( std::string address )
{
    m_address = address;
}

void 
HNProxyTicket::setPort( uint16_t port )
{
    m_port = port;
}

void 
HNProxyTicket::setQueryStr( std::string query )
{
    m_queryStr = query;
}

void 
HNProxyTicket::buildProxyPath( std::vector< std::string > &segments )
{
    m_proxyPathStr.clear();
    for( std::vector< std::string >::iterator it = segments.begin(); it != segments.end(); it++ )
    {
        m_proxyPathStr += "/" + *it;
    }
}

std::string
HNProxyTicket::getCRC32ID()
{
    return m_crc32ID;
}

std::string
HNProxyTicket::getAddress()
{
    return m_address;
}

uint16_t
HNProxyTicket::getPort()
{
    return m_port;
}

std::string
HNProxyTicket::getQueryStr()
{
    return m_queryStr;
}

std::string
HNProxyTicket::getProxyPath()
{
    return m_proxyPathStr;
}

HNSCGIRR* 
HNProxyTicket::getRR()
{
    return m_parentRR;
}

// Helper class for running HNSCGISink  
// proxy loop as an independent thread
class HNProxySequencerRunner : public Poco::Runnable
{
    private:
        Poco::Thread      m_thread;
        HNProxySequencer *m_abObj;

    public:  
        HNProxySequencerRunner( HNProxySequencer *value )
        {
            m_abObj = value;
        }

        void startThread()
        {
            m_thread.start( *this );
        }

        void killThread()
        {
            m_abObj->killProxySequencerLoop();
            m_thread.join();
        }

        virtual void run()
        {
            m_abObj->runProxySequencerLoop();
        }

};


HNProxySequencer::HNProxySequencer()
{

}

HNProxySequencer::~HNProxySequencer()
{

}

void 
HNProxySequencer::setParentResponseQueue( HNSigSyncQueue *parentResponseQueue )
{
    m_responseQueue = parentResponseQueue;
}

HNSigSyncQueue* 
HNProxySequencer::getRequestQueue()
{
    return &m_requestQueue;
}

void
HNProxySequencer::start()
{
    int error;

    std::cout << "HNProxySequencer::start()" << std::endl;

    // Allocate the thread helper
    m_thelp = new HNProxySequencerRunner( this );
    if( !m_thelp )
    {
        //cleanup();
        return;
    }

    m_runMonitor = true;

    // Start up the event loop
    ( (HNProxySequencerRunner*) m_thelp )->startThread();
}

void 
HNProxySequencer::runProxySequencerLoop()
{
    std::cout << "HNProxySequencer::runMonitoringLoop()" << std::endl;

    // Initialize for event loop
    m_epollFD = epoll_create1( 0 );
    if( m_epollFD == -1 )
    {
        //log.error( "ERROR: Failure to create epoll event loop: %s", strerror(errno) );
        return;
    }

    // Buffer where events are returned 
    m_events = (struct epoll_event *) calloc( MAXEVENTS, sizeof m_event );

    // Initialize the proxyResponseQueue
    // and add it to the epoll loop
    m_requestQueue.init();
    int requestQFD = m_requestQueue.getEventFD();
    addSocketToEPoll( requestQFD );

    // The listen loop 
    while( m_runMonitor == true )
    {
        int n;
        int i;
        struct tm newtime;
        time_t ltime;

        // Check for events
        n = epoll_wait( m_epollFD, m_events, MAXEVENTS, 2000 );

        std::cout << "HNProxySequencer::monitor wakeup" << std::endl;

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
            if( requestQFD == m_events[i].data.fd )
            {
                while( m_requestQueue.getPostedCnt() )
                {
                    HNProxyTicket *request = (HNProxyTicket *) m_requestQueue.aquireRecord();

                    std::cout << "HNProxySequencer::Received proxy request" << std::endl;

                    executeProxyRequest( request );
                }
            }
        }
    }

    std::cout << "HNProxySequencer::monitor exit" << std::endl;
}

void
HNProxySequencer::shutdown()
{
    if( !m_thelp )
    {
        //cleanup();
        return;
    }

    // End the event loop
    ( (HNProxySequencerRunner*) m_thelp )->killThread();

    delete ( (HNProxySequencerRunner*) m_thelp );
    m_thelp = NULL;
}

void 
HNProxySequencer::killProxySequencerLoop()
{
    m_runMonitor = false;    
}

HNPS_RESULT_T
HNProxySequencer::addSocketToEPoll( int sfd )
{
    int flags, s;

    flags = fcntl( sfd, F_GETFL, 0 );
    if( flags == -1 )
    {
        syslog( LOG_ERR, "HNProxySequencer - Failed to get socket flags: %s", strerror(errno) );
        return HNPS_RESULT_FAILURE;
    }

    flags |= O_NONBLOCK;
    s = fcntl( sfd, F_SETFL, flags );
    if( s == -1 )
    {
        syslog( LOG_ERR, "HNProxySequencer - Failed to set socket flags: %s", strerror(errno) );
        return HNPS_RESULT_FAILURE; 
    }

    m_event.data.fd = sfd;
    m_event.events = EPOLLIN | EPOLLET;
    s = epoll_ctl( m_epollFD, EPOLL_CTL_ADD, sfd, &m_event );
    if( s == -1 )
    {
        return HNPS_RESULT_FAILURE;
    }

    return HNPS_RESULT_SUCCESS;
}

HNPS_RESULT_T
HNProxySequencer::removeSocketFromEPoll( int sfd )
{
    int s;

    s = epoll_ctl( m_epollFD, EPOLL_CTL_DEL, sfd, NULL );
    if( s == -1 )
    {
        return HNPS_RESULT_FAILURE;
    }

    return HNPS_RESULT_SUCCESS;
}

class HNProxyPocoHelper : public HNPRRContentSource, public HNPRRContentSink 
{
    public:
        HNProxyPocoHelper();
       ~HNProxyPocoHelper();

        void init( HNProxyTicket *reqTicket );

        HNSS_RESULT_T initiateRequest( HNProxyTicket *reqTicket );
        HNSS_RESULT_T waitForResponse( HNProxyTicket *reqTicket );

        virtual std::istream* getSourceStreamRef();
        virtual std::ostream* getSinkStreamRef();

    private:
        Poco::URI              m_uri;

        pn::HTTPClientSession  m_session;
        pn::HTTPRequest        m_request;
        pn::HTTPResponse       m_response;

        std::istream          *m_rspStream;
        std::ostream          *m_reqStream;
};

HNProxyPocoHelper::HNProxyPocoHelper()
: m_request( pn::HTTPMessage::HTTP_1_1 )
{
    m_rspStream = NULL;
    m_reqStream = NULL;
}

HNProxyPocoHelper::~HNProxyPocoHelper()
{

}

std::istream* 
HNProxyPocoHelper::getSourceStreamRef()
{
    return m_rspStream;
}

std::ostream* 
HNProxyPocoHelper::getSinkStreamRef()
{
    std::cout << "HNProxyPocoHelper::getSinkStreamRef - m_reqStream" << std::endl;
    return m_reqStream;
}

void
HNProxyPocoHelper::init( HNProxyTicket *reqTicket )
{
    // Create the URL to proxy too.
    m_uri.setScheme( "http" );
    m_uri.setHost( reqTicket->getAddress() );
    m_uri.setPort( reqTicket->getPort() );
    m_uri.setRawQuery( reqTicket->getQueryStr() );
    m_uri.setPath( reqTicket->getProxyPath() ); 

    std::cout << "Proxy Request URL: " << m_uri.getPathAndQuery() << std::endl;

    // Setup session object
    m_session.setHost( m_uri.getHost() );
    m_session.setPort( m_uri.getPort() );

    // Set up request object from info in ticket and originating request.
    HNSCGIMsg &reqMsg = reqTicket->getRR()->getReqMsg();

    std::cout << "Proxy Request Method: " << reqMsg.getMethod() << std::endl;

    m_request.setMethod( reqMsg.getMethod() );
    m_request.setURI( m_uri.getPathAndQuery() );
}

HNSS_RESULT_T
HNProxyPocoHelper::initiateRequest( HNProxyTicket *reqTicket )
{
    HNSCGIMsg &reqMsg = reqTicket->getRR()->getReqMsg();

    // Send the request
    m_reqStream = &m_session.sendRequest( m_request );

    // Associate the transfer stream
    reqMsg.setContentSink( this );

    return (reqMsg.getContentLength() > 0) ? HNSS_RESULT_MSG_CONTENT : HNSS_RESULT_MSG_COMPLETE;
}

HNSS_RESULT_T
HNProxyPocoHelper::waitForResponse( HNProxyTicket *reqTicket )
{
    HNSCGIMsg &rspMsg = reqTicket->getRR()->getRspMsg();

    // Wait for a response
    m_rspStream = &m_session.receiveResponse( m_response );
    std::cout << m_response.getStatus() << " " << m_response.getReason() << " " << m_response.getContentLength() << std::endl;

    rspMsg.setStatusCode( m_response.getStatus() );
    rspMsg.setReason( m_response.getReason() );

    if( m_response.getContentLength() != (-1) )
        rspMsg.setContentLength( m_response.getContentLength() );

    for( pn::NameValueCollection::ConstIterator it = m_response.begin(); it != m_response.end(); it++ )
    {
        rspMsg.addHdrPair( it->first, it->second );
    }

    // Associate the transfer stream
    rspMsg.setContentSource( this );

    return (rspMsg.getContentLength() > 0) ? HNSS_RESULT_MSG_CONTENT : HNSS_RESULT_MSG_COMPLETE;
}

HNPS_RESULT_T
HNProxySequencer::executeProxyRequest( HNProxyTicket *reqTicket )
{
    HNSS_RESULT_T  result;
    HNSCGIMsg &reqMsg = reqTicket->getRR()->getReqMsg();
    HNProxyPocoHelper *ph = new HNProxyPocoHelper();

    ph->init( reqTicket );

    result = ph->initiateRequest( reqTicket );
    while( result == HNSS_RESULT_MSG_CONTENT )
    {
        result = reqMsg.xferContentChunk( 4096 );
    }

    if( result != HNSS_RESULT_MSG_COMPLETE )
    {
        return HNPS_RESULT_FAILURE;
    }

    result = ph->waitForResponse( reqTicket );

    if( (result != HNSS_RESULT_MSG_COMPLETE) && (result != HNSS_RESULT_MSG_CONTENT) )
    {
        return HNPS_RESULT_FAILURE;
    }

    if( m_responseQueue == NULL )
        return HNPS_RESULT_FAILURE;

    m_responseQueue->postRecord( reqTicket );

#if 0
    Poco::URI uri;
    uri.setScheme( "http" );
    uri.setHost( reqTicket->getAddress() );
    uri.setPort( reqTicket->getPort() );
    uri.setPath( reqTicket->getPath() ); // i.e. "/hnode2/irrigation/status"

    // Allocate
    pn::HTTPClientSession session( uri.getHost(), uri.getPort() );

    // Set session based on request parameters
    // Copy over any proxy headers
    HNProxyHTTPMsg &reqMsg = reqTicket->getRR()->getRequest();
    pn::HTTPRequest request( reqMsg.getMethod(), uri.getPathAndQuery(), pn::HTTPMessage::HTTP_1_1 );


    // Send the request
    std::ostream& os = session.sendRequest( request );

    // If there is outbound data, then send that now
    if( reqMsg.getContentLength() )
    {
        //uint bytesSent
    }

    // Wait for a response
    pn::HTTPResponse response;
    std::istream& rs = session.receiveResponse( response );
    std::cout << response.getStatus() << " " << response.getReason() << " " << response.getContentLength() << std::endl;

    if( response.getStatus() != Poco::Net::HTTPResponse::HTTP_OK )
    {
        return HNPS_RESULT_FAILURE;
    }

    std::string body;
    Poco::StreamCopier::copyToString( rs, body );
    std::cout << "Response:" << std::endl << body << std::endl;
#endif

    return HNPS_RESULT_SUCCESS;
}

