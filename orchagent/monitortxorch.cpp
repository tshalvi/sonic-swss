#include "monitortxorch.h"
#include "portsorch.h"
#include "select.h"
#include "notifier.h"
#include "sai_serialize.h"
#include <inttypes.h>
#include <unordered_map>
#include "portsorch.h"


#define POLLING_PERIOD 30
#define OK_STATE "OK"
#define NOT_OK_STATE "NOT OK"

extern sai_port_api_t *sai_port_api;
extern PortsOrch *gPortsOrch;


MonitorTxOrch& MonitorTxOrch::getInstance(DBConnector *db)
{
    SWSS_LOG_ENTER();

    static vector<string> tableNames = {"TX_ERROR_MONITOR"};  // TODO: change to CFG_TX_MONITORS_ARGS_TABLE_NAME
    static MonitorTxOrch *wd = new MonitorTxOrch(db, tableNames);

    return *wd;
}

MonitorTxOrch::MonitorTxOrch(DBConnector *db, vector<string> &tableNames):
    Orch(db, tableNames),
    m_countersDb(new DBConnector("COUNTERS_DB", 0)),
    m_countersTable(new Table(m_countersDb.get(), COUNTERS_TABLE)),
    m_stateDb(new DBConnector("STATE_DB", 0)),
    m_stateTable(new Table(m_stateDb.get(), STATE_TX_ERROR_TABLE_NAME))
{
    SWSS_LOG_ENTER();
    auto interv = timespec { .tv_sec = POLLING_PERIOD, .tv_nsec = 0 };
    auto timer = new SelectableTimer(interv);
    auto executor = new ExecutableTimer(timer, this, "MC_TX_POLL");
    Orch::addExecutor(executor);
    timer->start();
}

MonitorTxOrch::~MonitorTxOrch(void)
{
    SWSS_LOG_ENTER();
}


void MonitorTxOrch::doTask(SelectableTimer &timer)
{
    SWSS_LOG_ENTER();

    std::unordered_map<std::string, std::string> txState;
    std::string txErrorsCounterStr;

    SWSS_LOG_NOTICE("start of func");
    printMap(lastTxErrorsPerPortStats);

    auto allPorts = gPortsOrch->getAllPorts();
    for (auto &it: allPorts)
    {
        Port port = it.second;

        if (!m_countersTable->hget(sai_serialize_object_id(port.m_port_id), "SAI_PORT_STAT_IF_OUT_ERRORS", txErrorsCounterStr))
        {
            SWSS_LOG_NOTICE("COULD NOT FIND PORT'S TX_ERRORS");
            continue;
        }

        SWSS_LOG_NOTICE("after hget!");
        string currInterface = port.m_alias.c_str();
        uint64_t currValue = stoul(txErrorsCounterStr);
        if(currValue - lastTxErrorsPerPortStats[currInterface] > threshold)
            txState[currInterface] = NOT_OK_STATE;
        else
            txState[currInterface] = OK_STATE;

        lastTxErrorsPerPortStats[currInterface] = currValue;
    }
    SWSS_LOG_NOTICE("end of func");
    printMap(lastTxErrorsPerPortStats);
    SWSS_LOG_NOTICE("Results");
    printResults(txState);
    updateStateDb(txState);
}

//void MonitorTxOrch::updateStateDb(std::unordered_map<std::string, std::string> txState){
//
////        std::shared_ptr<swss::DBConnector> state_db = std::make_shared<swss::DBConnector> ("STATE_DB", 0);
//    SWSS_LOG_NOTICE("starting to write to State DB");
//    for (auto const& entry : txState){
//        SWSS_LOG_NOTICE("IN updateStateDb loop");
//        std::string key = entry.first;
//        std::string state = entry.second;
//        m_stateTable->hset(key, "TX_state", state);
//    }
//    SWSS_LOG_NOTICE("Done writing to State DB");
//}

void MonitorTxOrch::updateStateDb(std::unordered_map<std::string, std::string>& txState){
    SWSS_LOG_NOTICE("starting to write to State DB");

    for (auto const& entry : txState){
        SWSS_LOG_NOTICE("IN updateStateDb loop");
        FieldValueTuple tuple(entry.first, entry.second);
        vector<FieldValueTuple> fields;
        fields.push_back(tuple);
        m_stateTable->set("", fields);
        fields.clear();
    }

    SWSS_LOG_NOTICE("Done writing to State DB");
}

void MonitorTxOrch::printMap(std::unordered_map<std::string, uint64_t>& m)
{
    SWSS_LOG_NOTICE("Dictionary content:");
    for (auto const &pair2: m) {
        SWSS_LOG_NOTICE("     {%s ~~~      %" PRIx64 "}\n", pair2.first.c_str(), pair2.second);
    }
}


void MonitorTxOrch::printResults(std::unordered_map<std::string, std::string>& m)
{
    SWSS_LOG_NOTICE("STATE_DB content:");
    for (auto const &pair2: m) {
        SWSS_LOG_NOTICE("     {%s    *~~~*   %s}\n", pair2.first.c_str(), pair2.second.c_str());
    }
}





void doTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();
    SWSS_LOG_NOTICE("INSIDE doTask(Consumer)");

}



















//void MonitorTxOrch::getTxErrorsPerPortStats(std::unordered_map<std::string, uint64_t>& stats){
//    vector<FieldValueTuple> fieldValues;
//    auto allPorts = gPortsOrch->getAllPorts();
//    for (auto &it: allPorts)
//    {
//        Port port = it.second;
//        if (!m_countersTable->get(sai_serialize_object_id(port.m_port_id), fieldValues))
//        {
//            SWSS_LOG_NOTICE("COULD NOT FIND PORT'S TX_ERRORS");
//        }
//
//        for (const auto& fv : fieldValues)
//        {
//            const auto field = fvField(fv);
//            const auto value = fvValue(fv);
//            if (field == "SAI_PORT_STAT_IF_OUT_ERRORS")
//            {
//                stats[sai_serialize_object_id(port.m_port_id).c_str()] = stoul(value);
//            }
//        }
//    }
//
//        printMap();
//}
//
//void MonitorTxOrch::mcCounterCheck()
//{
//    SWSS_LOG_ENTER();
//
//    for (auto& i : m_mcCountersMap)
//    {
//        auto oid = i.first;
//        auto mcCounters = i.second;
//        uint8_t pfcMask = 0;
//
//        Port port;
//        if (!gPortsOrch->getPort(oid, port))
//        {
//            SWSS_LOG_ERROR("Invalid port oid 0x%" PRIx64, oid);
//            continue;
//        }
//
//        auto newMcCounters = getQueueMcCounters(port);
//
//        if (!gPortsOrch->getPortPfc(port.m_port_id, &pfcMask))
//        {
//            SWSS_LOG_ERROR("Failed to get PFC mask on port %s", port.m_alias.c_str());
//            continue;
//        }
//
//        for (size_t prio = 0; prio != mcCounters.size(); prio++)
//        {
//            bool isLossy = ((1 << prio) & pfcMask) == 0;
//            if (newMcCounters[prio] == numeric_limits<uint64_t>::max())
//            {
//                SWSS_LOG_WARN("Could not retreive MC counters on queue %zu port %s",
//                        prio,
//                        port.m_alias.c_str());
//            }
//            else if (!isLossy && mcCounters[prio] < newMcCounters[prio])
//            {
//                SWSS_LOG_WARN("Got Multicast %" PRIu64 " frame(s) on lossless queue %zu port %s",
//                        newMcCounters[prio] - mcCounters[prio],
//                        prio,
//                        port.m_alias.c_str());
//            }
//        }
//
//        i.second= newMcCounters;
//    }
//}
//
//void MonitorTxOrch::pfcFrameCounterCheck()
//{
//    SWSS_LOG_ENTER();
//
//    for (auto& i : m_pfcFrameCountersMap)
//    {
//        auto oid = i.first;
//        auto counters = i.second;
//        auto newCounters = getPfcFrameCounters(oid);
//        uint8_t pfcMask = 0;
//
//        Port port;
//        if (!gPortsOrch->getPort(oid, port))
//        {
//            SWSS_LOG_ERROR("Invalid port oid 0x%" PRIx64, oid);
//            continue;
//        }
//
//        if (!gPortsOrch->getPortPfc(port.m_port_id, &pfcMask))
//        {
//            SWSS_LOG_ERROR("Failed to get PFC mask on port %s", port.m_alias.c_str());
//            continue;
//        }
//
//        for (size_t prio = 0; prio != counters.size(); prio++)
//        {
//            bool isLossy = ((1 << prio) & pfcMask) == 0;
//            if (newCounters[prio] == numeric_limits<uint64_t>::max())
//            {
//                SWSS_LOG_WARN("Could not retreive PFC frame count on queue %zu port %s",
//                        prio,
//                        port.m_alias.c_str());
//            }
//            else if (isLossy && counters[prio] < newCounters[prio])
//            {
//                SWSS_LOG_WARN("Got PFC %" PRIu64 " frame(s) on lossy queue %zu port %s",
//                        newCounters[prio] - counters[prio],
//                        prio,
//                        port.m_alias.c_str());
//            }
//        }
//
//        i.second = newCounters;
//    }
//}
//
//
//PfcFrameCounters MonitorTxOrch::getPfcFrameCounters(sai_object_id_t portId)
//{
//    SWSS_LOG_ENTER();
//
//    vector<FieldValueTuple> fieldValues;
//    PfcFrameCounters counters;
//    counters.fill(numeric_limits<uint64_t>::max());
//
//    static const array<string, PFC_WD_TC_MAX> counterNames =
//    {
//        "SAI_PORT_STAT_PFC_0_RX_PKTS",
//        "SAI_PORT_STAT_PFC_1_RX_PKTS",
//        "SAI_PORT_STAT_PFC_2_RX_PKTS",
//        "SAI_PORT_STAT_PFC_3_RX_PKTS",
//        "SAI_PORT_STAT_PFC_4_RX_PKTS",
//        "SAI_PORT_STAT_PFC_5_RX_PKTS",
//        "SAI_PORT_STAT_PFC_6_RX_PKTS",
//        "SAI_PORT_STAT_PFC_7_RX_PKTS"
//    };
//
//    if (!m_countersTable->get(sai_serialize_object_id(portId), fieldValues))
//    {
//        return counters;
//    }
//
//    for (const auto& fv : fieldValues)
//    {
//        const auto field = fvField(fv);
//        const auto value = fvValue(fv);
//
//
//        for (size_t prio = 0; prio != counterNames.size(); prio++)
//        {
//            if (field == counterNames[prio])
//            {
//                counters[prio] = stoul(value);
//            }
//        }
//    }
//
//    return counters;
//}
//
//QueueMcCounters MonitorTxOrch::getQueueMcCounters(
//        const Port& port)
//{
//    SWSS_LOG_ENTER();
//
//    vector<FieldValueTuple> fieldValues;
//    QueueMcCounters counters;
//
//    for (uint8_t prio = 0; prio < port.m_queue_ids.size(); prio++)
//    {
//        sai_object_id_t queueId = port.m_queue_ids[prio];
//        auto queueIdStr = sai_serialize_object_id(queueId);
//        auto queueType = m_countersDb->hget(COUNTERS_QUEUE_TYPE_MAP, queueIdStr);
//
//        if (queueType.get() == nullptr || *queueType != "SAI_QUEUE_TYPE_MULTICAST" || !m_countersTable->get(queueIdStr, fieldValues))
//        {
//            continue;
//        }
//
//        uint64_t pkts = numeric_limits<uint64_t>::max();
//        for (const auto& fv : fieldValues)
//        {
//            const auto field = fvField(fv);
//            const auto value = fvValue(fv);
//
//            if (field == "SAI_QUEUE_STAT_PACKETS")
//            {
//                pkts = stoul(value);
//            }
//        }
//        counters.push_back(pkts);
//    }
//
//    return counters;
//}
//
//void MonitorTxOrch::addPort(const Port& port)
//{
//    m_mcCountersMap.emplace(port.m_port_id, getQueueMcCounters(port));
//    m_pfcFrameCountersMap.emplace(port.m_port_id, getPfcFrameCounters(port.m_port_id));
//}
//
//void MonitorTxOrch::removePort(const Port& port)
//{
//    m_mcCountersMap.erase(port.m_port_id);
//    m_pfcFrameCountersMap.erase(port.m_port_id);
//}
