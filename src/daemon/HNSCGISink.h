#ifndef _HN_SCGI_SINK_H_
#define _HN_SCGI_SINK_H_

#include <sys/epoll.h>

#include <string>
#include <map>
#include <list>

#include "hnode2/HNodeID.h"

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
    HNSS_RESULT_RCV_ERR
}HNSS_RESULT_T;

class HNSCGIChunk
{
    private:
        // Storage for some data
        uint8_t  m_data[4096];
        
        // Start and end index
        uint m_startIdx;
        uint m_endIdx;

        // Current point where parsing is occuring.
        uint m_parseIdx;
        
        // How much is used
        uint m_length;

        uint getInsertPos( uint8_t **bufPtr );
        
    public:
        HNSCGIChunk();
       ~HNSCGIChunk();
    
        bool isConsumed();       
        bool hasSpace();
       
        HNSS_RESULT_T recvData( int fd );
        
        HNSS_RESULT_T peekNextByte( uint8_t &nxtChar );
        
        void consumeByte();
        void consumeBytes( uint byteCnt );
        
        HNSS_RESULT_T extractNetStrStart( std::string &lenStr );
        HNSS_RESULT_T extractNullStr( std::string &nullStr );
        HNSS_RESULT_T extractNetStrEnd();        
};
    
class HNSCGIChunkQueue
{
    private:
        std::list< HNSCGIChunk* > m_chunkList;
    
        std::string m_partialStr;
        
    public:
        HNSCGIChunkQueue();
       ~HNSCGIChunkQueue();
       
        HNSS_RESULT_T recvData( int fd );

        void resetParseState();
        
        HNSS_RESULT_T parseNetStrStart( uint &headerLength );

        HNSS_RESULT_T parseNullStr( std::string &name );
        
        HNSS_RESULT_T parseNetStrEnd();
        
};

#if 0
class HNSCGIReqRsp
{
    private:
        // 
        uint32_t m_contentLength;
        
        // 
        std::map< std::string, std::string > m_paramMap;
        
    public:
        HNSCGIReqRsp();
       ~HNSCGIReqRsp();
       
        void setHeaderDone( bool value );
        bool isHeaderDone();
        
        void setRunning( bool value );
        bool isRunning();     
       
        void addHdrPair( std::string name, std::string value );
};
#endif

typedef enum HNSCGISinkClientStreamState
{
    HNSCGI_SS_IDLE,           // In between requests.
    HNSCGI_SS_HDR_NSTR_LEN,   // Waiting for start of the header netstring
    HNSCGI_SS_HDR_DATA_NAME,  // Waiting for the next header name to be recieved
    HNSCGI_SS_HDR_DATA_VALUE, // Waiting for the next header value to be recieved
    HNSCGI_SS_HDR_NSTR_COMMA, // Waiting for header netstring trailing comma
    HNSCGI_SS_PAYLOAD,        // Waiting for the Content Length of payload
    HNSCGI_SS_DONE,           // Request RX is complete 
    HNSCGI_SS_ERROR           // An error occurred during processing.
}HNSC_SS_T;

class HNSCGISinkClient
{
        HNSCGISink        *m_parent;

        HNSCGIChunkQueue   m_rxQueue;
        HNProxyRequest    *m_curReq;
        
        //std::list< HNSCGIReqRsp* > m_rrQueue;
        
        HNSC_SS_T  m_rxState;
        
        uint m_expHdrLen;
        uint m_rcvHdrLen;
        
        std::string m_curHdrName;
        std::string m_curHdrValue;
        
        void setRxParseState( HNSC_SS_T newState );
        HNSS_RESULT_T rxNextParse();
        
    public:
        HNSCGISinkClient( HNSCGISink *parent );
       ~HNSCGISinkClient();
       
        HNSS_RESULT_T recvData( int fd );
        
};

class HNSCGISink
{

    private:
        // The instance name for this daemon
        std::string m_instanceName;

        // A map of reqrsp objects
        
        // A map of client connections
        std::map< int, HNSCGISinkClient > m_clientMap;
        
        // The thread helper
        void *m_thelp;

        // Should the monitor still be running.
        bool m_runMonitor;

        int m_epollFD;
        int m_acceptFD;
    
        struct epoll_event m_event;
        struct epoll_event *m_events;

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

        //HNMDL_RESULT_T notifyDiscoverAdd( HNMDARecord &record );
        //HNMDL_RESULT_T notifyDiscoverRemove( HNMDARecord &record );

        void start( std::string instance );
        void shutdown();

        void debugPrint();

        void dispatchProxyRequest( HNProxyRequest *reqPtr );
        
    friend HNSCGIRunner;
};

#endif // _HN_MANAGED_DEVICE_ARBITER_H_