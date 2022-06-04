#ifndef _HN_SCGI_SINK_H_
#define _HN_SCGI_SINK_H_

#include <sys/epoll.h>

#include <ext/stdio_filebuf.h>

#include <string>
#include <map>
#include <list>
#include <fstream>

#include <hnode2/HNSigSyncQueue.h>
#include <hnode2/HNodeID.h>

#include "HNProxyReqRsp.h"

// Forward declaration
class HNSCGIRunner;
class HNSCGISink;

typedef enum HNSCGISinkResultEnum
{
    HNSS_RESULT_SUCCESS,
    HNSS_RESULT_FAILURE,
    HNSS_RESULT_PARSE_COMPLETE,
    HNSS_RESULT_PARSE_WAIT,
    HNSS_RESULT_PARSE_CONTINUE,    
    HNSS_RESULT_PARSE_ERR,
    HNSS_RESULT_RCV_DONE,
    HNSS_RESULT_RCV_CONT,
    HNSS_RESULT_RCV_ERR,
    HNSS_RESULT_REQUEST_READY,
    HNSS_RESULT_CLIENT_DONE
}HNSS_RESULT_T;

typedef enum HNSCGISinkClientStreamState
{
    HNSCGI_SS_IDLE,              // In between requests.
    HNSCGI_SS_HDR_NSTR_LEN,      // Extract the length part of the header netstring
    HNSCGI_SS_HDR_ACCUMULATE,    // Accumulate all of the expected bytes for the header netstring.
    HNSCGI_SS_HDR_EXTRACT_PAIRS, // Parse the netstring buffer into header name-value pairs.
    HNSCGI_SS_HDR_NSTR_COMMA,    // Consume the comma at the end of the header net string
    HNSCGI_SS_HDR_DONE,          // Request Header has been parsed. 
    HNSCGI_SS_ERROR              // An error occurred during processing.
}HNSC_SS_T;

class HNSCGISinkClient : public HNPRRContentSource, public HNPRRContentSink
{
        uint               m_fd;
        HNSCGISink        *m_parent;

        HNProxyHTTPReqRsp  m_curRR;
               
        HNSC_SS_T  m_rxState;
        
        std::string m_partialStr;

        uint m_expHdrLen;
        uint m_rcvHdrLen;
        
        char *m_headerBuf;
        
        __gnu_cxx::stdio_filebuf<char> m_ifilebuf;
        std::iostream m_istream; 

        __gnu_cxx::stdio_filebuf<char> m_ofilebuf;
        std::iostream m_ostream;

        void setRxParseState( HNSC_SS_T newState );
        HNSS_RESULT_T readRequestHeaders();
        
        HNSS_RESULT_T readNetStrStart();
        HNSS_RESULT_T fillRequestHeaderBuffer();
        HNSS_RESULT_T extractHeaderPairsFromBuffer();
        HNSS_RESULT_T consumeNetStrComma();

    public:
        HNSCGISinkClient( uint fd, HNSCGISink *parent );
       ~HNSCGISinkClient();
       
        HNSS_RESULT_T recvData();

        void finish();

        HNProxyHTTPReqRsp *getReqRsp();

        virtual std::istream* getSourceStreamRef();
        virtual std::ostream* getSinkStreamRef();
};

class HNSCGISink
{

    private:
        // The instance name for this daemon
        std::string m_instanceName;

        // A map of reqrsp objects
        
        // A map of client connections
        std::map< int, HNSCGISinkClient* > m_clientMap;
        
        // The thread helper
        void *m_thelp;

        // Should the monitor still be running.
        bool m_runMonitor;

        int m_epollFD;
        int m_acceptFD;
    
        struct epoll_event m_event;
        struct epoll_event *m_events;

        HNSigSyncQueue  m_proxyResponseQueue;

        HNSigSyncQueue  *m_parentRequestQueue;

        HNSS_RESULT_T openSCGISocket();

        HNSS_RESULT_T addSocketToEPoll( int sfd );
        HNSS_RESULT_T removeSocketFromEPoll( int sfd );
        HNSS_RESULT_T processNewClientConnections();
        HNSS_RESULT_T closeClientConnection( int clientFD );
        HNSS_RESULT_T processClientRequest( int cfd );

    protected:
        void runSCGILoop();
        void killSCGILoop();

    public:
        HNSCGISink();
       ~HNSCGISink();

        void setParentRequestQueue( HNSigSyncQueue *parentRequestQueue );

        HNSigSyncQueue* getProxyResponseQueue();

        void start( std::string instance );
        void shutdown();

        void debugPrint();

        void queueProxyRequest( HNProxyHTTPReqRsp *reqPtr );
        
        void markForSend( uint fd );

        void clientComplete( uint fd );

    friend HNSCGIRunner;
};

#endif // _HN_MANAGED_DEVICE_ARBITER_H_
