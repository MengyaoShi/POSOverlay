#include "CalibFormats/SiPixelObjects/interface/PixelCalibConfiguration.h"
#include "CalibFormats/SiPixelObjects/interface/PixelDACNames.h"
#include "PixelCalibrations/include/PixelTBMDelayCalibrationWithScores.h"

//#include <toolbox/convertstring.h>

using namespace pos;

PixelTBMDelayCalibrationWithScores::PixelTBMDelayCalibrationWithScores(const PixelSupervisorConfiguration & tempConfiguration, SOAPCommander* mySOAPCmdr)
  : PixelCalibrationBase(tempConfiguration, *mySOAPCmdr)
{
  std::cout << "Greetings from the PixelTBMDelayCalibrationWithScores copy constructor." << std::endl;
}

void PixelTBMDelayCalibrationWithScores::beginCalibration() {
  PixelCalibConfiguration* tempCalibObject = dynamic_cast<PixelCalibConfiguration*>(theCalibObject_);
  assert(tempCalibObject != 0);

  // Check that PixelCalibConfiguration settings make sense.
	
  if (!tempCalibObject->singleROC() && tempCalibObject->maxNumHitsPerROC() > 2 && tempCalibObject->parameterValue("OverflowWarning") != "no") {
    std::cout << "ERROR:  FIFO3 will overflow with more than two hits on each ROC.  To run this calibration, use 2 or less hits per ROC, or use SingleROC mode.  Now aborting..." << std::endl;
    assert(0);
  }

  if (!tempCalibObject->containsScan("TBMADelay") && !tempCalibObject->containsScan("TBMBDelay") && !tempCalibObject->containsScan("TBMPLL"))
    std::cout << "warning: none of TBMADelay, TBMBDelay, TBMPLLDelay found in scan variable list!" <<std::endl;

  DelayBeforeFirstTrigger = tempCalibObject->parameterValue("DelayBeforeFirstTrigger") == "yes";
  DelayEveryTrigger = tempCalibObject->parameterValue("DelayEveryTrigger") == "yes";
}

bool PixelTBMDelayCalibrationWithScores::execute() {
  PixelCalibConfiguration* tempCalibObject = dynamic_cast<PixelCalibConfiguration*>(theCalibObject_);
  assert(tempCalibObject != 0);

  const bool firstOfPattern = event_ % tempCalibObject->nTriggersPerPattern() == 0;
  const unsigned state = event_/(tempCalibObject->nTriggersPerPattern());
  reportProgress(0.05);

  // Configure all TBMs and ROCs according to the PixelCalibConfiguration settings, but only when it's time for a new configuration.
  if (firstOfPattern) {
    std::cout << "new state " << state << std::endl;
    commandToAllFECCrates("CalibRunning");
  }

  if (firstOfPattern) {
    //commandToAllFEDCrates("JMTJunk");
    std::cout << "Sleeping 5 seconds for feds to re-acquire phases" << std::endl;
    sleep(5);
  }

  //if (DelayEveryTrigger || (DelayBeforeFirstTrigger && firstOfPattern))
  //  usleep(1000000);

  // Send trigger to all TBMs and ROCs.
  sendTTCCalSync();

  // Read out data from each FED.
  Attribute_Vector parametersToFED(2);
  parametersToFED[0].name_ = "WhatToDo"; parametersToFED[0].value_ = "RetrieveData";
  parametersToFED[1].name_ = "StateNum"; parametersToFED[1].value_ = itoa(state);
  commandToAllFEDCrates("FEDCalibrations", parametersToFED);

  return event_ + 1 < tempCalibObject->nTriggersTotal();
}

void PixelTBMDelayCalibrationWithScores::endCalibration() {
  PixelCalibConfiguration* tempCalibObject = dynamic_cast<PixelCalibConfiguration*>(theCalibObject_);
  assert(tempCalibObject != 0);
  assert(event_ == tempCalibObject->nTriggersTotal());
	
  Attribute_Vector parametersToFED(2);
  parametersToFED[0].name_ = "WhatToDo"; parametersToFED[0].value_ = "Analyze";
  parametersToFED[1].name_ = "StateNum"; parametersToFED[1].value_ = "0";
  commandToAllFEDCrates("FEDCalibrations", parametersToFED);
}

std::vector<std::string> PixelTBMDelayCalibrationWithScores::calibrated() {
  std::vector<std::string> tmp;
  return tmp;
}
