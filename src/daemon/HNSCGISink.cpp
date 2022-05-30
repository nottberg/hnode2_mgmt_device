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

HNSCGIChunk::HNSCGIChunk()
{        
    m_startIdx = 0;
    m_endIdx   = 0;
        
    m_length = 0;
}

HNSCGIChunk::~HNSCGIChunk()
{

}

uint
HNSCGIChunk::getInsertPos( uint8_t **bufPtr )
{
    *bufPtr = &m_data[m_endIdx];
    return ( sizeof(m_data) - m_length );
}

bool 
HNSCGIChunk::hasSpace()
{
    return ( m_length < sizeof(m_data) ) ? true : false; 
}

bool 
HNSCGIChunk::isConsumed()
{
    return false; 
}

HNSS_RESULT_T 
HNSCGIChunk::recvData( int fd )
{
    uint     availLen;
    uint8_t *bufPtr;
    
    availLen = getInsertPos( &bufPtr );
    
    printf( "HNSCGIChunk::recvData - availLen: %u\n", availLen );
    
    if( availLen == 0 )
        return HNSS_RESULT_RCV_CONT;
        
    ssize_t bytesRead = recv( fd, bufPtr, availLen, 0 );
 
    printf( "HNSCGIChunk::recvData - bytesRead: %lu\n", bytesRead );
   
    if( bytesRead == 0 )
        return HNSS_RESULT_RCV_DONE;
    else if( bytesRead < 0 )
        return HNSS_RESULT_RCV_ERR;
    
    // Update the data bounds.
    m_length  += bytesRead;
    m_endIdx += bytesRead;
    
    if( hasSpace() == false )
        return HNSS_RESULT_RCV_CONT;
        
    return HNSS_RESULT_RCV_DONE;
}

HNSS_RESULT_T
HNSCGIChunk::peekNextByte( uint8_t &nxtChar )
{
    if( m_startIdx >= m_endIdx )
        return HNSS_RESULT_FAILURE;
        
    nxtChar = m_data[ m_startIdx ];
    return HNSS_RESULT_SUCCESS;
}

void
HNSCGIChunk::consumeByte()
{
    consumeBytes(1);     
}

void
HNSCGIChunk::consumeBytes( uint byteCnt )
{
    m_startIdx += byteCnt;
    
    if( m_startIdx > sizeof( m_data ) )
        m_startIdx -= sizeof( m_data );
}

HNSS_RESULT_T
HNSCGIChunk::extractNetStrStart( std::string &lenStr )
{   
    // Check if there is any data available, 
    // if not wait for some more
    if( m_startIdx == m_endIdx )
        return HNSS_RESULT_PARSE_WAIT;
        
    // Look for only numeric characters with a delimiting ':' at the end,
    // if found turn the string into an uint and consume the characters.
    //char numBuf[32];
    //char *numIns = numBuf;
    uint curIdx;
    uint bytesProcessed = 0;
    
    bool hdrComplete = false;
  
    //printf( "extractNetStrStart - m_startIdx: %u, m_endIdx: %u\n", m_startIdx, m_endIdx );
    
    curIdx = m_startIdx;
    while( curIdx <= m_endIdx )
    {
        uint8_t curByte = m_data[ curIdx ];
            
        //printf( "extractNetStrStart - curIdx: %u, byte: '%c'\n", curIdx, curByte );
        
        bytesProcessed += 1;
            
        if( isspace( curByte ) == true )
        {
            //printf( "isspace\n");
            curIdx += 1;
            continue;
        }
        else if( isdigit( curByte ) == true )
        {
            //printf( "isdigit\n");
            //*numIns = curByte;
            //numIns++;
            lenStr.push_back( curByte );
            curIdx += 1;
            continue;
        }
        else if( curByte == ':' )
        {
            hdrComplete = true;
            break;
        }
        else
            return HNSS_RESULT_PARSE_ERR;
            
        // Check next character
        curIdx += 1;
    }
     
    //printf( "extractNetStrStart - bytesConsumed: %u\n", bytesProcessed );

    consumeBytes( bytesProcessed );
       
    return ( hdrComplete == false ) ? HNSS_RESULT_PARSE_WAIT : HNSS_RESULT_PARSE_COMPLETE;
}

HNSS_RESULT_T
HNSCGIChunk::extractNullStr( std::string &nullStr )
{   
    // Check if there is any data available, 
    // if not wait for some more
    if( m_startIdx == m_endIdx )
        return HNSS_RESULT_PARSE_WAIT;
        
    // Transfer over all characters until a delimiting null is seen.
    uint curIdx;
    uint bytesProcessed = 0;
    
    bool strComplete = false;
  
    //printf( "extractNullStr - m_startIdx: %u, m_endIdx: %u\n", m_startIdx, m_endIdx );
    
    curIdx = m_startIdx;
    while( curIdx <= m_endIdx )
    {
        uint8_t curByte = m_data[ curIdx ];
            
        //printf( "extractNullStr - curIdx: %u, byte: '%c'\n", curIdx, curByte );
        
        bytesProcessed += 1;
            
        if( curByte == '\0' )
        {
            strComplete = true;
            curIdx += 1;
            break;
        }

        // Record this byte and move to next        
        nullStr.push_back( curByte );
        curIdx += 1;
    }
     
    //printf( "extractNullStr - bytesConsumed: %u\n", bytesProcessed );
     
    consumeBytes( bytesProcessed );
       
    return ( strComplete == false ) ? HNSS_RESULT_PARSE_WAIT : HNSS_RESULT_PARSE_COMPLETE;
}

HNSS_RESULT_T
HNSCGIChunk::extractNetStrEnd()
{   
    // Check if there is any data available, 
    // if not wait for some more
    if( m_startIdx == m_endIdx )
        return HNSS_RESULT_PARSE_WAIT;
        
    // Look for only the trailing ',' character of a netstring
    if( m_data[ m_startIdx ] == ',' )
    {
        consumeBytes(1);
        return HNSS_RESULT_PARSE_COMPLETE;
    }
    
    // Parsing error
    return HNSS_RESULT_PARSE_ERR;
}

HNSCGIChunkQueue::HNSCGIChunkQueue()
{

}

HNSCGIChunkQueue::~HNSCGIChunkQueue()
{

}
       
HNSS_RESULT_T 
HNSCGIChunkQueue::recvData( int fd )
{
    // Get iterator to current rx element
    std::list< HNSCGIChunk* >::iterator it = m_chunkList.begin();
    
    // If there is no current rx element, allocate one.
    // If the current rx-element is full, allocate another one.
    if( it == m_chunkList.end() )
    {
        HNSCGIChunk *chunkPtr = new HNSCGIChunk;
        m_chunkList.push_front( chunkPtr );
        it = m_chunkList.begin();
    }
    else if( (*it)->hasSpace() == false )
    {
        HNSCGIChunk *chunkPtr = new HNSCGIChunk;
        m_chunkList.push_front( chunkPtr );
        it = m_chunkList.begin();
    }
    
    // Receive all the data that we can
    HNSS_RESULT_T result = HNSS_RESULT_RCV_CONT;
    while( result == HNSS_RESULT_RCV_CONT )
    {
        result = (*it)->recvData( fd );
        
        switch( result )
        {
            case HNSS_RESULT_RCV_DONE:
            break;
            
            case HNSS_RESULT_RCV_CONT:
            {
                // Allocate some additional storage space
                HNSCGIChunk *chunkPtr = new HNSCGIChunk;
                m_chunkList.push_front( chunkPtr );
                it = m_chunkList.begin();                
            }
            break;
            
            case HNSS_RESULT_RCV_ERR:
                return HNSS_RESULT_FAILURE;
            break;
        }
    }
    
    return HNSS_RESULT_SUCCESS;
}

void 
HNSCGIChunkQueue::resetParseState()
{
    m_partialStr.clear();
}

HNSS_RESULT_T 
HNSCGIChunkQueue::parseNetStrStart( uint &headerLength )
{
   HNSS_RESULT_T result = HNSS_RESULT_PARSE_WAIT;
   
   // Default return length
   headerLength = 0;
   
   // Start parsing through the available data
   while( m_chunkList.empty() == false )
   {
       HNSCGIChunk *curChunk = m_chunkList.back();

       // Check in the current chunk of data.
       result = curChunk->extractNetStrStart( m_partialStr );
       
       // Determine if the current chunk has been consumed, if so remove it and move to the next
       if( curChunk->isConsumed() == true )
       {
           m_chunkList.pop_back();
       }
       
       // Check on the result, and next steps
       switch( result )
       {
           // Parsing complete and successful
           case HNSS_RESULT_PARSE_COMPLETE:
           {
               // Convert number string to an integer
               headerLength = strtol( m_partialStr.c_str(), NULL, 0 );
        
               return HNSS_RESULT_PARSE_COMPLETE;
           }
           break;
           
           // Found part of the length, check next chunk.
           case HNSS_RESULT_PARSE_WAIT:
           break;
           
           // Need to wait for more data to be recieved
           // or we encountered an error.
           case HNSS_RESULT_PARSE_ERR:
               return result;
           break;
       }
   }
   
   // Got here by finding a partial match,
   // but then running out of data. 
   // Signal we want to wait for more data.
   return HNSS_RESULT_PARSE_WAIT;
}

HNSS_RESULT_T 
HNSCGIChunkQueue::parseNullStr( std::string &name )
{
   HNSS_RESULT_T result = HNSS_RESULT_PARSE_WAIT;
    
   //printf( "parseNullStr - list: %lu\n", m_chunkList.size() );
   
   // Start parsing through the available data
   while( m_chunkList.empty() == false )
   {
       HNSCGIChunk *curChunk = m_chunkList.back();

       // Check in the current chunk of data.
       result = curChunk->extractNullStr( m_partialStr );
       
       // Determine if the current chunk has been consumed, if so remove it and move to the next
       if( curChunk->isConsumed() == true )
       {
           m_chunkList.pop_back();
       }
       
       // Check on the result, and next steps
       switch( result )
       {
           // Parsing complete and successful
           case HNSS_RESULT_PARSE_COMPLETE:
               name = m_partialStr;
               return HNSS_RESULT_PARSE_COMPLETE;
           break;
           
           // Found part of the length, check next chunk.
           case HNSS_RESULT_PARSE_WAIT:
           break;
           
           // Need to wait for more data to be recieved
           // or we encountered an error.
           case HNSS_RESULT_PARSE_ERR:
               return result;
           break;
       }
   }
   
   // Got here by finding a partial match,
   // but then running out of data. 
   // Signal we want to wait for more data.
   return HNSS_RESULT_PARSE_WAIT;
}

HNSS_RESULT_T 
HNSCGIChunkQueue::parseNetStrEnd()
{
   HNSS_RESULT_T result = HNSS_RESULT_PARSE_WAIT;
    
   //printf( "parseNetStrEnd - list: %lu\n", m_chunkList.size() );
   
   // Start parsing through the available data
   while( m_chunkList.empty() == false )
   {
       HNSCGIChunk *curChunk = m_chunkList.back();

       // Check in the current chunk of data.
       result = curChunk->extractNetStrEnd();
       
       // Determine if the current chunk has been consumed, if so remove it and move to the next
       if( curChunk->isConsumed() == true )
       {
           m_chunkList.pop_back();
       }
       
       // Check on the result, and next steps
       switch( result )
       {
           // Parsing complete and successful
           case HNSS_RESULT_PARSE_COMPLETE:
               return HNSS_RESULT_PARSE_COMPLETE;
           break;
           
           // Found part of the length, check next chunk.
           case HNSS_RESULT_PARSE_WAIT:
           break;
           
           // Need to wait for more data to be recieved
           // or we encountered an error.
           case HNSS_RESULT_PARSE_ERR:
               return result;
           break;
       }
   }
   
   // Got here by finding a partial match,
   // but then running out of data. 
   // Signal we want to wait for more data.
   return HNSS_RESULT_PARSE_WAIT;
}

#if 0
HNSCGIReqRsp::HNSCGIReqRsp()
{

}

HNSCGIReqRsp::~HNSCGIReqRsp()
{

}

void
HNSCGIReqRsp::addHdrPair( std::string name, std::string value )
{
    //printf( "addHdrPair - name: %s,  value: %s\n", name.c_str(), value.c_str() );
    
    m_paramMap.insert( std::pair<std::string, std::string>(name, value) );
}
#endif

HNSCGISinkClient::HNSCGISinkClient( uint fd, HNSCGISink *parent )
: m_curRR( fd ), m_filebuf( fd, (std::ios::in|std::ios::out|std::ios::binary) ), m_iostream( &m_filebuf)
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

    m_rxQueue.resetParseState();
    m_rxState = newState;
}


HNSS_RESULT_T
HNSCGISinkClient::rxNextParse()
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
    
        // Waiting for start of the header netstring
        case HNSCGI_SS_HDR_NSTR_LEN:
        {   
            result = m_rxQueue.parseNetStrStart( m_expHdrLen );
            
            switch( result )
            {
                case HNSS_RESULT_PARSE_COMPLETE:
                    //printf( "HDR_NSTR Len: %u\n", m_expHdrLen );
                    
                    m_curHdrName.clear();
                    m_curHdrValue.clear();

                    setRxParseState( HNSCGI_SS_HDR_DATA_NAME );
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
        
        // Parsing header data looking for next name
        case HNSCGI_SS_HDR_DATA_NAME:
        {   
            result = m_rxQueue.parseNullStr( m_curHdrName );
    
            switch( result )
            {
                case HNSS_RESULT_PARSE_COMPLETE:
                    //printf( "Name Null Str(%lu): %*.*s\n", m_curHdrName.size(), (int)m_curHdrName.size(), (int)m_curHdrName.size(), m_curHdrName.c_str() );
                    
                    m_rcvHdrLen += ( m_curHdrName.size() + 1 );
               
                    //printf( "Name exp: %u,  rcv: %u\n", m_expHdrLen, m_rcvHdrLen );
     
                    if( m_rcvHdrLen >= m_expHdrLen )
                    {
                        printf( "ERROR: Malformed request header\n" );
                        setRxParseState( HNSCGI_SS_ERROR );
                        return HNSS_RESULT_PARSE_ERR; 
                    }
                    
                    setRxParseState( HNSCGI_SS_HDR_DATA_VALUE );
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
        
        // Parsing header data looking for next value
        case HNSCGI_SS_HDR_DATA_VALUE:
        {
            result = m_rxQueue.parseNullStr( m_curHdrValue );
    
            switch( result )
            {
                case HNSS_RESULT_PARSE_COMPLETE:
                    //printf( "Value Null Str(%lu): %*.*s\n", m_curHdrValue.size(), (int)m_curHdrValue.size(), (int)m_curHdrValue.size(), m_curHdrValue.c_str() );
                    
                    m_curRR.getRequest().addHdrPair( m_curHdrName, m_curHdrValue );
                                        
                    m_rcvHdrLen += ( m_curHdrValue.size() + 1 );
                   
                    //printf( "Value exp: %u,  rcv: %u\n", m_expHdrLen, m_rcvHdrLen );
                    
                    if( m_rcvHdrLen > m_expHdrLen )
                    {
                        printf( "ERROR: Malformed request header\n" );
                        setRxParseState( HNSCGI_SS_ERROR );
                        return HNSS_RESULT_PARSE_ERR; 
                    }
                    else if( m_rcvHdrLen == m_expHdrLen )
                    {
                        setRxParseState( HNSCGI_SS_HDR_NSTR_COMMA );
                        return HNSS_RESULT_PARSE_CONTINUE; 
                    }
                    
                    m_curHdrName.clear();
                    m_curHdrValue.clear();
                    
                    setRxParseState( HNSCGI_SS_HDR_DATA_NAME );
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
        
        // Find and strip off the header netstring trailing comma
        case HNSCGI_SS_HDR_NSTR_COMMA:
        {   
            result = m_rxQueue.parseNetStrEnd();
            
            switch( result )
            {
                case HNSS_RESULT_PARSE_COMPLETE:
                    //printf( "HDR_NSTR End:\n");

                    m_curRR.getRequest().setHeaderDone( true );
                    
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
    
    // Allocate a chunk if needed
    //if( m_curRxChunk == NULL )
    //    m_curRxChunk = new HNSCGIChunk;
        
    // Pull in some data from the socket
    result = m_rxQueue.recvData( m_fd );
    
    if( result != HNSS_RESULT_SUCCESS )
    {
        // No further progress can be made currently.
        return result;
    }
    
    // Attempt parsing until we run out of data,
    // find a complete request, or encounter an error.
    result = HNSS_RESULT_PARSE_CONTINUE;
    while( result == HNSS_RESULT_PARSE_CONTINUE )
    {
        result = rxNextParse();    
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

std::istream&
HNSCGISinkClient::getSourceStreamRef()
{
    return m_iostream;
}

std::ostream&
HNSCGISinkClient::getSinkStreamRef()
{
    return m_iostream;
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

                    HNPRR_RESULT_T status = response->getResponse().sendHeaders();

                    if( status == HNPRR_RESULT_RESPONSE_COMPLETE )
                    {
                        it->second->finish();
                        m_clientMap.erase(it);
                        continue;    
                    }
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


