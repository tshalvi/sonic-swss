#ifndef MONITORTX_ORCH_H
#define MONITORTX_ORCH_H

#include "orch.h"
#include "port.h"
#include "timer.h"
#include "portsorch.h"
#include <array>


class MonitorTxOrch: public Orch
{
public:
    static MonitorTxOrch& getInstance(TableConnector configDbTableConnetctor);
    virtual void doTask(swss::SelectableTimer &timer);
    virtual void doTask(Consumer &consumer);

private:
    std::unique_ptr<swss::Table> m_cfgTxTbl;
	std::shared_ptr<swss::DBConnector> m_countersDb = nullptr;
	std::shared_ptr<swss::Table> m_countersTable = nullptr;
	std::shared_ptr<swss::DBConnector> m_stateDb = nullptr;
	std::shared_ptr<swss::Table> m_stateTable = nullptr;
    uint32_t m_threshold = 51;
    uint32_t m_duration = 15;
    std::unordered_map<std::string, uint32_t> lastTxErrorsPerPortStats;
    SelectableTimer* m_timer = nullptr;

    MonitorTxOrch(TableConnector configDbTableConnetctor);
    virtual ~MonitorTxOrch(void);

    void initCfgTxTbl();

    void updateCurrentStateForPort(string currInterface, uint32_t currValue, std::unordered_map<std::string, std::string>& txState);
    void updateStateDb(std::unordered_map<std::string, std::string>& txState);

    void handleEvent(KeyOpFieldsValuesTuple event);
    void HandleConfigUpdate(vector<FieldValueTuple> fvs);
};

#endif
