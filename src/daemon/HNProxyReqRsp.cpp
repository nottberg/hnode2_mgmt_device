#include "HNProxyReqRsp.h"

HNProxyRequest::HNProxyRequest()
{
    m_headerComplete = false;
    m_dispatched = false;
}

HNProxyRequest::~HNProxyRequest()
{

}

void
HNProxyRequest::addHdrPair( std::string name, std::string value )
{
    printf( "addHdrPair - name: %s,  value: %s\n", name.c_str(), value.c_str() );
    
    m_paramMap.insert( std::pair<std::string, std::string>(name, value) );
}

const std::string& 
HNProxyRequest::getURI() const
{
    return m_uri;
}

const std::string& 
HNProxyRequest::getMethod() const
{
    return m_method;
}

void 
HNProxyRequest::setDispatched( bool value )
{
    m_dispatched = value;
}

void 
HNProxyRequest::setHeaderDone( bool value )
{
    m_headerComplete = value;
}

bool 
HNProxyRequest::isHeaderDone()
{
    return m_headerComplete;
}

bool 
HNProxyRequest::isDispatched()
{
    return m_dispatched;
}

HNProxyResponse::HNProxyResponse()
{

}

HNProxyResponse::~HNProxyResponse()
{

}

