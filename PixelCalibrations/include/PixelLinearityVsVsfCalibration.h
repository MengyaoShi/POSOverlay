/*************************************************************************
 * XDAQ Components for Distributed Data Acquisition                      *
 * Copyright (C) 2000-2004, CERN.			                 *
 * All rights reserved.                                                  *
 * Authors: J. Gutleber and L. Orsini					 *
 *                                                                       *
 * For the licensing terms see LICENSE.		                         *
 * For the list of contributors see CREDITS.   			         *
 *************************************************************************/

#ifndef _PixelLinearityVsVsfCalibration_h_
#define _PixelLinearityVsVsfCalibration_h_

#include "toolbox/exception/Handler.h"
#include "toolbox/Event.h"

#include "PixelCalibrations/include/PixelCalibrationBase.h"

class PixelLinearityVsVsfCalibration: public PixelCalibrationBase {
 public:

  // PixelLinearityVsVsfCalibration Constructor
  //PixelLinearityVsVsfCalibration();

  PixelLinearityVsVsfCalibration( const PixelSupervisorConfiguration &, SOAPCommander* );

  virtual ~PixelLinearityVsVsfCalibration(){};

  void beginCalibration();

  virtual bool execute();

  void endCalibration();

  virtual std::vector<std::string> calibrated();

};

#endif
 
