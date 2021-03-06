/*
 * StargazerController.h
 *
 *
 *  Created on: 30-11-2012
 *      Author: michal
 */

#ifndef STARTGAZERCONTROLLER_H_
#define STARGAZERCONTROLLER_H_

#include <log4cxx/logger.h>
#include <boost/thread.hpp>
#include <boost/ref.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

#include "AmberScheduler.h"
#include "AmberPipes.h"
#include "RoboclawDriver.h"
#include "drivermsg.pb.h"
#include "roboclaw.pb.h"
#include "RoboclawLib.h"

class RoboclawController: public MessageHandler {
public:
	RoboclawController(int pipeInFd, int pipeOutFd, const char *confFilename);
	virtual ~RoboclawController();

	void handleDataMsg(amber::DriverHdr *driverHdr, amber::DriverMsg *driverMsg);
	void handleClientDiedMsg(int clientID);
	void operator()();

private:
	RoboclawDriver *_roboclawDriver;
	AmberPipes *_amberPipes;

	bool _roboclawDisabled;
	bool _overheated;
	bool _batteryLow;

	RoboclawConfiguration *_configuration;
	boost::thread *_batteryMonitorThread;
	boost::thread *_errorMonitorThread;
	boost::thread *_temperatureMonitorThread;
	boost::thread *_timeoutMonitorThread;

	boost::interprocess::interprocess_mutex _timeoutsMutex;
	boost::system_time _motorsStopTime;
	boost::system_time _resetTime;
	bool _motorsStopTimerEnabled;

	static log4cxx::LoggerPtr _logger;

	amber::DriverMsg *buildCurrentSpeedMsg();
	void sendCurrentSpeedMsg(int receiver, int ackNum);
	void handleCurrentSpeedRequest(int sender, int synNum);
	void handleMotorsEncoderCommand(amber::roboclaw_proto::MotorsSpeed *motorsCommand);
	void parseConfigurationFile(const char *filename);
	void resetAndWait();
	void resetTimeouts();

	void batteryMonitor();
	void errorMonitor();
	void temperatureMonitor();
	void timeoutMonitor();

	std::string getErorDescription(__u8 errorStatus);
	int toQpps(int in);
	int toMmps(int in);

};


#endif /* STARGAZERCONTROLLER_H_ */
