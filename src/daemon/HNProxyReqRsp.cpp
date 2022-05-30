#include <iostream>
#include <sstream>

#include "HNProxyReqRsp.h"

HNProxyHTTPMsg::HNProxyHTTPMsg()
{
    m_headerComplete = false;
    m_dispatched = false;

    m_cSource = NULL;
    m_cSink = NULL;
}

HNProxyHTTPMsg::~HNProxyHTTPMsg()
{

}

void 
HNProxyHTTPMsg::clearHeaders()
{
    m_statusCode = 0;
    
    m_contentType.clear();

    m_contentLength = 0;
    m_contentMoved  = 0;

    m_paramMap.clear();
}

void
HNProxyHTTPMsg::addHdrPair( std::string name, std::string value )
{
    printf( "addHdrPair - name: %s,  value: %s\n", name.c_str(), value.c_str() );
    
    // Special case certain headers
    if( Poco::icompare( name, "Content-Length" ) == 0 )
    {
        m_contentLength = strtol( value.c_str(), NULL, 0);
        return;
    }
    else if( Poco::icompare( name, "CONTENT_LENGTH" ) == 0 )
    {
        m_contentLength = strtol( value.c_str(), NULL, 0);
        return;
    }
    else if( Poco::icompare( name, "Status") == 0 )
    {
        m_statusCode = strtol( value.c_str(), NULL, 0);
        return;
    }
    else if( Poco::icompare( name, "Content-Type") == 0 )
    {
        m_contentType = value;
        return;
    }
    else if( Poco::icompare( name, "CONTENT_TYPE") == 0 )
    {
        m_contentType = value;
        return;
    }
    else if( Poco::icompare( name, "REQUEST_URI") == 0 )
    {
        m_uri = value;
        return;
    }
    else if( Poco::icompare( name, "REQUEST_METHOD") == 0 )
    {
        m_method = value;
        return;
    }

    m_paramMap.insert( std::pair<std::string, std::string>(name, value) );
}

void 
HNProxyHTTPMsg::buildExtraHeaders( std::ostream &outStream )
{
   // Add other extra headers
   for( std::map< std::string, std::string >::iterator hit = m_paramMap.begin(); hit != m_paramMap.end(); hit++ )
   {
       outStream << hit->first << ": " << hit->second << "\r\n";
   }
}

const std::string& 
HNProxyHTTPMsg::getURI() const
{
    return m_uri;
}

const std::string& 
HNProxyHTTPMsg::getMethod() const
{
    return m_method;
}

void 
HNProxyHTTPMsg::setDispatched( bool value )
{
    m_dispatched = value;
}

void 
HNProxyHTTPMsg::setHeaderDone( bool value )
{
    m_headerComplete = value;
}

bool 
HNProxyHTTPMsg::isHeaderDone()
{
    return m_headerComplete;
}

bool 
HNProxyHTTPMsg::isDispatched()
{
    return m_dispatched;
}

void 
HNProxyHTTPMsg::setStatusCode( uint statusCode )
{
    m_statusCode = statusCode;
}

void 
HNProxyHTTPMsg::setReason( std::string reason )
{
    m_reason = reason;
}

void 
HNProxyHTTPMsg::setContentType( std::string typeStr )
{
    m_contentType = typeStr;
}

void 
HNProxyHTTPMsg::setContentLength( uint length )
{
    m_contentLength = length;
    m_contentMoved  = 0;
}

void 
HNProxyHTTPMsg::configAsNotImplemented()
{
    clearHeaders();

    setStatusCode( 501 );
    setReason("Not Implemented");
    setContentLength( 0 );
}

void 
HNProxyHTTPMsg::configAsNotFound()
{
    clearHeaders();

    setStatusCode( 404 );
    setReason("Not Found");
    setContentLength( 0 );
}

void 
HNProxyHTTPMsg::configAsInternalServerError()
{
    clearHeaders();

    setStatusCode( 500 );
    setReason("Internal Server Error");
    setContentLength( 0 );
}

uint 
HNProxyHTTPMsg::getStatusCode()
{
    return m_statusCode;
}

std::string 
HNProxyHTTPMsg::getReason()
{
    return m_reason;
}

std::string 
HNProxyHTTPMsg::getContentType()
{
    return m_contentType;
}

uint 
HNProxyHTTPMsg::getContentLength()
{
    return m_contentLength;
}

HNPRR_RESULT_T 
HNProxyHTTPMsg::sendHeaders()
{
    std::cout << "sendHeaders - 1" << std::endl;

    // Get the output stream reference
    if( m_cSink == NULL )
        return HNPRR_RESULT_FAILURE;

    std::cout << "sendHeaders - 2" << std::endl;

    std::ostream &outStream = m_cSink->getSinkStreamRef();

    // Add the Status header
    outStream << "Status: " << getStatusCode() << " " << getReason() << "\r\n";

    // Add Content-Length header
    uint contentLength = getContentLength();
    outStream << "Content-Length: " << contentLength << "\r\n";
   
    // See if we need the Content-Type header
    if( contentLength != 0 )
    {
        outStream << "Content-Type: " << getContentType() << "\r\n";
    }

    // Add any additional headers
    buildExtraHeaders( outStream );   
 
    // Add a blank line to mark the end of the headers
    outStream << "\r\n";

    outStream.flush();

    //sprintf( rtnBuf, "Status: 200 OK\r\nContent-Type: text/plain\r\n\r\n42" );

    //ssize_t bytesWritten = send( m_fd, rtnBuf, strlen(rtnBuf), 0 );
 
    //printf( "HNSCGISinkClient::sendData - bytesWritten: %lu\n", bytesWritten );
    
    return contentLength ? HNPRR_RESULT_RESPONSE_CONTENT : HNPRR_RESULT_RESPONSE_COMPLETE;
}

void 
HNProxyHTTPMsg::setContentSource( HNPRRContentSource *source )
{
    m_cSource = source;
}

void 
HNProxyHTTPMsg::setContentSink( HNPRRContentSink *sink )
{
    m_cSink = sink;
}

std::ostream&
HNProxyHTTPMsg::useLocalContentSource()
{
    m_localContent.clear();
    setContentSource( this );
    return m_localContent; 
}

void
HNProxyHTTPMsg::finalizeLocalContent()
{
    uint size = m_localContent.tellp();
    std::cout << "finalizeLocalContent: " << size << std::endl;
    setContentLength(size);
}

HNPRR_RESULT_T 
HNProxyHTTPMsg::xferContentChunk( uint maxChunkLength )
{
    char buff[4096];

    if( (m_cSource == NULL) || (m_cSink == NULL) )
        return HNPRR_RESULT_FAILURE;

    if( m_contentMoved >= m_contentLength )
        return HNPRR_RESULT_RESPONSE_COMPLETE;

    std::istream &is = m_cSource->getSourceStreamRef();
    std::ostream &os = m_cSink->getSinkStreamRef();

    uint bytesToMove = m_contentLength - m_contentMoved;
    if( bytesToMove > sizeof(buff) )
        bytesToMove = sizeof(buff);

    is.read( buff, bytesToMove );
    os.write( buff, bytesToMove );
 
    std::cout << "xferContentChunk - bytesToMove: " << bytesToMove << std::endl;

    m_contentMoved += bytesToMove;

    if( m_contentMoved < m_contentLength )
        return HNPRR_RESULT_RESPONSE_CONTENT;
 
    os.flush();

    return HNPRR_RESULT_RESPONSE_COMPLETE;
}

std::istream& 
HNProxyHTTPMsg::getSourceStreamRef()
{
    return m_localContent;
}

std::ostream& 
HNProxyHTTPMsg::getSinkStreamRef()
{
    return m_localContent;
}

void 
HNProxyHTTPMsg::debugPrint()
{
    std::cout << "==== Proxy Request ====" << std::endl;
    std::cout << "== Header Parameter List ==" << std::endl;

    for( std::map< std::string, std::string >::iterator mip = m_paramMap.begin(); mip != m_paramMap.end(); mip++ )
    {
        std::cout << mip->first << " :    " << mip->second << std::endl;
    }
}


HNProxyHTTPReqRsp::HNProxyHTTPReqRsp( uint parentTag )
{
    m_parentTag = parentTag;
}

HNProxyHTTPReqRsp::~HNProxyHTTPReqRsp()
{

}

HNProxyHTTPMsg &
HNProxyHTTPReqRsp::getRequest()
{
    return m_request;
}

HNProxyHTTPMsg &
HNProxyHTTPReqRsp::getResponse()
{
    return m_response;
}

uint 
HNProxyHTTPReqRsp::getParentTag()
{
    return m_parentTag;
}

