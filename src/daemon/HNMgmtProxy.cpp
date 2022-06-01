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


HNProxyTicket::HNProxyTicket( HNProxyHTTPReqRsp *parentRR )
{
    m_parentRR = parentRR;
}

HNProxyTicket::~HNProxyTicket()
{

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

                    makeProxyRequest( request );
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

HNPS_RESULT_T
HNProxySequencer::makeProxyRequest( HNProxyTicket *reqTicket )
{
    Poco::URI uri;
    uri.setScheme( "http" );
    uri.setHost( "192.168.1.155" );
    uri.setPort( 8080 );
    uri.setPath( "/hnode2/irrigation/status" );

    pn::HTTPClientSession session( uri.getHost(), uri.getPort() );
    pn::HTTPRequest request( pn::HTTPRequest::HTTP_GET, uri.getPathAndQuery(), pn::HTTPMessage::HTTP_1_1 );
    pn::HTTPResponse response;

    session.sendRequest( request );

    std::istream& rs = session.receiveResponse( response );
    std::cout << response.getStatus() << " " << response.getReason() << " " << response.getContentLength() << std::endl;

    if( response.getStatus() != Poco::Net::HTTPResponse::HTTP_OK )
    {
        return HNPS_RESULT_FAILURE;
    }

    std::string body;
    Poco::StreamCopier::copyToString( rs, body );
    std::cout << "Response:" << std::endl << body << std::endl;

    return HNPS_RESULT_SUCCESS;
}

