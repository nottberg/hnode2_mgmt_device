#ifndef _HN_MANAGED_DEVICE_ARBITER_H_
#define _HN_MANAGED_DEVICE_ARBITER_H_

#include <string>
#include <map>

#include "hnode2/HNodeID.h"

// Forward declaration for friend class below
class HNMDARunner;

typedef enum HNManagedDeviceRecordDiscoveryStateEnum
{
    HNMDR_DISC_STATE_NOTSET,
    HNMDR_DISC_STATE_NEW
}HNMDR_DISC_STATE_T;

typedef enum HNManagedDeviceRecordOwnerStateEnum
{
    HNMDR_OWNER_STATE_NOTSET,
    HNMDR_OWNER_STATE_UNKNOWN,
}HNMDR_OWNER_STATE_T;

typedef enum HNManagedDeviceListResultEnum
{
    HNMDL_RESULT_SUCCESS,
    HNMDL_RESULT_FAILURE
}HNMDL_RESULT_T;

class HNMDARecord
{
    private:
        HNMDR_DISC_STATE_T   discoveryState;
        HNMDR_OWNER_STATE_T  ownershipState;

        std::string discID;
        HNodeID     hnodeID;
        std::string devType;
        std::string devVersion;
        std::string name;

        std::string baseIPv4URL;
        std::string baseIPv6URL;
        std::string baseSelfURL;

    public:
        HNMDARecord();
       ~HNMDARecord();

        void setDiscoveryState( HNMDR_DISC_STATE_T value );
        void setOwnershipState( HNMDR_OWNER_STATE_T value );

        void setDiscoveryID( std::string value );
        void setDeviceType( std::string value );
        void setDeviceVersion( std::string value );
        void setHNodeIDFromStr( std::string value );
        void setName( std::string value );

        void setBaseIPv4URL( std::string value );
        void setBaseIPv6URL( std::string value );
        void setBaseSelfURL( std::string value );
      
        HNMDR_DISC_STATE_T  getDiscoveryState();
        std::string getDiscoveryStateStr();

        HNMDR_OWNER_STATE_T getOwnershipState();
        std::string getOwnershipStateStr();

        std::string getDiscoveryID();
        std::string getDeviceType();
        std::string getDeviceVersion();
        std::string getHNodeIDStr();
        std::string getName();
        std::string getBaseIPv4URL();
        std::string getBaseIPv6URL();
        std::string getBaseSelfURL();
        std::string getCRC32ID();

        void debugPrint( uint offset );
};

class HNManagedDeviceArbiter
{
    private:

        // A map of known hnode2 devices
        std::map< std::string, HNMDARecord > mdrMap;

        // The thread helper
        void *thelp;

        // Should the monitor still be running.
        bool runMonitor;

    protected:
        void runMonitoringLoop();
        void killMonitoringLoop();

    public:
        HNManagedDeviceArbiter();
       ~HNManagedDeviceArbiter();

        HNMDL_RESULT_T notifyDiscoverAdd( HNMDARecord &record );
        HNMDL_RESULT_T notifyDiscoverRemove( HNMDARecord &record );

        void start();
        void shutdown();

        void debugPrint();

    friend HNMDARunner;
};

#endif // _HN_MANAGED_DEVICE_ARBITER_H_
