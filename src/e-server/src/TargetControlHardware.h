/*
  File: targetCntrlHardware.h

  This file is part of the Epiphany Software Development Kit.

  Copyright (C) 2013 Adapteva, Inc.
  See AUTHORS for list of contributors.
  Support e-mail: <support@adapteva.com>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program (see the file COPYING).  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef TARGET_CONTROL_HARDWARE__H
#define TARGET_CONTROL_HARDWARE__H

//-----------------------------------------------------------------------------
//! Module implementing a hardware target
//!
//! A thread invoked by RSP memory access commands.
//! The call is blocking. The gdb doesn't gain control until the call is done
//-----------------------------------------------------------------------------


#include "TargetControl.h"

#include <e-xml/src/epiphany_platform.h>
#include <e-hal/src/epiphany-hal-data.h>

class TargetControlHardware:public TargetControl
{
public:
  // Constructor
  TargetControlHardware (unsigned  indexInMemMap,
			 bool      _dontCheckHwAddress);

  // check if specified coreis is supported by HW system
    virtual bool SetAttachedCoreId (unsigned);

  // Functions to access memory. All register access on the ATDSP is via memory
  virtual bool readMem32 (uint32_t addr, uint32_t &);
  virtual bool readMem16 (uint32_t addr, uint16_t &);
  virtual bool readMem8 (uint32_t addr, uint8_t &);

  virtual bool writeMem32 (uint32_t addr, uint32_t value);
  virtual bool writeMem16 (uint32_t addr, uint16_t value);
  virtual bool writeMem8 (uint32_t addr, uint8_t value);

  //burst write and read
  virtual bool WriteBurst (unsigned long addr, unsigned char *buf,
			   size_t buff_size);
  virtual bool ReadBurst (unsigned long addr, unsigned char *buf,
			  size_t buff_size);

  //send system specific reset for all platfom/chip
  virtual void PlatformReset ();

  //no support for the trace
  virtual bool initTrace ()
  {
    return true;
  }
  virtual bool startTrace ()
  {
    return true;
  }
  virtual bool stopTrace ()
  {
    return true;
  }

  virtual std::string GetTargetId ();

  //! Static function to initialize the hardware
  static int  initHwPlatform (platform_definition_t* platform);

  //! Static function to initialize the memory map
  static unsigned initDefaultMemoryMap (platform_definition_t* platform);

private:
  // Local copy of flag.
  bool  dontCheckHwAddress;

  /* convert the local address to full address */
  unsigned long ConvertAddress (unsigned long);

  /* read and write from target */
  bool readMem (uint32_t addr, uint32_t & data, unsigned burst_size);
  bool writeMem (uint32_t addr, uint32_t data, unsigned burst_size);

};

#endif /* TARGET_CONTROL_HARDWARE__H */


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// End: