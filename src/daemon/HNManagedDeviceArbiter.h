#ifndef _HN_MANAGED_DEVICE_ARBITER_H_
#define _HN_MANAGED_DEVICE_ARBITER_H_

#include <string>
#include <map>

#include "hnode2/HNodeID.h"

typedef enum HNManagedDeviceRecordDiscoveryStateEnum
{
    HNMDR_DISC_STATE_NOTSET
}HNMDR_DISC_STATE_T;

typedef enum HNManagedDeviceRecordOwnerStateEnum
{
    HNMDR_OWNER_STATE_NOTSET
}HNMDR_OWNER_STATE_T;

typedef enum HNManagedDeviceListResultEnum
{
    HNMDL_RESULT_SUCCESS,
    HNMDL_RESULT_FAILURE
}HNMDL_RESULT_T;

class HNManagedDeviceRecord
{
    private:
        HNMDR_DISC_STATE_T   discoveryState;
        HNMDR_OWNER_STATE_T  ownershipState;

        HNodeID     hnodeID;
        std::string name;
        std::string baseURL;
        std::string version;

    public:
        HNManagedDeviceRecord();
       ~HNManagedDeviceRecord();

};

class HNManagedDeviceArbiter
{
    private:

       // A map of known hnode2 devices
       std::map< std::string, HNManagedDeviceRecord > mdrMap;

    public:
        HNManagedDeviceArbiter();
       ~HNManagedDeviceArbiter();

};

#endif // _HN_MANAGED_DEVICE_ARBITER_H_
