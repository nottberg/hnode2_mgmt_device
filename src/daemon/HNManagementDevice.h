#ifndef __HN_MANAGEMENT_DEVICE_H__
#define __HN_MANAGEMENT_DEVICE_H__

#include <string>
#include <vector>
#include <set>

#include "Poco/Util/ServerApplication.h"
#include "Poco/Util/OptionSet.h"

#include "hnode2/HNAvahiBrowser.h"

#include "HNSCGISink.h"
#include "HNManagedDeviceArbiter.h"

#define MAXEVENTS  8

typedef enum HNManagementDeviceResultEnum
{
  HNMD_RESULT_SUCCESS,
  HNMD_RESULT_FAILURE
}HNMD_RESULT_T;

class HNManagementDevice : public Poco::Util::ServerApplication
{
    private:
        bool _helpRequested   = false;
        bool _debugLogging    = false;
        bool _instancePresent = false;

        std::string _instance; 

        std::string instanceName;

        int epollFD;
    
        struct epoll_event event;
        struct epoll_event *events;

        HNManagedDeviceArbiter arbiter;
        HNSCGISink             reqsink;
        
        bool quit;

        void displayHelp();

        HNMD_RESULT_T addSocketToEPoll( int sfd );
        HNMD_RESULT_T removeSocketFromEPoll( int sfd );

    protected:
        void defineOptions( Poco::Util::OptionSet& options );
        void handleOption( const std::string& name, const std::string& value );
        int main( const std::vector<std::string>& args );

};

#endif // __HN_MANAGEMENT_DEVICE_H__
