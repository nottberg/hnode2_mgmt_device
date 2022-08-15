#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <grp.h>

#include <syslog.h>

#include <iostream>
#include <sstream>

#include <Poco/String.h>
#include <Poco/Thread.h>
#include <Poco/Runnable.h>

#include "HNSCGISink.h"

#define MAXEVENTS  8

HNSCGIMsg::HNSCGIMsg()
{
    m_headerComplete = false;
    m_dispatched = false;

    m_cSource = NULL;
    m_cSink = NULL;

    m_contentMoved = 0;
}

HNSCGIMsg::~HNSCGIMsg()
{

}

void 
HNSCGIMsg::clearHeaders()
{
    m_statusCode = 0;
    m_reason.clear();

    m_contentMoved  = 0;

    m_paramMap.clear();
}

void
HNSCGIMsg::addSCGIRequestHeader( std::string name, std::string value )
{
    // Special handling for headers orginating from the SCGI layer
    // The original CGI interface passed headers via ENV variables 
    // which meant some of the common headers are passed with all
    // capital underscore instead of dash versions of the names.
    // Here segregate that style of header into a seperate list and
    // translate some of them into the more common format.
    if( Poco::icompare( name, "SCGI" ) == 0 )
    {
        // Header is the special SCGI header
        printf( "addCGIVarPair - name: %s,  value: %s\n", name.c_str(), value.c_str() );
        m_cgiVarMap.insert( std::pair<std::string, std::string>(name, value) );
        return;
    }
    else if( name.find('_') != std::string::npos )
    {
        // Header contains an underscore so add it to CGI variable map.
        printf( "addCGIVarPair - name: %s,  value: %s\n", name.c_str(), value.c_str() );
        m_cgiVarMap.insert( std::pair<std::string, std::string>(name, value) );

        // Do some special handling for some of the CGI values.
        if( Poco::icompare( name, "CONTENT_LENGTH" ) == 0 )
        {
            addHdrPair( "Content-Length", value );
            return;
        }
        else if( Poco::icompare( name, "CONTENT_TYPE") == 0 )
        {
            if( value.empty() == false )
                addHdrPair( "Content-Type", value );

            return;
        }
        else if( Poco::icompare( name, "REQUEST_URI") == 0 )
        {
            setURI( value );
            return;
        }
        else if( Poco::icompare( name, "REQUEST_METHOD") == 0 )
        {
            setMethod( value );
            return;
        }

        // Done processing this header
        return;
    }

    // Not a CGI parameter so add as a normal header
    addHdrPair( name, value );
}

void
HNSCGIMsg::addHdrPair( std::string name, std::string value )
{
    printf( "addHdrPair - name: %s,  value: %s\n", name.c_str(), value.c_str() );
    
    // Handle content length specially so that we always get constant capilization. 
    if( Poco::icompare( name, "Content-Length" ) == 0 )
    {
        m_paramMap.insert(std::pair< std::string, std::string >( "Content-Length", value ) );
        return;
    }

    m_paramMap.insert( std::pair<std::string, std::string>(name, value) );
}

bool 
HNSCGIMsg::hasHeader( std::string name )
{
    std::map< std::string, std::string >::iterator it = m_paramMap.find( name ); 

    if( it == m_paramMap.end() )
        return false;

    return true;
}

const std::string& 
HNSCGIMsg::getURI() const
{
    return m_uri;
}

const std::string& 
HNSCGIMsg::getMethod() const
{
    return m_method;
}

void 
HNSCGIMsg::setDispatched( bool value )
{
    m_dispatched = value;
}

void 
HNSCGIMsg::setHeaderDone( bool value )
{
    m_headerComplete = value;
}

bool 
HNSCGIMsg::isHeaderDone()
{
    return m_headerComplete;
}

bool 
HNSCGIMsg::isDispatched()
{
    return m_dispatched;
}

void 
HNSCGIMsg::setURI( std::string uri )
{
    m_uri = uri;
}

void 
HNSCGIMsg::setMethod( std::string method )
{
    m_method = method;
}

void 
HNSCGIMsg::setStatusCode( uint statusCode )
{
    m_statusCode = statusCode;
}

void 
HNSCGIMsg::setReason( std::string reason )
{
    m_reason = reason;
}

void 
HNSCGIMsg::setContentLength( uint length )
{
    char tmpBuf[64];
    sprintf(tmpBuf, "%u", length);
    m_paramMap.insert( std::pair<std::string, std::string>("Content-Length", tmpBuf) );
}

void 
HNSCGIMsg::setContentType( std::string typeStr )
{
    m_paramMap.insert( std::pair<std::string, std::string>("Content-Type", typeStr) );
}

void 
HNSCGIMsg::configAsNotImplemented()
{
    clearHeaders();

    setStatusCode( 501 );
    setReason("Not Implemented");
    setContentLength( 0 );
}

void 
HNSCGIMsg::configAsNotFound()
{
    clearHeaders();

    setStatusCode( 404 );
    setReason("Not Found");
    setContentLength( 0 );
}

void 
HNSCGIMsg::configAsInternalServerError()
{
    clearHeaders();

    setStatusCode( 500 );
    setReason("Internal Server Error");
    setContentLength( 0 );
}

uint 
HNSCGIMsg::getStatusCode()
{
    return m_statusCode;
}

std::string 
HNSCGIMsg::getReason()
{
    return m_reason;
}

uint 
HNSCGIMsg::getContentLength()
{
    std::map< std::string, std::string >::iterator it = m_paramMap.find( "Content-Length" ); 

    if( it == m_paramMap.end() )
        return 0;

    return strtol( it->second.c_str(), NULL, 0);
}

HNSS_RESULT_T 
HNSCGIMsg::sendSCGIResponseHeaders()
{
    std::cout << "SCGIResponseHeaders - 1" << std::endl;

    // Get the output stream reference
    if( m_cSink == NULL )
        return HNSS_RESULT_FAILURE;

    std::cout << "SCGIResponseHeaders - 2 - Status: " << getStatusCode() << "  Reason: " << getReason() << std::endl;

    std::ostream *outStream = m_cSink->getSinkStreamRef();

    // Add the Status header
    *outStream << "Status: " << getStatusCode() << " " << getReason() << "\r\n";

    // Output other headers    
    for( std::map< std::string, std::string >::iterator hit = m_paramMap.begin(); hit != m_paramMap.end(); hit++ )
    {
        std::cout << "SCGIResponseHeaders - 3 - Name: " << hit->first << "  Value: " << hit->second << std::endl;
        *outStream << hit->first << ": " << hit->second << "\r\n";
    }

    // Add a blank line to mark the end of the headers
    *outStream << "\r\n";

    // Push this first bit to the server.
    outStream->flush();

    std::cout << "SCGIResponseHeaders - 4 - eof: " << outStream->eof() << "  fail: " << outStream->fail() << "  bad: " << outStream->bad() << std::endl;

    // Move to content send    
    return HNSS_RESULT_MSG_CONTENT;
}

void 
HNSCGIMsg::setContentSource( HNPRRContentSource *source )
{
    m_cSource = source;
}

void 
HNSCGIMsg::setContentSink( HNPRRContentSink *sink )
{
    m_cSink = sink;
}

std::ostream&
HNSCGIMsg::useLocalContentSource()
{
    m_localContent.clear();
    setContentSource( this );
    return m_localContent; 
}

void
HNSCGIMsg::finalizeLocalContent()
{
    uint size = m_localContent.tellp();
    std::cout << "finalizeLocalContent: " << size << std::endl;
    m_contentMoved = 0;
    setContentLength(size);
}

void 
HNSCGIMsg::readContentToLocal()
{
    m_localContent.clear();
    setContentSink(this);

    HNSS_RESULT_T status = HNSS_RESULT_MSG_CONTENT;
    while( status == HNSS_RESULT_MSG_CONTENT )
    {
        status = xferContentChunk( 4096 );
    }
}

std::istream& 
HNSCGIMsg::getLocalInputStream()
{
    return m_localContent;
}

HNSS_RESULT_T 
HNSCGIMsg::xferContentChunk( uint maxChunkLength )
{
    char buff[4096];
    uint contentLength = 0;

    if( (m_cSource == NULL) || (m_cSink == NULL) )
        return HNSS_RESULT_FAILURE;

    // If content length is 0, don't send anything
    if( hasHeader( "Content-Length" ) == true )
    {
        contentLength = getContentLength();

        if( contentLength == 0 )
        {
           std::cout << "xferContentChunk - No content" << std::endl;
            return HNSS_RESULT_MSG_COMPLETE;
        }
    }

    std::istream *is = m_cSource->getSourceStreamRef();
    std::ostream *os = m_cSink->getSinkStreamRef();

    if( hasHeader( "Content-Length" ) == true )
    {
        if( m_contentMoved >= contentLength )
            return HNSS_RESULT_MSG_COMPLETE;

        uint bytesToMove = contentLength - m_contentMoved;
        if( bytesToMove > sizeof(buff) )
            bytesToMove = sizeof(buff);

        is->read( buff, bytesToMove );
        std::streamsize bytesRead = is->gcount();
        os->write( buff, bytesRead );

        m_contentMoved += bytesRead;

        std::cout << "xferContentChunk - bytesMoved: " << bytesRead << "  totalMoved: " << m_contentMoved << "  contentLength: " << contentLength << std::endl;
        std::cout << "xferContentChunk - is - eof: " << is->eof() << "  fail: " << is->fail() << "  bad: " << is->bad() << std::endl;
        std::cout << "xferContentChunk - os - eof: " << os->eof() << "  fail: " << os->fail() << "  bad: " << os->bad() << std::endl;

        if( (m_contentMoved < contentLength) && (is->eof() == false) )
            return HNSS_RESULT_MSG_CONTENT;
    }
    else
    {
        uint bytesToMove = sizeof(buff);

        is->read( buff, bytesToMove );
        std::streamsize bytesRead = is->gcount();
        os->write( buff, bytesRead );

        m_contentMoved += bytesRead;

        std::cout << "xferContentChunk - bytesMoved: " << bytesRead << "  totalMoved: " << m_contentMoved << std::endl;
        std::cout << "xferContentChunk - is - eof: " << is->eof() << "  fail: " << is->fail() << "  bad: " << is->bad() << std::endl;
        std::cout << "xferContentChunk - os - eof: " << os->eof() << "  fail: " << os->fail() << "  bad: " << os->bad() << std::endl;

        if( is->eof() == false )
            return HNSS_RESULT_MSG_CONTENT;
    }
    
    // Push everything to server before closing out.
    os->flush();

    return HNSS_RESULT_MSG_COMPLETE;
}

std::istream* 
HNSCGIMsg::getSourceStreamRef()
{
    return &m_localContent;
}

std::ostream* 
HNSCGIMsg::getSinkStreamRef()
{
    std::cout << "HNSCGIMsg::getSinkStreamRef - m_localContent" << std::endl;
    return &m_localContent;
}

void 
HNSCGIMsg::debugPrint()
{
    std::cout << "==== Proxy Request ====" << std::endl;
    std::cout << "== Header Parameter List ==" << std::endl;

    for( std::map< std::string, std::string >::iterator mip = m_paramMap.begin(); mip != m_paramMap.end(); mip++ )
    {
        std::cout << mip->first << " :    " << mip->second << std::endl;
    }
}

HNSCGIRR::HNSCGIRR( uint fd, HNSCGISink *parent )
: m_ofilebuf( fd, (std::ios::out|std::ios::binary) ), m_ifilebuf( fd, (std::ios::in|std::ios::binary) ), m_ostream( &m_ofilebuf ), m_istream( &m_ifilebuf )
{
    m_fd      = fd;

    m_parent  = parent;
    m_rxState = HNSCGI_SS_IDLE;

    m_request.setContentSource( this );
    m_response.setContentSink( this );
}

HNSCGIRR::~HNSCGIRR()
{
    for( std::vector< std::pair< SHUTDOWN_CALL_FNPTR_T, void* > >::iterator it = m_shutdownCallList.begin(); it != m_shutdownCallList.end(); it++)
    {
        if( it->first != NULL )
            it->first( it->second );
    }
}       

uint 
HNSCGIRR::getSCGIFD()
{
    return m_fd;
}

HNSCGIMsg&
HNSCGIRR::getReqMsg()
{
    return m_request;
}

HNSCGIMsg&
HNSCGIRR::getRspMsg()
{
    return m_response;
}

void
HNSCGIRR::setRxParseState( HNSC_SS_T newState )
{
    printf( "setRxParseState - newState: %u\n", newState );

    //m_rxQueue.resetParseState();
    m_rxState = newState;
}

HNSS_RESULT_T 
HNSCGIRR::readNetStrStart()
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
HNSCGIRR::fillRequestHeaderBuffer()
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
HNSCGIRR::extractHeaderPairsFromBuffer()
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
                m_request.addSCGIRequestHeader( curHdrName, curHdrValue );

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
HNSCGIRR::consumeNetStrComma()
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
HNSCGIRR::readRequestHeaders()
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
                    m_request.setHeaderDone( true );

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
HNSCGIRR::recvData()
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
            m_request.debugPrint();
            return HNSS_RESULT_REQUEST_READY;
        }

    }

    return HNSS_RESULT_SUCCESS;
}

std::istream*
HNSCGIRR::getSourceStreamRef()
{
    return &m_istream;
}

std::ostream*
HNSCGIRR::getSinkStreamRef()
{
    std::cout << "HNSCGIRR::getSinkStreamRef - m_osteam, m_fd: " << m_fd << std::endl;
    return &m_ostream;
}

void
HNSCGIRR::addShutdownCall( SHUTDOWN_CALL_FNPTR_T funcPtr, void *objAddr )
{
    m_shutdownCallList.push_back( std::pair< SHUTDOWN_CALL_FNPTR_T, void* >( funcPtr, objAddr ) );
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
                    HNSCGIRR *response = (HNSCGIRR *) m_proxyResponseQueue.aquireRecord();

                    std::map< int, HNSCGIRR* >::iterator it = m_rrMap.find( response->getSCGIFD() );
                    if( it == m_rrMap.end() )
                    {
                        syslog( LOG_ERR, "ERROR: Could not find client record - sfd: %d", response->getSCGIFD() );
                        //return HNSS_RESULT_FAILURE;
                    }
    
                    std::cout << "HNSCGISink::Received proxy response" << std::endl;

                    HNSS_RESULT_T status = response->getRspMsg().sendSCGIResponseHeaders();

                    while( status == HNSS_RESULT_MSG_CONTENT )
                    {
                        status = response->getRspMsg().xferContentChunk( 4096 );
                    }

                    closeClientConnection( response->getSCGIFD() );
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
    char   str[512];
    struct group *grp;

    // Clear address structure - UNIX domain addressing
    // addr.sun_path[0] cleared to 0 by memset() 
    memset( &addr, 0, sizeof(struct sockaddr_un) );  
    addr.sun_family = AF_UNIX;                     

    // Socket with name /var/run/hnode2-scgi-<instanceName>.sock
    sprintf( str, "/var/run/hnode2-scgi-%s.sock", m_instanceName.c_str() );
    strncpy( &addr.sun_path[0], str, strlen(str) );

    // Since the socket is bound to a fs path, try a unlink first to clean up any leftovers.
    unlink( str );
    
    // Attempt to create the new unix socket.
    m_acceptFD = socket( AF_UNIX, SOCK_STREAM, 0 );
    if( m_acceptFD == -1 )
    {
        printf( "Opening daemon listening socket failed (%s).", strerror(errno) );
        return HNSS_RESULT_FAILURE;
    }

    // Bind it to the path in the filesystem
    if( bind( m_acceptFD, (struct sockaddr *) &addr, sizeof( sa_family_t ) + strlen( str ) + 1 ) == -1 )
    {
        printf( "Failed to bind socket to @%s (%s).", str, strerror(errno) );
        return HNSS_RESULT_FAILURE;
    }

    // Change the ownership and permissions to allow the front end server
    // to connect to the unix-domain socket.
    grp = getgrnam("www-data");
    if( grp == NULL ) 
    {
        std::cout << "ERROR: Failed to get gid for 'www-data'" << std::endl;
        return HNSS_RESULT_FAILURE;
    }

    if( chown( str, ((uid_t)-1), grp->gr_gid ) < 0 ) 
    {
        std::cout << "ERROR: Could not set group of unix domain socket: " << str << std::endl;
        return HNSS_RESULT_FAILURE;
    }

    if( chmod( str, (S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IWGRP|S_IXGRP) ) < 0 ) 
    {
        std::cout << "ERROR: Could not set permissions for unix domain socket: " << str << std::endl;
        return HNSS_RESULT_FAILURE;
    }

    // Accept connections.
    if( listen( m_acceptFD, 4 ) == -1 )
    {
        printf( "Failed to listen on socket for @%s (%s).", str, strerror(errno) );
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

        HNSCGIRR *client = new HNSCGIRR( infd, this );
        std::cout << "Allocated new HNSCGIRR: " << client << std::endl;

        m_rrMap.insert( std::pair< int, HNSCGIRR* >( infd, client ) );

        addSocketToEPoll( infd );
    }

    return HNSS_RESULT_SUCCESS;
}

                    
HNSS_RESULT_T
HNSCGISink::closeClientConnection( int clientFD )
{
    std::map< int, HNSCGIRR* >::iterator cit = m_rrMap.find( clientFD );
    if( cit == m_rrMap.end() )
        return HNSS_RESULT_FAILURE;

    removeSocketFromEPoll( clientFD );

    close( clientFD );

    printf( "Closed client - sfd: %d\n", clientFD );

    HNSCGIRR *client = cit->second;
    m_rrMap.erase( cit );

    std::cout << "Delete HNSCGIRR: " << client << std::endl;
    delete client;
    
    return HNSS_RESULT_SUCCESS;
}

HNSS_RESULT_T
HNSCGISink::processClientRequest( int cfd )
{
    HNSS_RESULT_T result;

    syslog( LOG_ERR, "Process client data - sfd: %d", cfd );
    
    // Find the client record
    std::map< int, HNSCGIRR* >::iterator it = m_rrMap.find( cfd );
    if( it == m_rrMap.end() )
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
            queueProxyRequest( it->second );
            return HNSS_RESULT_SUCCESS;
        }
        break;

    }

    return HNSS_RESULT_SUCCESS;
}

void 
HNSCGISink::queueProxyRequest( HNSCGIRR *reqPtr )
{
    if( m_parentRequestQueue == NULL )
        return;

    m_parentRequestQueue->postRecord( reqPtr );
}
