#ifndef _HN_SCGI_SINK_H_
#define _HN_SCGI_SINK_H_

#include <sys/epoll.h>

#include <ext/stdio_filebuf.h>

#include <string>
#include <map>
#include <list>
#include <fstream>
#include <sstream>

#include <hnode2/HNSigSyncQueue.h>
#include <hnode2/HNodeID.h>

//#include "HNProxyReqRsp.h"

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
    HNSS_RESULT_CLIENT_DONE,
    HNSS_RESULT_MSG_CONTENT,
    HNSS_RESULT_MSG_COMPLETE
}HNSS_RESULT_T;

typedef enum HNSCGIRRStreamState
{
    HNSCGI_SS_IDLE,              // In between requests.
    HNSCGI_SS_HDR_NSTR_LEN,      // Extract the length part of the header netstring
    HNSCGI_SS_HDR_ACCUMULATE,    // Accumulate all of the expected bytes for the header netstring.
    HNSCGI_SS_HDR_EXTRACT_PAIRS, // Parse the netstring buffer into header name-value pairs.
    HNSCGI_SS_HDR_NSTR_COMMA,    // Consume the comma at the end of the header net string
    HNSCGI_SS_HDR_DONE,          // Request Header has been parsed. 
    HNSCGI_SS_ERROR              // An error occurred during processing.
}HNSC_SS_T;


class HNPRRContentSource
{
    public:
        // Return the stream object for inbound content
        virtual std::istream* getSourceStreamRef() = 0;
};

class HNPRRContentSink
{
    public:
        // Return the the stream object for outbound content
        virtual std::ostream* getSinkStreamRef() = 0;
};

// Signature for optional shutdown function to get free resources 
// after completion of the request-response operations.
typedef void (*SHUTDOWN_CALL_FNPTR_T)( void *objAddr );

class HNSCGIMsg : public HNPRRContentSource, public HNPRRContentSink
{
    private:
        std::string m_uri;
        std::string m_method;
        uint m_statusCode;
        std::string m_reason;
        
        bool m_headerComplete;
        bool m_dispatched;
        
        std::map< std::string, std::string > m_cgiVarMap;

        std::map< std::string, std::string > m_paramMap;
        
        HNPRRContentSource  *m_cSource;
        HNPRRContentSink    *m_cSink;

        std::stringstream    m_localContent;
        uint                 m_contentMoved;

    public:
        HNSCGIMsg();
       ~HNSCGIMsg();
        
        void clearHeaders();

        void setHeaderDone( bool value );
        bool isHeaderDone();
        
        void setDispatched( bool value );
        bool isDispatched();     
       
        void setURI( std::string uri );
        void setMethod( std::string method );

        void setStatusCode( uint statusCode );
        void setReason( std::string reason );

        void setContentLength( uint length );

        void setContentType( std::string typeStr );

        void configAsNotImplemented();
        void configAsNotFound();
        void configAsInternalServerError();

        uint getStatusCode();
        std::string getReason();

        uint getContentLength();

        void addSCGIRequestHeader( std::string name, std::string value );

        void addHdrPair( std::string name, std::string value );

        HNSS_RESULT_T sendSCGIResponseHeaders();

        bool hasHeader( std::string name );

        const std::string& getURI() const;
        const std::string& getMethod() const;
    
        void setContentSource( HNPRRContentSource *source );
        void setContentSink( HNPRRContentSink *sink );

        std::ostream& useLocalContentSource();
        void finalizeLocalContent();

        HNSS_RESULT_T xferContentChunk( uint maxChunkLength );

        std::istream* getSourceStreamRef();
        std::ostream* getSinkStreamRef();

        void debugPrint();
};

class HNSCGIRR : public HNPRRContentSource, public HNPRRContentSink
{
        uint               m_fd;
        HNSCGISink        *m_parent;

        HNSCGIMsg m_request;
        HNSCGIMsg m_response;
            
        HNSC_SS_T  m_rxState;
        
        std::string m_partialStr;

        uint m_expHdrLen;
        uint m_rcvHdrLen;
        
        char *m_headerBuf;
        
        __gnu_cxx::stdio_filebuf<char> m_ifilebuf;
        std::iostream m_istream; 

        __gnu_cxx::stdio_filebuf<char> m_ofilebuf;
        std::iostream m_ostream;

        std::vector< std::pair< SHUTDOWN_CALL_FNPTR_T, void* > > m_shutdownCallList;

        void setRxParseState( HNSC_SS_T newState );
        HNSS_RESULT_T readRequestHeaders();
        
        HNSS_RESULT_T readNetStrStart();
        HNSS_RESULT_T fillRequestHeaderBuffer();
        HNSS_RESULT_T extractHeaderPairsFromBuffer();
        HNSS_RESULT_T consumeNetStrComma();

    public:
        HNSCGIRR( uint fd, HNSCGISink *parent );
       ~HNSCGIRR();

        uint getSCGIFD();

        HNSS_RESULT_T recvData();

        void finish();

        // Add a function and parameter that should get called when the 
        // Request and Response is finished and things are being cleaned up.
        void addShutdownCall( SHUTDOWN_CALL_FNPTR_T funcPtr, void *objAddr );

        HNSCGIMsg& getReqMsg();
        HNSCGIMsg& getRspMsg();

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
        std::map< int, HNSCGIRR* > m_rrMap;
        
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

        void queueProxyRequest( HNSCGIRR *reqPtr );
        
        void markForSend( uint fd );

        void clientComplete( uint fd );

    friend HNSCGIRunner;
};

#endif // _HN_MANAGED_DEVICE_ARBITER_H_
