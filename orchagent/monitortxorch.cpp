#include "monitortxorch.h"
#include "select.h"
#include <inttypes.h>
#include <unordered_map>
#include "converter.h"

extern PortsOrch *gPortsOrch;

#define TX_ERRORS_COUNTER_NAME "SAI_PORT_STAT_IF_OUT_ERRORS"

#define TX_ERRORS_OK_STATE "OK"
#define TX_ERRORS_NOT_OK_STATE "NOT OK"

#define CONFIG_KEY_NAME "config"
#define THRESHOLD_FIELD_NAME "threshold"
#define DURATION_FIELD_NAME "duration"



MonitorTxOrch& MonitorTxOrch::getInstance(TableConnector TxErrorsMonitorTableConnetctor)
{
    SWSS_LOG_ENTER();
    static MonitorTxOrch *monitorTxOrch = new MonitorTxOrch(TxErrorsMonitorTableConnetctor);
    return *monitorTxOrch;
}


MonitorTxOrch::MonitorTxOrch(TableConnector TxErrorsMonitorTableConnetctor):
    Orch(TxErrorsMonitorTableConnetctor.first, TxErrorsMonitorTableConnetctor.second),
	m_countersDb(new DBConnector("COUNTERS_DB", 0)),
	m_countersTable(new Table(m_countersDb.get(), COUNTERS_TABLE)),
	m_stateDb(new DBConnector("STATE_DB", 0)),
	m_stateTable(new Table(m_stateDb.get(), STATE_TX_ERROR_TABLE_NAME))
{
    SWSS_LOG_ENTER();

    m_cfgTxTbl = unique_ptr<Table>(new Table(TxErrorsMonitorTableConnetctor.first, TxErrorsMonitorTableConnetctor.second));
    initCfgTxTbl();

    auto interv = timespec { .tv_sec = m_duration, .tv_nsec = 0 };
    m_timer = new SelectableTimer(interv);
    auto executor = new ExecutableTimer(m_timer, this, "MC_TX_POLL");
    Orch::addExecutor(executor);
    m_timer->start();
}


MonitorTxOrch::~MonitorTxOrch(void)
{
    SWSS_LOG_ENTER();
}


void MonitorTxOrch::initCfgTxTbl()
{
    /*
     table name: "TX_ERROR_MONITOR" "config"
     "duration"
     <duration value>
     "threshold"
     <threshold value>
     */
    SWSS_LOG_ENTER();

    vector<FieldValueTuple> fvs;
    fvs.emplace_back(DURATION_FIELD_NAME, std::to_string(m_duration));
    fvs.emplace_back(THRESHOLD_FIELD_NAME, std::to_string(m_threshold));
    m_cfgTxTbl->set(CONFIG_KEY_NAME, fvs);
}


void MonitorTxOrch::doTask(SelectableTimer &timer)
{
    SWSS_LOG_ENTER();

    std::unordered_map<std::string, std::string> txErrorStates;
    std::string txErrorsCounterStr;

    auto allPorts = gPortsOrch->getAllPorts();
    for (auto &it: allPorts)
    {
        Port port = it.second;
        string currInterface = port.m_alias.c_str();

		if (!m_countersTable->hget(sai_serialize_object_id(port.m_port_id), TX_ERRORS_COUNTER_NAME, txErrorsCounterStr))
		{
			SWSS_LOG_NOTICE("COULD NOT FIND PORT'S TX_ERRORS");
			continue;
		}
		uint32_t currValue = to_uint<uint32_t>(txErrorsCounterStr);

        updateCurrentStateForPort(currInterface, currValue, txErrorStates);

        lastTxErrorsPerPortStats[currInterface] = currValue;
    }
    updateStateDb(txErrorStates);
}


void MonitorTxOrch::updateCurrentStateForPort(string currInterface, uint32_t currValue, std::unordered_map<std::string, std::string>& txErrorStates){
	uint32_t lastValue = lastTxErrorsPerPortStats[currInterface];
	if(currValue > lastValue){
		if(currValue - lastValue > m_threshold)
			txErrorStates[currInterface] = TX_ERRORS_NOT_OK_STATE;
		else
			txErrorStates[currInterface] = TX_ERRORS_OK_STATE;
	}
	else{
		txErrorStates[currInterface] = TX_ERRORS_OK_STATE;
	}
}


void MonitorTxOrch::updateStateDb(std::unordered_map<std::string, std::string>& txErrorStates){

    for (auto const& entry : txErrorStates){
        FieldValueTuple currTxState(entry.first, entry.second);
        vector<FieldValueTuple> fields;
        fields.push_back(currTxState);
        m_stateTable->set("", fields);
        fields.clear();
    }
}


void MonitorTxOrch::doTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();

    auto it = consumer.m_toSync.begin();
	while (it != consumer.m_toSync.end())
	{
		auto &event = it->second;
		handleEvent(event);
		consumer.m_toSync.erase(it++);
	}

}


void MonitorTxOrch::handleEvent(KeyOpFieldsValuesTuple event){
	string key = kfvKey(event);
	string op = kfvOp(event);
	vector<FieldValueTuple> fvs = kfvFieldsValues(event);

	if(key == CONFIG_KEY_NAME && op == SET_COMMAND){
		HandleConfigUpdate(fvs);
	}
	else{
		SWSS_LOG_WARN("Unexpected event: op = %s; key = %s" ,op.c_str(), key.c_str());
	}
}


void MonitorTxOrch::HandleConfigUpdate(vector<FieldValueTuple> fvs){
	for (auto fv : fvs){
		string field = fvField(fv);
		string value = fvValue(fv);

		if(field == THRESHOLD_FIELD_NAME){
			uint32_t newThreshold = to_uint<uint32_t>(value);
			if(newThreshold != m_threshold){
				m_threshold = to_uint<uint32_t>(value);
				SWSS_LOG_NOTICE("Threshold set to: %s",value.c_str());
			}
		}
		else{
			if(field == DURATION_FIELD_NAME){
				uint32_t newDuration = to_uint<uint32_t>(value);
				if(m_duration != newDuration){
					m_duration = newDuration;
					SWSS_LOG_NOTICE("Duration set to: %s",value.c_str());
					auto interv = timespec { .tv_sec = m_duration, .tv_nsec = 0 };
					m_timer->setInterval(interv);
					m_timer->reset();
				}
			}
			else
				SWSS_LOG_WARN("Unexpected field for set command: %s", field.c_str());
		}
	}
}
