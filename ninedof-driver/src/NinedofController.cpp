/*
 * NinedofController.cpp
 *
 *  Created on: 12-11-2012
 *      Author: michal
 */

#include "NinedofController.h"
#include <log4cxx/logger.h>
#include <log4cxx/propertyconfigurator.h>
#include <boost/program_options.hpp>

#include "AmberPipes.h"
#include "AmberScheduler.h"
#include "NinedofDriver.h"

#include "drivermsg.pb.h"
#include "ninedof.pb.h"

using namespace std;
using namespace boost;
using namespace boost::interprocess;
using namespace boost::program_options;
using namespace log4cxx;
using namespace amber;

LoggerPtr NinedofController::_logger (Logger::getLogger("Ninedof.Controller"));

NinedofController::NinedofController(int pipeInFd, int pipeOutFd, const char *confFilename) {
	GOOGLE_PROTOBUF_VERIFY_VERSION;

	parseConfigurationFile(confFilename);

	_ninedofDriver = new NinedofDriver(_configuration);
	_amberScheduler = new AmberScheduler<NinedofSchedulerEntry>(this);
	_amberPipes = new AmberPipes(this, pipeInFd, pipeOutFd);

	_dataStruct = _ninedofDriver->getDataStruct();

	_schedulerThread = new boost::thread(boost::ref(*_amberScheduler));
	_driverThread = new boost::thread(boost::ref(*_ninedofDriver));
}

NinedofController::~NinedofController() {
	delete _ninedofDriver;
	delete _amberScheduler;
	delete _amberPipes;
}

void NinedofController::handleDataRequestMsg(int sender, int synNum, ninedof_proto::DataRequest *dataRequest) {
	if (_logger->isDebugEnabled()) {
		LOG4CXX_DEBUG(_logger, "Got DataRequest message");
	}

	if (_logger->isDebugEnabled()) {
		LOG4CXX_DEBUG(_logger, "Sending SensorData message");
	}

	sendSensorDataMsg(sender, synNum, dataRequest->accel(), dataRequest->gyro(), dataRequest->magnet());

}

void NinedofController::handleSubscribeActionMsg(int sender, ninedof_proto::SubscribeAction *subscribeAction) {
	if (_logger->isDebugEnabled()) {
		LOG4CXX_DEBUG(_logger, "Got SubscribeAction message");
	}

	if (subscribeAction->freq() == 0) {
		
		if (_logger->isDebugEnabled()) {
			LOG4CXX_DEBUG(_logger, "Removing client id: " << sender);
		}

		_amberScheduler->removeClient(sender);
	} else {

		if (_logger->isDebugEnabled()) {
			LOG4CXX_DEBUG(_logger, "Adding new client id: " << sender << ", freq: " << subscribeAction->freq());
		}

		_amberScheduler->addClient(sender, subscribeAction->freq(),
				new NinedofSchedulerEntry(subscribeAction->accel(), subscribeAction->gyro(), subscribeAction->magnet()));
	}
}

void NinedofController::sendSensorDataMsg(int receiver, int ackNum, bool accel, bool gyro, bool magnet) {
	DriverMsg *sensorDataMsg = buildSensorDataMsg(accel, gyro, magnet);
	sensorDataMsg->set_acknum(ackNum);
	DriverHdr *header = new DriverHdr();
	header->add_clientids(receiver);
	//header->set_devicetype(1);
	//header->set_deviceid(0);

	_amberPipes->writeMsgToPipe(header, sensorDataMsg);

	delete sensorDataMsg;
	delete header;
}


int NinedofController::toMilliG(__s16 value) {
	return (int) (value / (double)ACCEL_LSB_PER_MILLIG);
}

int NinedofController::toMilliGauss(__s16 value) {
	return (int) (value / (double)MAGNET_LSB_PER_MILLIGAUSS);
}

int NinedofController::toDPS(__s16 value) {
	return (int) (value / (double)GYRO_LSB_PER_DPS);
}

DriverMsg *NinedofController::buildSensorDataMsg(bool accel, bool gyro, bool magnet) {
	scoped_lock<interprocess_mutex> lock(_ninedofDriver->dataMutex);

	_ninedofDriver->needToGetData = true;
	_ninedofDriver->noNeedToGetData.notify_one();

	_ninedofDriver->dataNotReady.wait(lock);

	DriverMsg *message = new DriverMsg();
	message->set_type(DriverMsg_MsgType_DATA);

	ninedof_proto::SensorData *sensorData = message->MutableExtension(ninedof_proto::sensorData);

	//LOG4CXX_DEBUG(_logger, "buildSensorDataMsg " << accel << " " << gyro << " " << magnet);

	ninedof_proto::SensorData::AxisData *axisData;
	if (accel) {
		axisData = sensorData->mutable_accel();
		axisData->set_xaxis(toMilliG(_dataStruct->accel.x_axis));
		axisData->set_yaxis(toMilliG(_dataStruct->accel.y_axis));
		axisData->set_zaxis(toMilliG(_dataStruct->accel.z_axis));
	}

	if (gyro) {
		axisData = sensorData->mutable_gyro();
		axisData->set_xaxis(toDPS(_dataStruct->gyro.x_axis));
		axisData->set_yaxis(toDPS(_dataStruct->gyro.y_axis));
		axisData->set_zaxis(toDPS(_dataStruct->gyro.z_axis));
	}

	if (magnet) {
		axisData = sensorData->mutable_magnet();
		axisData->set_xaxis(toMilliGauss(_dataStruct->magnet.x_axis));
		axisData->set_yaxis(toMilliGauss(_dataStruct->magnet.y_axis));
		axisData->set_zaxis(toMilliGauss(_dataStruct->magnet.z_axis));
	}

	return message;
}

void NinedofController::handleSchedulerEvent(int clientId, NinedofSchedulerEntry *entry) {
	
	if (_logger->isDebugEnabled()) {
		LOG4CXX_DEBUG(_logger, "Handling scheduler event, clientId: "<< clientId
			<< ", accel: " << entry->accel
			<< ", gyro: " << entry->gyro
			<< ", magnet: " << entry->magnet);
	}
	
	sendSensorDataMsg(clientId, 0, entry->accel, entry->gyro, entry->magnet);
}

void NinedofController::handleDataMsg(DriverHdr *driverHdr, DriverMsg *driverMsg) {

	// TODO: hack for now
	int clientId = driverHdr->clientids_size() > 0 ? driverHdr->clientids(0) : 0;

	// DataRequest
	if (driverMsg->HasExtension(ninedof_proto::dataRequest)) {
		if (!driverMsg->has_synnum()) {
			LOG4CXX_WARN(_logger, "Got DataRequest, but SynNum not set, ignoring.");
			return;
		}

		ninedof_proto::DataRequest *dataRequest = driverMsg->MutableExtension(ninedof_proto::dataRequest);
		handleDataRequestMsg(clientId, driverMsg->synnum(), dataRequest);
	}

	// SubscribeAction
	if (driverMsg->HasExtension(ninedof_proto::subscribeAction)) {
		ninedof_proto::SubscribeAction *subscribeAction = driverMsg->MutableExtension(ninedof_proto::subscribeAction);
		handleSubscribeActionMsg(clientId, subscribeAction);
	}
}

void NinedofController::handleClientDiedMsg(int clientID) {
	LOG4CXX_INFO(_logger, "Client " << clientID << " died, removing from scheduler");

	if (_amberScheduler->hasClient(clientID)) {
		_amberScheduler->removeClient(clientID);
	}

}

void NinedofController::operator()() {
	_amberPipes->operator ()();
}

void NinedofController::parseConfigurationFile(const char *filename) {

	LOG4CXX_INFO(_logger, "Parsing configuration file: " << filename);

	_configuration = new NinedofConfiguration();

	options_description desc("Ninedof options");
	desc.add_options()
			("ninedof.i2c_port", value<string>(&_configuration->i2c_port)->default_value("/dev/i2c-4"))
	;

	variables_map vm;

	try {
		store(parse_config_file<char>(filename, desc), vm);
		notify(vm);

	} catch (std::exception& e) {
		LOG4CXX_ERROR(_logger, "Error in parsing configuration file: " << e.what());
	}

}

int main(int argc, char *argv[]) {

	if (argc < 3) {
		return 1;
	}

	const char *confFile = argv[1];
	const char *logConfFile = argv[2];

	PropertyConfigurator::configure(logConfFile);

	LoggerPtr logger (Logger::getLogger("main"));

	LOG4CXX_INFO(logger, "-------------");
	LOG4CXX_INFO(logger, "Creating controller, config_file: " << argv[1] << ", log_config_file: " << argv[2]);

	NinedofController controller(0, 1, confFile);
	controller();
}
