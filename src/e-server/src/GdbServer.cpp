// GDB RSP server class: Definition.

// Copyright (C) 2008, 2009, Embecosm Limited
// Copyright (C) 2009-2014 Adapteva Inc.

// Contributor: Oleg Raikhman <support@adapteva.com>
// Contributor: Yaniv Sapir <support@adapteva.com>
// Contributor: Jeremy Bennett <jeremy.bennett@embecosm.com>

// This file is part of the Adapteva RSP server.

// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.

// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details.

// You should have received a copy of the GNU General Public License along
// with this program.  If not, see <http://www.gnu.org/licenses/>.  */

// Note that the Epiphany is a little endian architecture.

#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "GdbServer.h"
#include "Utils.h"

#include "libgloss_syscall.h"

using std::cerr;
using std::cout;
using std::dec;
using std::endl;
using std::flush;
using std::hex;
using std::ostringstream;
using std::setbase;
using std::setfill;
using std::setw;
using std::stringstream;
using std::vector;


//! @todo We do not handle a user coded BKPT properly (i.e. one that is not
//!       a breakpoint). Effectively it is ignored, whereas we ought to set up
//!       the exception registers and redirect through the trap vector.

//! At this time the stall state of the target is unknown.



//! Constructor for the GDB RSP server.

//! Create a new packet for passing, a new connection to listen to the client
//! and a new hash table for breakpoints etc.

//! @param[in] _si   All the information about the server.
GdbServer::GdbServer (ServerInfo* _si) :
  si (_si),
  fTargetControl (NULL),
  fIsTargetRunning (false)
{
  pkt = new RspPacket (RSP_PKT_MAX);
  rsp = new RspConnection (si);
  mpHash = new MpHash ();

}	// GdbServer ()


//! Destructor
GdbServer::~GdbServer ()
{
  delete mpHash;
  delete rsp;
  delete pkt;

}	// ~GdbServer ()


//! Attach to the target

//! If not already halted, the target will be halted.

//! @todo What should we really do if the target fails to halt?

//! @note  The target should *not* be reset when attaching.
void
GdbServer::rspAttach ()
{
  bool isHalted = targetHalt ();

  if (!isHalted)
      rspReportException (0, 0 /*all threads */ , TARGET_SIGNAL_HUP);

}	// rspAttach ()


//! Detach from hardware.

//! For now a null function.

//! @todo Leave emulation mode?
void
GdbServer::rspDetach ()
{
}	// rspDetach ()


//! Listen for RSP requests

//! @param[in] _fTargetControl  Pointer to the target API for the actual
//!                             target.
void
GdbServer::rspServer (TargetControl* _fTargetControl)
{
  fTargetControl = _fTargetControl;
  assert (fTargetControl);

  // Loop processing commands forever
  while (true)
    {
      // Make sure we are still connected.
      while (!rsp->isConnected ())
	{
	  // Reconnect and stall the processor on a new connection
	  if (!rsp->rspConnect ())
	    {
	      // Serious failure. Must abort execution.
	      cerr << "ERROR: Failed to reconnect to client. Exiting.";
	      exit (EXIT_FAILURE);
	    }
	  else
	    {
	      cout << "INFO: connected to port " << si->port () << endl;

	      if (si->haltOnAttach ())
		  rspAttach ();
	    }
	}


      // Get a RSP client request
      if (si->debugStopResume())
	cerr << dec << "DebugStopResume: Getting RSP client request." << endl;

      rspClientRequest ();

      //check if the target is stopped and not hit by BP in continue command
      //and check gdb CTRL-C and continue again
      while (fIsTargetRunning)
	{
	  if (si->debugCtrlCWait())
	    cerr << "DebugCtrlCWait: Check for Ctrl-C" << endl;
	  bool isGotBreakCommand = rsp->getBreakCommand ();
	  if (isGotBreakCommand)
	    {
	      cerr << "CTLR-C request from gdb server." << endl;

	      rspSuspend ();
	      //get CTRl-C from gdb, the user should continue the target
	    }
	  else
	    {
	      //continue
	      rspContinue (0, 0);	//the args are ignored by continue command in this mode
	    }
	  if (si->debugCtrlCWait())
	    cerr << dec <<
	      "check for CTLR-C done" << endl << flush;
	}

      if (si->debugStopResume ())
	cerr <<
	  "-------------- rspClientRequest(): end" << endl << endl << flush;
    }
}				// rspServer()


//-----------------------------------------------------------------------------
//! Deal with a request from the GDB client session

//! In general, apart from the simplest requests, this function replies on
//! other functions to implement the functionality.

//! @note It is the responsibility of the recipient to delete the packet when
//!       it is finished with. It is permissible to reuse the packet for a
//!       reply.

//! @todo Is this the implementation of the 'D' packet really the intended
//!       meaning? Or does it just mean that only vAttach will be recognized
//!       after this?

//! @param[in] pkt  The received RSP packet
//-----------------------------------------------------------------------------
void
GdbServer::rspClientRequest ()
{
  if (!rsp->getPkt (pkt))
    {
      rsp->rspClose ();		// Comms failure
      return;
    }

  switch (pkt->data[0])
    {
    case '!':
      // Request for extended remote mode
      pkt->packStr ("");	// Empty = not supported
      rsp->putPkt (pkt);
      break;

    case '?':
      // Return last signal ID
      rspReportException (0 /*PC ??? */ , 0 /*all threads */ ,
			  TARGET_SIGNAL_TRAP);
      break;

    case 'A':
      // Initialization of argv not supported
      cerr << "Warning: RSP 'A' packet not supported: ignored" << endl <<
	flush;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      break;

    case 'b':
      // Setting baud rate is deprecated
      cerr << "Warning: RSP 'b' packet is deprecated and not "
	<< "supported: ignored" << endl << flush;
      break;

    case 'B':
      // Breakpoints should be set using Z packets
      cerr << "Warning: RSP 'B' packet is deprecated (use 'Z'/'z' "
	<< "packets instead): ignored" << endl << flush;
      break;

    case 'c':
      // Continue
      rspContinue (TARGET_SIGNAL_NONE);
      break;

    case 'C':
      // Continue with signal (in the packet)
      rspContinue ();
      break;

    case 'd':
      // Disable debug using a general query
      cerr << "Warning: RSP 'd' packet is deprecated (define a 'Q' "
	<< "packet instead: ignored" << endl << flush;
      break;

    case 'D':
      // Detach GDB. Do this by closing the client. The rules say that
      // execution should continue, so unstall the processor.
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
      rsp->rspClose ();

      break;

    case 'F':

      //parse the F reply packet
      rspFileIOreply ();

      //always resume -- (continue c or s command)
      targetResume ();

      break;

    case 'g':
      rspReadAllRegs ();
      break;

    case 'G':
      rspWriteAllRegs ();
      break;

    case 'H':
      rspSetThread ();
      break;

    case 'i':
    case 'I':
      // Single cycle step not currently supported. Mark the target as
      // running, so that next time it will be detected as stopped (it is
      // still stalled in reality) and an ack sent back to the client.
      cerr << "Warning: RSP cycle stepping not supported: target "
	<< "stopped immediately" << endl << flush;
      break;

    case 'k':

      cerr << hex << "GDB client kill request. The multicore server will be detached from the"
	   << endl
	   << "specific gdb client. Use target remote :<port> to connect again"
	   << endl << flush;	//Stop target id supported
      //pkt->packStr("OK");
      //rsp->putPkt(pkt);
      //exit(23);
      rspDetach ();
      //reset to the initial state to prevent reporting to the disconnected client
      fIsTargetRunning = false;

      break;

    case 'm':
      // Read memory (symbolic)
      rspReadMem ();
      break;

    case 'M':
      // Write memory (symbolic)
      rspWriteMem ();
      break;

    case 'p':
      // Read a register
      rspReadReg ();
      break;

    case 'P':
      // Write a register
      rspWriteReg ();
      break;

    case 'q':
      // Any one of a number of query packets
      rspQuery ();
      break;

    case 'Q':
      // Any one of a number of set packets
      rspSet ();
      break;

    case 'r':
      // Reset the system. Deprecated (use 'R' instead)
      cerr << "Warning: RSP 'r' packet is deprecated (use 'R' "
	<< "packet instead): ignored" << endl << flush;
      break;

    case 'R':
      // Restart the program being debugged.
      rspRestart ();
      break;

    case 's':
      // Single step one machine instruction.
      rspStep (TARGET_SIGNAL_NONE);
      break;

    case 'S':
      // Single step one machine instruction.
      rspStep ();
      break;

    case 't':
      // Search. This is not well defined in the manual and for now we don't
      // support it. No response is defined.
      cerr << "Warning: RSP 't' packet not supported: ignored"
	<< endl << flush;
      break;

    case 'T':
      // Is the thread alive. We are bare metal, so don't have a thread
      // context. The answer is always "OK".
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
      break;

    case 'v':
      // Any one of a number of packets to control execution
      rspVpkt ();
      break;

    case 'X':
      // Write memory (binary)
      rspWriteMemBin ();
      break;

    case 'z':
      // Remove a breakpoint/watchpoint.
      rspRemoveMatchpoint ();
      break;

    case 'Z':
      // Insert a breakpoint/watchpoint.
      rspInsertMatchpoint ();
      break;

    default:
      // Unknown commands are ignored
      cerr << "Warning: Unknown RSP request" << pkt->data << endl << flush;
      break;
    }
}				// rspClientRequest()


//-----------------------------------------------------------------------------
//! Send a packet acknowledging an exception has occurred

//! The only signal we ever see in this implementation is TRAP/ABORT.
//! TODO no thread support -- always report as S packet
//-----------------------------------------------------------------------------
void
GdbServer::rspReportException (unsigned stoppedPC, unsigned threadID,
			       unsigned exCause)
{
  if (si->debugStopResume ())
    cerr << "stopped at PC 0x" << hex << stoppedPC << "  EX 0x" << exCause
	 << dec << endl << flush;

  // Construct a signal received packet
  if (threadID == 0)
    {
      pkt->data[0] = 'S';
    }
  else
    {
      pkt->data[0] = 'T';
    }

  pkt->data[1] = Utils::hex2Char (exCause >> 4);
  pkt->data[2] = Utils::hex2Char (exCause % 16);

  if (threadID != 0)
    {
      sprintf ((pkt->data), "T05thread:%d;", threadID);

    }
  else
    {

      pkt->data[3] = '\0';
    }
  pkt->setLen (strlen (pkt->data));

  rsp->putPkt (pkt);

  //core in Debug state (bkpt) .. report to gdb
  fIsTargetRunning = false;

}				// rspReportException()


//-----------------------------------------------------------------------------
//! Handle a RSP continue request

//! This version is typically used for the 'c' packet, to continue without
//! signal, in which case TARGET_SIGNAL_NONE is passed in as the exception to
//! use.

//! At present other exceptions are not supported

//! @param[in] except  The GDB signal to use
//-----------------------------------------------------------------------------
void
GdbServer::rspContinue (uint32_t except)
{
  uint32_t addr;		// Address to continue from, if any

  // Reject all except 'c' packets
  if ('c' != pkt->data[0])
    {
      cerr << "Warning: Continue with signal not currently supported: "
	<< "ignored" << endl << flush;
      return;
    }

  // Get an address if we have one
  if (0 == strcmp ("c", pkt->data))
    {
      addr = readPc ();		// Default uses current PC
    }
  else if (1 != sscanf (pkt->data, "c%x", &addr))
    {
      cerr << "Warning: RSP continue address " << pkt->data
	<< " not recognized: ignored" << endl << flush;
      addr = readPc ();		// Default uses current NPC
    }

  rspContinue (addr, TARGET_SIGNAL_NONE);

}				// rspContinue()


//-----------------------------------------------------------------------------
//! Handle a RSP continue with signal request

//! @todo Currently does nothing. Will use the underlying generic continue
//!       function.
//-----------------------------------------------------------------------------
void
GdbServer::rspContinue ()
{
  if (si->debugTrapAndRspCon ())
    cerr << "RSP continue with signal '" << pkt->
      data << "' received" << endl << flush;

  //return the same exception
  unsigned exCause = TARGET_SIGNAL_TRAP;

  if ((0 == strcmp ("C03", pkt->data)))
    {				//get continue with signal after reporting QUIT/exit, silently ignore

      exCause = TARGET_SIGNAL_QUIT;

    }
  else
    {
      cerr << "WARNING: RSP continue with signal '" << pkt->
	data << "' received, the server will ignore the continue" << endl <<
	flush;

      //check the exception state
      isTargetExceptionState (exCause);
      //bool isExState= isTargetExceptionState(exCause);
    }

  //get PC
  uint32_t reportedPc = readPc ();

  // report to gdb the target has been stopped
  rspReportException (reportedPc, 0 /* all threads */ , exCause);

}				// rspContinue()


//----------------------------------------------------------
//! have sleep con for request .. get other thread to communicate with target
//
//-----------------------------------------------------------------------------
void
GdbServer::NanoSleepThread (unsigned long timeout)
{
  struct timespec sleepTime;
  struct timespec remainingSleepTime;

  sleepTime.tv_sec = 0;
  sleepTime.tv_nsec = timeout;
  nanosleep (&sleepTime, &remainingSleepTime);

}


//-----------------------------------------------------------------------------
//! Resume target, writing ATDSP_DEBUG_RUN to core debug register
//
//-----------------------------------------------------------------------------
void
GdbServer::targetResume ()
{
  //write to CORE_DEBUGCMD
  fTargetControl->writeMem32 (CORE_DEBUGCMD, ATDSP_DEBUG_RUN);

  if (si->debugTrapAndRspCon ())
    cerr << dec <<
      " resume CORE_DEBUGCMD " << hex << CORE_DEBUGCMD << " " <<
      ATDSP_DEBUG_RUN << dec << endl << flush;

  fIsTargetRunning = true;

  if (si->debugStopResume ())
    cerr << "resumed"
	 << endl << flush;
}


//-----------------------------------------------------------------------------
//! Generic processing of a continue request

//! The signal may be TARGET_SIGNAL_NONE if there is no exception to be
//! handled. Currently the exception is ignored.

//! @param[in] addr    Address from which to step
//! @param[in] except  The exception to use (if any). Currently ignored
//-----------------------------------------------------------------------------
void
GdbServer::rspContinue (uint32_t addr, uint32_t except)
{
  if ((!fIsTargetRunning && si->debugStopResume ()) || si->debugTranDetail ())
    {
      cerr << dec <<
	"GdbServer::rspContinue PC 0x" << hex << addr << dec << endl <<
	flush;
    }

  uint32_t prevPc = 0;

  if (!fIsTargetRunning)
    {
      //cerr << "********* fIsTargetRunning = false **************" << endl << flush;
      //check if core in debug state
      if (!isTargetInDebugState ())
	{
	  //cerr << "********* isTargetInDebugState = false **************" << endl << flush;

	  //cerr << "Internal Error(DGB server): Core is not in HALT state while the GDB is asking the cont" << endl << flush;
	  //pkt->packStr("E01");
	  //rsp->putPkt(pkt);
	  //exit(2);

	  fIsTargetRunning = true;
	}
      else
	{
	  //cerr << "********* isTargetInDebugState = true **************" << endl << flush;

	  //set PC
	  writePc (addr);

	  //resume
	  targetResume ();
	}
    }

  unsigned long timeout_me = 0;
  unsigned long timeout_limit = 100;

  timeout_limit = 3;

  while (true)
    {
      //cerr << "********* while true **************" << endl << flush;

      NanoSleepThread (300000000);

      timeout_me += 1;

      //give up control and check for CTRL-C
      if (timeout_me > timeout_limit)
	{
	  //cerr << "********* timeout me > limit **************" << endl << flush;
	  //cerr << " PC << " << hex << readPc() << dec << endl << flush;
	  assert (fIsTargetRunning);
	  break;
	}

      //check the value of debug register

      if (isTargetInDebugState ())
	{
	  //cerr << "********* isTargetInDebugState = true **************" << endl << flush;

	  // If it's a breakpoint, then we need to back up one instruction, so
	  // on restart we execute the actual instruction.
	  uint32_t c_pc = readPc ();
	  //cout << "stopped at @pc " << hex << c_pc << dec << endl << flush;
	  prevPc = c_pc - ATDSP_BKPT_INSTLEN;

	  //check if it is trap
	  uint16_t val_;
	  fTargetControl->readMem16 (prevPc, val_);
	  //bool retSt = fTargetControl->readMem16(prevPc, val_);
	  uint16_t valueOfStoppedInstr = val_;

	  if (valueOfStoppedInstr == ATDSP_BKPT_INSTR)
	    {
	      //cerr << "********* valueOfStoppedInstr = ATDSP_BKPT_INSTR **************" << endl << flush;

	      if (NULL != mpHash->lookup (BP_MEMORY, prevPc))
		{
		  writePc (prevPc);
		  if (si->debugTrapAndRspCon ())
		    cerr << dec << "set pc back " << hex << prevPc << dec << endl
		      << flush;
		}

	      if (si->debugTrapAndRspCon ())
		cerr <<
		  dec << "After wait CONT GdbServer::rspContinue PC 0x" <<
		  hex << prevPc << dec << endl << flush;

	      // report to gdb the target has been stopped

	      rspReportException (prevPc, 0 /*all threads */ ,
				  TARGET_SIGNAL_TRAP);



	    }
	  else
	    {			// check if stopped for trap (stdio handling)
	      //cerr << "********* valueOfStoppedInstr =\\= ATDSP_BKPT_INSTR **************" << endl << flush;

	      bool stoppedAtTrap =
		(getfield (valueOfStoppedInstr, 9, 0) == ATDSP_TRAP_INSTR);
	      if (!stoppedAtTrap)
		{
		  //cerr << "********* stoppedAtTrap = false **************" << endl << flush;
		  //try to go back an look for trap // bug in the design !!!!!!!!!!!!!!
		  if (si->debugTrapAndRspCon ())
		    cerr << dec << "missed trap ... looking backward for trap "
		      << hex << c_pc << dec << endl << flush;


		  if (valueOfStoppedInstr == ATDSP_NOP_INSTR)
		    {		//trap is always padded by nops
		      for (unsigned j = prevPc - 2; j > prevPc - 20;
			   j = j - 2 /* length of */ )
			{
			  //check if it is trap

			  fTargetControl->readMem16 (j, val_);
			  //bool rSt = fTargetControl->readMem16(j, val_);
			  valueOfStoppedInstr = val_;

			  stoppedAtTrap =
			    (getfield (valueOfStoppedInstr, 9, 0) ==
			     ATDSP_TRAP_INSTR);
			  if (stoppedAtTrap)
			    {
			      if (si->debugStopResumeDetail ())
				cerr << dec <<
				  "trap found @" << hex << j << dec << endl <<
				  flush;
			      break;

			    }
			}
		    }


		}

	      if (stoppedAtTrap)
		{
		  //cerr << "********* stoppedAtTrap = true **************" << endl << flush;

		  fIsTargetRunning = false;

		  uint8_t trapNumber =
		    getfield (valueOfStoppedInstr, 15, 10);
		  redirectSdioOnTrap (trapNumber);
		}
	      else
		{
		  //cerr << "********* stoppedAtTrap = false **************" << endl << flush;
		  if (si->debugStopResumeDetail ())
		    cerr << dec << " no trap found, return control to gdb" <<
		      endl << flush;
		  // report to gdb the target has been stopped
		  rspReportException (readPc () /* PC no trap found */ ,
				      0 /* all threads */ ,
				      TARGET_SIGNAL_TRAP);
		}
	    }

	  break;
	}			// if (isCoreInDebugState())
    }				// while (true)
}				// rspContinue()


//-----------------------------------------------------------------------------
//! Generic processing of a suspend request .. CTRL-C command in gdb
//! Stop target
//! wait for confirmation on DEBUG state
//! report to GDB by TRAP
//! switch to not running stage, same as c or s command
//-----------------------------------------------------------------------------
void
GdbServer::rspSuspend ()
{
  unsigned exCause = TARGET_SIGNAL_TRAP;
  uint32_t reportedPc;

  bool isHalted;

  if (si->debugTrapAndRspCon ())
    cerr << dec <<
      "force debug mode" << endl << flush;

  //probably target suspended
  if (!isTargetInDebugState ())
    {

      isHalted = targetHalt ();
    }
  else
    {
      isHalted = true;
    }

  if (!isHalted)
    {

      exCause = TARGET_SIGNAL_HUP;


    }
  else
    {

      //get PC
      reportedPc = readPc ();

      //check the exception state
      bool isExState = isTargetExceptionState (exCause);

      if (isExState)
	{
	  //stopped due to some exception -- just report to gdb

	}
      else
	{

	  if (isTargetInIldeState ())
	    {

	      //fetch instruction opcode on PC
	      uint16_t val16;
	      fTargetControl->readMem16 (reportedPc, val16);
	      //bool st1 = fTargetControl->readMem16(reportedPc, val16);
	      uint16_t instrOpcode = val16;

	      //idle
	      if (getfield (instrOpcode, 8, 0) == IDLE_OPCODE)
		{
		  //cerr << "POINT on IDLE " << endl << flush;
		}
	      else
		{

		  reportedPc = reportedPc - 2;
		}
	      writePc (reportedPc);

	      //cerr << "SUPEND " << hex << reportedPc << endl << flush;
	    }
	}
    }

  // report to gdb the target has been stopped
  rspReportException (reportedPc, 0 /*all threads */ , exCause);
}


//-----------------------------------------------------------------------------
//! Reply to F packet
/*
 * Fretcode,errno,Ctrl-C flag;call-specific attachment’ retcode is the return code of the system call as hexadecimal value.
 * errno is the errno set by the call, in protocol-specific representation.
 * This parameter can be omitted if the call was successful. Ctrl-C flag is only sent if the user requested a break.
 * In this case, errno must be sent as well, even if the call was successful.
 * The Ctrl-C flag itself consists of
 * the character ‘C’: F0,0,C or, if the call was interrupted before the host call has been performed: F-1,4,C
 * assuming 4 is the protocol-specific representation of EINTR.
 */
//-----------------------------------------------------------------------------
void
GdbServer::rspFileIOreply ()
{

  long int result_io = -1;
  long int host_respond_error_code;

  if (2 ==
      sscanf (pkt->data, "F%lx,%lx", &result_io, &host_respond_error_code))
    {
      //write to r0
      writeGpr (0, result_io);

      //write to r3 error core
      writeGpr (3, host_respond_error_code);
      if (si->debugStopResumeDetail ())
	cerr << dec <<
	  " remote io done " << result_io << "error code" <<
	  host_respond_error_code << endl << flush;

    }
  else if (1 == sscanf (pkt->data, "F%lx", &result_io))
    {

      if (si->debugStopResumeDetail ())
	cerr << dec <<
	  " remote io done " << result_io << endl << flush;

      //write to r0
      writeGpr (0, result_io);
    }
  else
    {
      cerr << " remote IO operation fail " << endl << flush;
    }
}

//-----------------------------------------------------------------------------
//! Redirect the SDIO to gdb using F packets open,write,read, close are supported
//
//
//
//-----------------------------------------------------------------------------

/* Enum declaration for trap instruction dispatch code. See sim/epiphany/epiphany-desc.*/
enum TRAP_CODES
{
  TRAP_WRITE,			// 0
  TRAP_READ,			// 1
  TRAP_OPEN,			// 2
  TRAP_EXIT,			// 3
  TRAP_PASS,			// 4
  TRAP_FAIL,			// 5
  TRAP_CLOSE,			// 6
  TRAP_OTHER,			// 7
};


#define MAX_FILE_NAME_LENGTH (256*4)


void
GdbServer::redirectSdioOnTrap (uint8_t trapNumber)
{
  //cout << "---- stop on PC 0x " << hex << prevPc << dec << endl << flush;
  //cout << "---- got trap 0x" << hex << valueOfStoppedInstr << dec << endl << flush;

  uint32_t r0, r1, r2, r3;
  char *buf;
  //int result_io;
  unsigned int k;

  char res_buf[2048];
  char fmt[2048];

  switch (trapNumber)
    {
    case TRAP_WRITE:

      if (si->debugTrapAndRspCon ())
	cerr << dec <<
	  " Trap 0 write " << endl << flush;
      r0 = readGpr (0);		//chan
      r1 = readGpr (1);		//addr
      r2 = readGpr (2);		//length

      if (si->debugTrapAndRspCon ())
	cerr << dec <<
	  " write to chan " << r0 << " bytes " << r2 << endl << flush;

      sprintf ((pkt->data), "Fwrite,%lx,%lx,%lx", (unsigned long) r0,
	       (unsigned long) r1, (unsigned long) r2);
      pkt->setLen (strlen (pkt->data));
      rsp->putPkt (pkt);

      break;

    case TRAP_READ:
      if (si->debugTrapAndRspCon ())
	cerr << dec << " Trap 1 read " << endl << flush;	/*read(chan, addr, len) */
      r0 = readGpr (0);		//chan
      r1 = readGpr (1);		//addr
      r2 = readGpr (2);		//length

      if (si->debugTrapAndRspCon ())
	cerr << dec <<
	  " read from chan " << r0 << " bytes " << r2 << endl << flush;


      sprintf ((pkt->data), "Fread,%lx,%lx,%lx", (unsigned long) r0,
	       (unsigned long) r1, (unsigned long) r2);
      pkt->setLen (strlen (pkt->data));
      rsp->putPkt (pkt);

      break;
    case TRAP_OPEN:
      r0 = readGpr (0);		//filepath
      r1 = readGpr (1);		//flags

      if (si->debugTrapAndRspCon ())
	cerr << dec <<
	  " Trap 2 open, file name located @" << hex << r0 << dec << " (mode)"
	  << r1 << endl << flush;

      for (k = 0; k < MAX_FILE_NAME_LENGTH - 1; k++)
	{
	  uint8_t val_;
	  fTargetControl->readMem8 (r0 + k, val_);
	  //bool retSt = fTargetControl->readMem8(r0+k, val_);
	  if (val_ == '\0')
	    {
	      break;
	    }
	}

      //Fopen, pathptr/len, flags, mode
      sprintf ((pkt->data), "Fopen,%lx/%d,%lx,%lx", (unsigned long) r0, k,
	       (unsigned long) r1 /*O_WRONLY */ ,
	       (unsigned long) (S_IRUSR | S_IWUSR));
      pkt->setLen (strlen (pkt->data));
      rsp->putPkt (pkt);
      break;

    case TRAP_EXIT:
      if (si->debugTrapAndRspCon ())
	cerr << dec <<
	  " Trap 3 exiting .... ??? " << endl << flush;
      r0 = readGpr (0);		//status
      //cerr << " The remote target got exit() call ... no OS -- ignored" << endl << flush;
      //exit(4);
      rspReportException (readPc (), 0 /*all threads */ , TARGET_SIGNAL_QUIT);
      break;
    case TRAP_PASS:
      cerr << " Trap 4 PASS " << endl << flush;
      rspReportException (readPc (), 0 /*all threads */ , TARGET_SIGNAL_TRAP);
      break;
    case TRAP_FAIL:
      cerr << " Trap 5 FAIL " << endl << flush;
      rspReportException (readPc (), 0 /*all threads */ , TARGET_SIGNAL_QUIT);
      break;
    case TRAP_CLOSE:
      r0 = readGpr (0);		//chan
      if (si->debugTrapAndRspCon ())
	cerr << dec <<
	  " Trap 6 close: " << r0 << endl << flush;
      sprintf ((pkt->data), "Fclose,%lx", (unsigned long) r0);
      pkt->setLen (strlen (pkt->data));
      rsp->putPkt (pkt);
      break;
    case TRAP_OTHER:

      if (NULL != si->ttyOut ())
	{

	  //cerr << " Trap 7 syscall -- ignored" << endl << flush;
	  if (si->debugTrapAndRspCon ())
	    cerr << dec <<
	      " Trap 7 " << endl << flush;
	  r0 = readGpr (0);	// buf_addr
	  r1 = readGpr (1);	// fmt_len
	  r2 = readGpr (2);	// total_len

	  //fprintf(stderr, " TRAP_OTHER %x %x", PARM0,PARM1);

	  //cerr << " buf " << hex << r0 << "  " << r1 << "  " << r2 << dec << endl << flush;

	  buf = (char *) malloc (r2);
	  for (unsigned k = 0; k < r2; k++)
	    {
	      uint8_t val_;

	      fTargetControl->readMem8 (r0 + k, val_);
	      //bool retSt = fTargetControl->readMem8(r0+k, val_);
	      buf[k] = val_;
	    }


	  strncpy (fmt, buf, r1);
	  fmt[r1] = '\0';


	  printfWrapper (res_buf, fmt, buf + r1 + 1);
	  fprintf (si->ttyOut (), "%s", res_buf);

	  targetResume ();
	}
      else
	{

	  r0 = readGpr (0);
	  r1 = readGpr (1);
	  r2 = readGpr (2);
	  r3 = readGpr (3);	//SUBFUN;

	  switch (r3)
	    {

	    case SYS_close:

	      //int close(int fd);
	      //‘Fclose, fd’
	      sprintf ((pkt->data), "Fclose,%lx", (unsigned long) r0);
	      break;

	    case SYS_open:

	      //asm_syscall(file, flags, mode, SYS_open);
	      for (k = 0; k < MAX_FILE_NAME_LENGTH - 1; k++)
		{
		  uint8_t val_;
		  fTargetControl->readMem8 (r0 + k, val_);
		  //bool retSt = fTargetControl->readMem8(r0+k, val_);
		  if (val_ == '\0')
		    {
		      break;
		    }
		}

	      //Fopen, pathptr/len, flags, mode
	      sprintf ((pkt->data), "Fopen,%lx/%d,%lx,%lx",
		       (unsigned long) r0, k,
		       (unsigned long) r1 /*O_WRONLY */ , (unsigned long) r2);
	      break;

	    case SYS_read:
	      //int read(int fd, void *buf, unsigned int count);
	      // ‘Fread, fd, bufptr, count’
	      //asm_syscall(fildes, ptr, len, SYS_read);

	      sprintf ((pkt->data), "Fread,%lx,%lx,%lx", (unsigned long) r0,
		       (unsigned long) r1, (unsigned long) r2);
	      break;


	    case SYS_write:
	      //int write(int fd, const void *buf, unsigned int count);
	      //‘Fwrite, fd, bufptr, count’
	      //asm_syscall(file, ptr, len, SYS_write);
	      sprintf ((pkt->data), "Fwrite,%lx,%lx,%lx", (unsigned long) r0,
		       (unsigned long) r1, (unsigned long) r2);
	      break;


	    case SYS_lseek:
	      //‘Flseek, fd, offset, flag’
	      //asm_syscall(fildes, offset, whence, ..)
	      sprintf ((pkt->data), "Flseek,%lx,%lx,%lx", (unsigned long) r0,
		       (unsigned long) r1, (unsigned long) r2);
	      break;

	    case SYS_unlink:
	      //‘Funlink, pathnameptr/len’
	      // asm_syscall(name, NULL, NULL, SYS_unlink);
	      for (k = 0; k < MAX_FILE_NAME_LENGTH - 1; k++)
		{
		  uint8_t val_;
		  fTargetControl->readMem8 (r0 + k, val_);
		  //bool retSt = fTargetControl->readMem8(r0+k, val_);
		  if (val_ == '\0')
		    {
		      break;
		    }
		}
	      sprintf ((pkt->data), "Funlink,%lx/%d", (unsigned long) r0, k);
	      break;

	    case SYS_stat:
	      //‘Fstat, pathnameptr/len, bufptr’
	      //_stat(const char *file, struct stat *st)
	      //asm_syscall(file, st, NULL, SYS_stat);
	      for (k = 0; k < MAX_FILE_NAME_LENGTH - 1; k++)
		{
		  uint8_t val_;
		  fTargetControl->readMem8 (r0 + k, val_);
		  //bool retSt = fTargetControl->readMem8(r0+k, val_);
		  if (val_ == '\0')
		    {
		      break;
		    }
		}
	      sprintf ((pkt->data), "Fstat,%lx/%d,%lx", (unsigned long) r0, k,
		       (unsigned long) r1);
	      break;

	    case SYS_fstat:
	      //‘Ffstat, fd, bufptr’
	      //_fstat(int fildes, struct stat *st)
	      //asm_syscall(fildes, st, NULL, SYS_fstat);

	      sprintf ((pkt->data), "Ffstat,%lx,%lx", (unsigned long) r0,
		       (unsigned long) r1);
	      if (si->debugTrapAndRspCon ())
		cerr <<
		  dec << "SYS_fstat fildes " << hex << r0 << " struct stat * "
		  << r1 << dec << endl << flush;
	      break;

	    default:
	      cerr << "ERROR: Trap 7 --- unknown SUBFUN " << r3 << endl << flush;
	      break;
	    }
	  if (si->debugTrapAndRspCon ())
	    cerr << "Trap 7: "
	      << (pkt->data) << endl << flush;

	  pkt->setLen (strlen (pkt->data));
	  rsp->putPkt (pkt);

	  //rspReportException(readPc() /* PC no trap found */, 0 /* all threads */, TARGET_SIGNAL_QUIT);
	}

      break;
    default:
      break;
    }
}


//-----------------------------------------------------------------------------
//! Handle a RSP read all registers request

//! The registers follow the GDB sequence for ATDSP: GPR0 through GPR63,
//! followed by the eight status registers (CONFIG, STATUS, PC, DEBUG, IRET,
//! ILAT, IMASK, IPEND). Each register is returned as a sequence of bytes in
//! target endian order.

//! Each byte is packed as a pair of hex digits.
//-----------------------------------------------------------------------------
void
GdbServer::rspReadAllRegs ()
{
  fTargetControl->startOfBaudMeasurement ();
  //cerr << "MTIME--- READ all regs START ----" << endl << flush;

  // The GPRs
  {
    unsigned char buf[ATDSP_NUM_GPRS * 4];
    bool retSt = fTargetControl->readBurst (CORE_R0, buf, sizeof (buf));

    for (int r = 0; r < ATDSP_NUM_GPRS; r++)
      {

	uint32_t val32;

	val32 =
	  (buf[r * 4 + 3] << (3 * 8)) | (buf[r * 4 + 2] << (2 * 8)) |
	  (buf[r * 4 + 1] << (1 * 8)) | (buf[r * 4 + 0] << (0 * 8));

	Utils::reg2Hex (val32, &(pkt->data[r * 8]));
      }

    if (!retSt)
      {
	cerr << "ERROR read all regs failed" << endl << flush;
	pkt->packStr ("E01");
	rsp->putPkt (pkt);
      }
  }

  // The SCRs
  {

    unsigned char buf[ATDSP_NUM_SCRS_0 * 4];

    bool retSt = fTargetControl->readBurst (CORE_CONFIG, buf, sizeof (buf));
    if (!retSt)
      {
	cerr << "ERROR read all regs failed" << endl << flush;
	pkt->packStr ("E01");
	rsp->putPkt (pkt);
      }

    for (int r = 0; r < ATDSP_NUM_SCRS_0; r++)
      {

	uint32_t val32;

	val32 =
	  (buf[r * 4 + 3] << (3 * 8)) | (buf[r * 4 + 2] << (2 * 8)) |
	  (buf[r * 4 + 1] << (1 * 8)) | (buf[r * 4 + 0] << (0 * 8));

	Utils::reg2Hex (val32, &(pkt->data[(ATDSP_NUM_GPRS + r) * 8]));
      }




    assert (ATDSP_NUM_SCRS_0 == ATDSP_NUM_SCRS_1);

    retSt = fTargetControl->readBurst (DMA0_CONFIG, buf, sizeof (buf));
    if (!retSt)
      {
	cerr << "ERROR read all regs failed" << endl << flush;
	pkt->packStr ("E01");
	rsp->putPkt (pkt);
      }

    for (int r = 0; r < ATDSP_NUM_SCRS_1; r++)
      {

	uint32_t val32;

	val32 =
	  (buf[r * 4 + 3] << (3 * 8)) | (buf[r * 4 + 2] << (2 * 8)) |
	  (buf[r * 4 + 1] << (1 * 8)) | (buf[r * 4 + 0] << (0 * 8));

	Utils::reg2Hex (val32,
			&(pkt->
			  data[(ATDSP_NUM_GPRS + ATDSP_NUM_SCRS_0 + r) * 8]));
      }
  }

  double mes = fTargetControl->endOfBaudMeasurement();
  if (si->debugStopResumeDetail ())
    {
      cerr << "DebugStopResumeDetail: MTIME--- READ all regs DONE "
	   << "-- milliseconds: " << mes << endl;
    }

  // Finalize the packet and send it
  pkt->data[ATDSP_TOTAL_NUM_REGS * 8] = '\0';
  pkt->setLen (ATDSP_TOTAL_NUM_REGS * 8);
  rsp->putPkt (pkt);

}	// rspReadAllRegs ()


//-----------------------------------------------------------------------------
//! Handle a RSP write all registers request

//! The registers follow the GDB sequence for ATDSP: GPR0 through GPR63,
//! followed by the eight status registers (CONFIG, STATUS, PC, DEBUG, IRET,
//! ILAT, IMASK, IPEND). Each register is supplied as a sequence of bytes in
//! target endian order.

//! Each byte is packed as a pair of hex digits.

//! @note Not believed to be used by the GDB client at present.

//! @todo There is no error checking at present. Non-hex chars will generate a
//!       warning message, but there is no other check that the right amount
//!       of data is present. The result is always "OK".
//-----------------------------------------------------------------------------
void
GdbServer::rspWriteAllRegs ()
{
  // The GPRs
  for (int r = 0; r < ATDSP_NUM_GPRS; r++)
    {
      writeGpr (r, Utils::hex2Reg (&(pkt->data[r * 8])));
    }

  // The SCRs
  for (int r = 0; r < ATDSP_NUM_SCRS; r++)
    {
      if (r < ATDSP_NUM_SCRS_0)
	{
	  writeScrGrp0 (r,
			Utils::
			hex2Reg (&(pkt->data[(ATDSP_NUM_GPRS + r) * 8])));
	}
      else
	{
	  writeScrDMA (r - ATDSP_NUM_SCRS_0,
		       Utils::
		       hex2Reg (&(pkt->data[(ATDSP_NUM_GPRS + r) * 8])));
	}

    }

  // Acknowledge (always OK for now).
  pkt->packStr ("OK");
  rsp->putPkt (pkt);

}				// rspWriteAllRegs()


//! Set the thread number of subsequent operations.

//! The thread number corresponds to the local core ID, but we can't use it
//! exactly, because then we would have thread ID '0' which means "any
//! thread", so the thread ID is core ID + 1.

//! We store this locally, but also pass it on to the target hardware. If we
//! have a thread ID which corresponds to an invalid core, then we return an
//! error.
void
GdbServer::rspSetThread ()
{
  char  c;
  int  threadId;

  if (2 != sscanf (pkt->data, "H%c%d:", &c, &threadId))
    {
      cerr << "Warning: Failed to recognize RSP set thread command: "
	   << pkt->data << endl;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }
  
  if ((c == 'c' && fTargetControl->setThreadExecute (threadId))
      || (c == 'g' && fTargetControl->setThreadGeneral (threadId)))
    {
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
    }
  else
    {
      cerr << "Warning: Failed RSP set thread command: "
	   << pkt->data << endl;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }
}	// rspSetThread ()


//-----------------------------------------------------------------------------
//! Handle a RSP read memory (symbolic) request

//! Syntax is:

//!   m<addr>,<length>:

//! The response is the bytes, lowest address first, encoded as pairs of hex
//! digits.

//! The length given is the number of bytes to be read.

//! @todo This implementation writes everything as individual bytes. A more
//!       efficient implementation would write words (where possible) and
//!       stream the accesses, since the ATDSP only supports word read/write
//!       at present.
//-----------------------------------------------------------------------------
void
GdbServer::rspReadMem ()
{
  unsigned int addr;		// Where to read the memory
  int len;			// Number of bytes to read
  int off;			// Offset into the memory

  if (2 != sscanf (pkt->data, "m%x,%x:", &addr, &len))
    {
      cerr << "Warning: Failed to recognize RSP read memory command: "
	<< pkt->data << endl << flush;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  // Make sure we won't overflow the buffer (2 chars per byte)
  if ((len * 2) >= pkt->getBufSize ())
    {
      cerr << "Warning: Memory read " << pkt->data
	<< " too large for RSP packet: truncated" << endl << flush;
      len = (pkt->getBufSize () - 1) / 2;
    }

  fTargetControl->startOfBaudMeasurement ();
  cerr << "MTIME--- READ mem START -- " << hex << addr << dec << " (" << len
       << ")" << endl;

  // Write the bytes to memory
  {
    char buf[len];

    bool retReadOp =
      fTargetControl->readBurst (addr, (unsigned char *) buf, len);

    if (!retReadOp)
      {
	pkt->packStr ("E01");
	rsp->putPkt (pkt);
	return;
      }

    // Refill the buffer with the reply
    for (off = 0; off < len; off++)
      {

	unsigned char ch = buf[off];

	pkt->data[off * 2] = Utils::hex2Char (ch >> 4);
	pkt->data[off * 2 + 1] = Utils::hex2Char (ch & 0xf);
      }


  }
  double mes = fTargetControl->endOfBaudMeasurement();
  cerr << "MTIME--- READ mem END -- milliseconds: " << mes << endl;

  pkt->data[off * 2] = '\0';	// End of string
  pkt->setLen (strlen (pkt->data));
  rsp->putPkt (pkt);

}				// rsp_read_mem()


//-----------------------------------------------------------------------------
//! Handle a RSP write memory (symbolic) request

//! Syntax is:

//!   m<addr>,<length>:<data>

//! The data is the bytes, lowest address first, encoded as pairs of hex
//! digits.

//! The length given is the number of bytes to be written.

//! @note Not believed to be used by the GDB client at present.
//-----------------------------------------------------------------------------
void
GdbServer::rspWriteMem ()
{
  uint32_t addr;		// Where to write the memory
  int len;			// Number of bytes to write

  if (2 != sscanf (pkt->data, "M%x,%x:", &addr, &len))
    {
      cerr << "Warning: Failed to recognize RSP write memory "
	<< pkt->data << endl << flush;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  // Find the start of the data and check there is the amount we expect.
  char *symDat = (char *) (memchr (pkt->data, ':', pkt->getBufSize ())) + 1;
  int datLen = pkt->getLen () - (symDat - pkt->data);

  // Sanity check
  if (len * 2 != datLen)
    {
      cerr << "Warning: Write of " << len * 2 << "digits requested, but "
	<< datLen << " digits supplied: packet ignored" << endl << flush;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  // Write the bytes to memory
  {
    //cerr << "rspWriteMem" << hex << addr << dec << " (" << len << ")" << endl << flush;
    if (!fTargetControl->writeBurst (addr, (unsigned char *) symDat, len))
      {
	pkt->packStr ("E01");
	rsp->putPkt (pkt);
	return;
      }
  }

  pkt->packStr ("OK");
  rsp->putPkt (pkt);

}				// rspWriteMem()


//-----------------------------------------------------------------------------
//! Read a single register

//! The registers follow the GDB sequence for ATDSP: GPR0 through GPR63,
//! followed by the eight status registers (CONFIG, STATUS, PC, DEBUG, IRET,
//! ILAT, IMASK, IPEND). The register is returned as a sequence of bytes in
//! target endian order.

//! Each byte is packed as a pair of hex digits.

//! @note Not believed to be used by the GDB client at present.
//-----------------------------------------------------------------------------
void
GdbServer::rspReadReg ()
{
  int regNum;

  // Break out the fields from the data
  if (1 != sscanf (pkt->data, "p%x", &regNum))
    {
      cerr << "Warning: Failed to recognize RSP read register command: "
	<< pkt->data << endl << flush;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  // Get the relevant register
  if (regNum < ATDSP_NUM_GPRS)
    {
      Utils::reg2Hex (readGpr (regNum), pkt->data);
    }
  else if (regNum < ATDSP_TOTAL_NUM_REGS)
    {
      if (regNum < ATDSP_NUM_GPRS + ATDSP_NUM_SCRS_0)
	{
	  Utils::reg2Hex (readScrGrp0 (regNum - ATDSP_NUM_GPRS), pkt->data);
	}
      else
	{
	  Utils::
	    reg2Hex (readScrDMA (regNum - ATDSP_NUM_GPRS - ATDSP_NUM_SCRS_0),
		     pkt->data);
	}
    }
  else
    {
      // Error response if we don't know the register
      cerr << "Warning: Attempt to read unknown register" << regNum
	<< ": ignored" << endl << flush;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  pkt->setLen (strlen (pkt->data));
  rsp->putPkt (pkt);

}				// rspReadReg()


//-----------------------------------------------------------------------------
//! Write a single register

//! The registers follow the GDB sequence for ATDSP: GPR0 through GPR63,
//! followed by the eight status registers (CONFIG, STATUS, PC, DEBUG, IRET,
//! ILAT, IMASK, IPEND). The register is specified as a sequence of bytes in
//! target endian order.

//! Each byte is packed as a pair of hex digits.
//-----------------------------------------------------------------------------
void
GdbServer::rspWriteReg ()
{
  int regNum;
  char valstr[9];		// Allow for EOS on the string

  // Break out the fields from the data
  if (2 != sscanf (pkt->data, "P%x=%8s", &regNum, valstr))
    {
      cerr << "Warning: Failed to recognize RSP write register command "
	<< pkt->data << endl << flush;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  // Set the relevant register
  if (regNum < ATDSP_NUM_GPRS)
    {
      writeGpr (regNum, Utils::hex2Reg (valstr));
    }
  else if (regNum < ATDSP_TOTAL_NUM_REGS)
    {
      if (regNum < ATDSP_NUM_GPRS + ATDSP_NUM_SCRS_0)
	{
	  writeScrGrp0 (regNum - ATDSP_NUM_GPRS, Utils::hex2Reg (valstr));
	}
      else
	{
	  writeScrDMA (regNum - ATDSP_NUM_GPRS - ATDSP_NUM_SCRS_0,
		       Utils::hex2Reg (valstr));
	}
    }
  else
    {
      // Error response if we don't know the register
      cerr << "Warning: Attempt to write unknown register " << regNum
	<< ": ignored" << endl << flush;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  pkt->packStr ("OK");
  rsp->putPkt (pkt);

}				// rspWriteReg()


//-----------------------------------------------------------------------------
//! Handle a RSP query request
//-----------------------------------------------------------------------------
void
GdbServer::rspQuery ()
{
  //cerr << "rspQuery " << pkt->data << endl << flush;

  if (0 == strcmp ("qC", pkt->data))
    {
      // Return the current thread ID (unsigned hex). A null response
      // indicates to use the previously selected thread. We use the constant
      // ATDSP_TID to represent our single thread of control.

      sprintf (pkt->data, "QC%x", ATDSP_TID);

      //TODO thread support - no threads...
      //sprintf(pkt->data, "QC%x", fTargetControl->GetCoreID()+1);

      pkt->setLen (strlen (pkt->data));
      rsp->putPkt (pkt);
    }
  else if (0 == strncmp ("qCRC", pkt->data, strlen ("qCRC")))
    {
      // Return CRC of memory area
      cerr << "Warning: RSP CRC query not supported" << endl << flush;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
    }
  else if (0 == strcmp ("qfThreadInfo", pkt->data))
    {
      // Return info about active threads. We return just the constant
      // ATDSP_TID to represent our single thread of control.

      //example for 2 threads TODO -- thread support
      //sprintf(pkt->data, "m%x,%x", 1, 2);

      sprintf (pkt->data, "m%x", ATDSP_TID);
      pkt->setLen (strlen (pkt->data));
      rsp->putPkt (pkt);
    }
  else if (0 == strcmp ("qsThreadInfo", pkt->data))
    {
      // Return info about more active threads. We have no more, so return the
      // end of list marker, 'l'
      pkt->packStr ("l");
      rsp->putPkt (pkt);
    }
  else if (0 == strncmp ("qGetTLSAddr:", pkt->data, strlen ("qGetTLSAddr:")))
    {
      // We don't support this feature
      pkt->packStr ("");
      rsp->putPkt (pkt);
    }
  else if (0 == strncmp ("qL", pkt->data, strlen ("qL")))
    {
      // Deprecated and replaced by 'qfThreadInfo'
      cerr << "Warning: RSP qL deprecated: no info returned" << endl << flush;
      pkt->packStr ("qM001");
      rsp->putPkt (pkt);
    }
  else if (0 == strcmp ("qOffsets", pkt->data))
    {
      // Report any relocation
      pkt->packStr ("Text=0;Data=0;Bss=0");
      rsp->putPkt (pkt);
    }
  else if (0 == strncmp ("qP", pkt->data, strlen ("qP")))
    {
      // Deprecated and replaced by 'qThreadExtraInfo'
      cerr << "Warning: RSP qP deprecated: no info returned" << endl << flush;
      pkt->packStr ("");
      rsp->putPkt (pkt);
    }
  else if (0 == strncmp ("qRcmd,", pkt->data, strlen ("qRcmd,")))
    {
      // This is used to interface to commands to do "stuff"
      rspCommand ();
    }
  else if (0 == strncmp ("qSupported", pkt->data, strlen ("qSupported")))
    {
      char const *coreExtension = "qSupported:xmlRegisters=coreid.";

      if (0 == strncmp (coreExtension, pkt->data, strlen (coreExtension)))
	cerr << "Warning: GDB setcoreid not supporte: ignored" << endl;

      // Report a list of the features we support. For now we just ignore any
      // supplied specific feature queries, but in the future these may be
      // supported as well. Note that the packet size allows for 'G' + all the
      // registers sent to us, or a reply to 'g' with all the registers and an
      // EOS so the buffer is a well formed string.
      sprintf (pkt->data, "PacketSize=%x;qXfer:osdata:read+",
	       pkt->getBufSize ());
      pkt->setLen (strlen (pkt->data));
      rsp->putPkt (pkt);
    }
  else if (0 == strncmp ("qSymbol:", pkt->data, strlen ("qSymbol:")))
    {
      // Offer to look up symbols. Nothing we want (for now). TODO. This just
      // ignores any replies to symbols we looked up, but we didn't want to
      // do that anyway!
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
    }
  else if (0 ==
	   strncmp ("qThreadExtraInfo,", pkt->data,
		    strlen ("qThreadExtraInfo,")))
    {
      //TODO --no thread support
      //rspQThreadExtraInfo();


      // Report that we are runnable, but the text must be hex ASCI
      // digits. For now do this by steam, reusing the original packet
      sprintf (pkt->data, "%02x%02x%02x%02x%02x%02x%02x%02x%02x",
	       'R', 'u', 'n', 'n', 'a', 'b', 'l', 'e', 0);
      pkt->setLen (strlen (pkt->data));
      rsp->putPkt (pkt);
    }
  else if (0 == strncmp ("qXfer:", pkt->data, strlen ("qXfer:")))
    {
      rspTransfer ();
    }
  else if (0 == strncmp ("qTStatus", pkt->data, strlen ("qTStatus")))
    {
      //Ask the stub if there is a trace experiment running right now
      //For now we support no 'qTStatus' requests
      //cerr << "Warning: RSP 'qTStatus' not supported: ignored" << endl << flush;
      pkt->packStr ("");
      rsp->putPkt (pkt);
    }
  else if (0 == strncmp ("qAttached", pkt->data, strlen ("qAttached")))
    {
      //Querying remote process attach state
      //The remote target doesn't run under any OS suppling the, dteaching and killing in will have a same effect
      //cerr << "Warning: RSP 'qAttached' not supported: ignored" << endl << flush;
      pkt->packStr ("");
      rsp->putPkt (pkt);
    }
  else
    {
      // We don't support this feature. RSP specification is to return an
      // empty packet.
      pkt->packStr ("");
      rsp->putPkt (pkt);
    }
}				// rspQuery()


//-----------------------------------------------------------------------------
//! Handle a RSP qRcmd request

//! The actual command follows the "qRcmd," in ASCII encoded to hex
//-----------------------------------------------------------------------------
void
GdbServer::rspCommand ()
{
  char cmd[RSP_PKT_MAX];

  Utils::hex2Ascii (cmd, &(pkt->data[strlen ("qRcmd,")]));

  // Say OK, so we don't stop
  sprintf (pkt->data, "OK");


  if (strcmp ("swreset", cmd) == 0)
    {

      cerr << dec <<
	"The debugger sent reset request" << endl << flush;

      //reset
      targetSwReset ();

    }
  else if (strcmp ("hwreset", cmd) == 0)
    {

      char mess[] =
	"The debugger sent HW (platfrom) reset request, please restart other debug clients.\n";

      cerr << dec << mess
	<< endl << flush;

      //HW reset (ESYS_RESET)
      targetHWReset ();

      //FIXME ???
      Utils::ascii2Hex (pkt->data, mess);

    }
  else if (strcmp ("halt", cmd) == 0)
    {

      cerr << dec <<
	"The debugger sent halt request," << endl << flush;

      //target halt
      bool isHalted = targetHalt ();
      if (!isHalted)
	{
	  rspReportException (0, 0 /*all threads */ , TARGET_SIGNAL_HUP);
	}

    }
  else if (strcmp ("run", cmd) == 0)
    {

      cerr 
<< dec <<
	"The debugger sent start request," << endl << flush;

      // target start (ILAT set)
      // ILAT set
      writeScrGrp0 (ATDSP_SCR_ILAT, ATDSP_EXCEPT_RESET);
    }
  else if (strcmp ("coreid", cmd) == 0)
    {

      uint32_t val = readCoreId ();

      char buf[256];
      sprintf (buf, "0x%x\n", val);

      Utils::ascii2Hex (pkt->data, buf);

    }
  else if (strcmp ("help", cmd) == 0)
    {

      Utils::ascii2Hex (pkt->data,
			(char *)
			"monitor commands: hwreset, coreid, swreset, halt, run, help\n");

    }
  else if (strcmp ("help-hidden", cmd) == 0)
    {

      Utils::ascii2Hex (pkt->data, (char *) "link,spi\n");

    }
  else
    {
      cerr << "Warning: received remote command " << cmd << ": ignored" <<
	endl << flush;
    }

  pkt->setLen (strlen (pkt->data));
  rsp->putPkt (pkt);

}				// rspCommand()


//-----------------------------------------------------------------------------
//! Handle a RSP qXfer request

//! The actual format is one of:
//! - "qXfer:<object>:read:<annex>:<offset>,<length>"
//! - "qXfer:<object>:write:<annex>:<offset>,<data>"

//! We only support a small subset.
//-----------------------------------------------------------------------------
void
GdbServer::rspTransfer ()
{
  stringstream   ss (pkt->data);
  vector<string> tokens;		// To break out the packet elements
  string         item;

  // Break out the packet
  while (getline (ss, item, ':'))
    tokens.push_back (item);

  // Break out offset/length or offset/data, which are comma separated.
  if (5 == tokens.size ())
    {
      ss.str (tokens[4]);
      ss.clear ();
      tokens.pop_back ();		// Remove offset/{length,data}

      // Break out the packet
      while (getline (ss, item, ','))
	tokens.push_back (item);
    }
      
  if (si->debugTrapAndRspCon ())
    {
      for (unsigned int i = 0; i < tokens.size (); i++)
	{
	  cerr << "RSP trace: qXfer: tokens[" << i << "] = " << tokens[i]
	       << "." << endl;
	}
    }

  // Default is to return an empty packet, indicating
  // unsupported/unrecognized.
  pkt->packStr ("");

  // See if we recognize anything
  if ((6 == tokens.size ())
      && (0 == tokens[2].compare ("read"))
      && (0 != tokens[4].size ())
      && (0 != tokens[5].size ()))
    {
      // All the read qXfers
      string object = tokens[1];
      string annex = tokens[3];
      unsigned int  offset;
      unsigned int  length;

      // Convert the offset and length.
      ss.str ("");
      ss.clear ();
      ss << hex << tokens[4];
      ss >> offset;
      ss.str ("");
      ss.clear ();
      ss << hex << tokens[5];
      ss >> length;

      if (si->debugTrapAndRspCon ())
	{
	  cerr << "RSP trace: qXfer, object = \"" << object
	       << "\", read, annex = \"" << annex << "\", offset = 0x"
	       << hex << offset << ", length = 0x" << length << dec << endl;
	}

      // Sort out what we have
      if (0 == object.compare ("osdata"))
	{
	  if (0 == annex.compare ("process"))
	    rspOsDataProcesses (offset, length);
	  else if (0 == annex.compare ("load"))
	    rspOsDataLoad (offset, length);
	  else if (0 == annex.compare ("traffic"))
	    rspOsDataTraffic (offset, length);
	}
    }
  else if ((6 == tokens.size ())
	   && (0 == tokens[2].compare ("write"))
	   && (0 != tokens[4].size ()))
    {
      string object = tokens[1];
      string annex = tokens[3];
      unsigned int  offset;
      string data = tokens[5];

      // Convert the offset.
      ss.str ("");
      ss.clear ();
      ss << hex << tokens[4];
      ss >> offset;

      // All the write qXfers. Currently none supported
      if (si->debugTrapAndRspCon ())
	cerr << "RSP trace: qXfer, object = \"" << object
	     << ", write, annex = \"" << annex << "\", offset = 0x" << hex
	     << offset << dec <<  ", data = " << data << endl;
    }
  else
    if (si->debugTrapAndRspCon ())
      cerr << "RSP trace: qXfer unrecognzed." << endl;

  // Push out the packet
  rsp->putPkt (pkt);

}	// rspTransfer ()


//-----------------------------------------------------------------------------
//! Handle an OS processes request

//! We need to return standard data, at this stage with all the cores. The
//! header and trailer part of the response is fixed.

//! @param[in] offset  Offset into the reply to send.
//! @param[in] length  Length of the reply to send.
//-----------------------------------------------------------------------------
void
GdbServer::rspOsDataProcesses (unsigned int offset,
			       unsigned int length)
{
  if (si->debugTrapAndRspCon ())
    {
      cerr << "RSP trace: qXfer:osdata:read:process offset 0x" << hex
	   << offset << ", length " << length << dec << endl;
    }

  // Get the data only for the first part of the reply. The rest of the time
  // we are just sending the remainder of the string.
  if (0 == offset)
    {
      osProcessReply =
	"<?xml version=\"1.0\"?>\n"
	"<!DOCTYPE target SYSTEM \"osdata.dtd\">\n"
	"<osdata type=\"processes\">\n"
	"  <item>\n"
	"    <column name=\"pid\">1</column>\n"
	"    <column name=\"user\">root</column>\n"
	"    <column name=\"command\"></column>\n"
	"    <column name=\"cores\">\n"
	"      ";

      vector <uint16_t> cores = fTargetControl->listCoreIds ();
      vector <uint16_t>::iterator  it;

      for (it = cores.begin (); it != cores.end (); it++)
	{
	  if (it != cores.begin ())
	    osProcessReply += ", ";

	  osProcessReply += intStr (*it);
	}

      osProcessReply += "\n"
	"    </column>\n"
	"  </item>\n"
	"  </osdata>";
    }

  // Send the reply (or part reply) back
  unsigned int  len = osProcessReply.size ();

  if (si->debugTrapAndRspCon ())
    {
      cerr << "RSP trace: OS process info length " << len << endl;
      cerr << osProcessReply << endl;
    }

  if (offset >= len)
    pkt->packStr ("l");
  else
    {
      unsigned int pktlen = len - offset;
      char pkttype = 'l';

      if (pktlen > length)
	{
	  /* Will need more packets */
	  pktlen = length;
	  pkttype = 'm';
	}

      pkt->packNStr (&(osProcessReply.c_str ()[offset]), pktlen, pkttype);
    }
}	// rspOsDataProcesses ()


//-----------------------------------------------------------------------------
//! Handle an OS core load request

//! This is epiphany specific.

//! @todo For now this is a stub which returns random values in the range 0 -
//! 99 for each core.

//! @param[in] offset  Offset into the reply to send.
//! @param[in] length  Length of the reply to send.
//-----------------------------------------------------------------------------
void
GdbServer::rspOsDataLoad (unsigned int offset,
			     unsigned int length)
{
  if (si->debugTrapAndRspCon ())
    {
      cerr << "RSP trace: qXfer:osdata:read:load offset 0x" << hex << offset
	   << ", length " << length << dec << endl;
    }

  // Get the data only for the first part of the reply. The rest of the time
  // we are just sending the remainder of the string.
  if (0 == offset)
    {
      osLoadReply =
	"<?xml version=\"1.0\"?>\n"
	"<!DOCTYPE target SYSTEM \"osdata.dtd\">\n"
	"<osdata type=\"load\">\n";

      vector <uint16_t> cores = fTargetControl->listCoreIds ();
      vector <uint16_t>::iterator  it;

      for (it = cores.begin (); it != cores.end (); it++)
	{
	  osLoadReply +=
	    "  <item>\n"
	    "    <column name=\"coreid\">";
	  osLoadReply += intStr (*it, 8, 4);
	  osLoadReply += "</column>\n";

	  osLoadReply +=
	    "    <column name=\"load\">";
	  osLoadReply += intStr (random () % 100, 10, 2);
	  osLoadReply += "</column>\n"
	    "  </item>\n";
	}

      osLoadReply += "</osdata>";

      if (si->debugTrapAndRspCon ())
	{
	  cerr << "RSP trace: OS load info length "
	       << osLoadReply.size () << endl;
	  cerr << osLoadReply << endl;
	}
    }

  // Send the reply (or part reply) back
  unsigned int  len = osLoadReply.size ();

  if (si->debugTrapAndRspCon ())
    {
      cerr << "RSP trace: OS load info length " << len << endl;
      cerr << osLoadReply << endl;
    }

  if (offset >= len)
    pkt->packStr ("l");
  else
    {
      unsigned int pktlen = len - offset;
      char pkttype = 'l';

      if (pktlen > length)
	{
	  /* Will need more packets */
	  pktlen = length;
	  pkttype = 'm';
	}

      pkt->packNStr (&(osLoadReply.c_str ()[offset]), pktlen, pkttype);
    }
}	// rspOsDataLoad ()


//-----------------------------------------------------------------------------
//! Handle an OS mesh load request

//! This is epiphany specific.

//! When working out "North", "South", "East" and "West", the assumption is
//! that core (0,0) is at the North-East corner. We provide in and out traffic
//! for each direction.

//! @todo Currently only dummy data.

//! @param[in] offset  Offset into the reply to send.
//! @param[in] length  Length of the reply to send.
//-----------------------------------------------------------------------------
void
GdbServer::rspOsDataTraffic (unsigned int offset,
			     unsigned int length)
{
  if (si->debugTrapAndRspCon ())
    {
      cerr << "RSP trace: qXfer:osdata:read:traffic offset 0x" << hex << offset
	   << ", length " << length << dec << endl;
    }

  // Get the data only for the first part of the reply. The rest of the time
  // we are just sending the remainder of the string.
  if (0 == offset)
    {
      osTrafficReply =
	"<?xml version=\"1.0\"?>\n"
	"<!DOCTYPE target SYSTEM \"osdata.dtd\">\n"
	"<osdata type=\"traffic\">\n";

      unsigned int maxRow = fTargetControl->getNumRows () - 1;
      unsigned int maxCol = fTargetControl->getNumCols () - 1;
      vector <uint16_t> cores = fTargetControl->listCoreIds ();
      vector <uint16_t>::iterator  it;

      for (it = cores.begin (); it != cores.end (); it++)
	{
	  uint16_t coreId = *it;
	  unsigned int row = (coreId >> 6) & 0x3f;
	  unsigned int col = coreId & 0x3f;
	  string inTraffic;
	  string outTraffic;

	  osTrafficReply +=
	    "  <item>\n"
	    "    <column name=\"coreid\">";
	  osTrafficReply += intStr (coreId, 8, 4);
	  osTrafficReply += "</column>\n";

	  // See what adjacent cores we have. Note that empty columns confuse
	  // GDB!
	  if (row > 0)
	    {
	      inTraffic = intStr (random () % 100, 10, 2);
	      outTraffic = intStr (random () % 100, 10, 2);
	    }
	  else
	    {
	      inTraffic = "--";
	      outTraffic = "--";
	    }
		  
	  osTrafficReply +=
	    "    <column name=\"North In\">";
	  osTrafficReply += inTraffic;
	  osTrafficReply += "</column>\n"
	    "    <column name=\"North Out\">";
	  osTrafficReply += outTraffic;
	  osTrafficReply += "</column>\n";

	  if (row < maxRow)
	    {
	      inTraffic = intStr (random () % 100, 10, 2);
	      outTraffic = intStr (random () % 100, 10, 2);
	    }
	  else
	    {
	      inTraffic = "--";
	      outTraffic = "--";
	    }

	  osTrafficReply +=
	    "    <column name=\"South In\">";
	  osTrafficReply += inTraffic;
	  osTrafficReply += "</column>\n"
	    "    <column name=\"South Out\">";
	  osTrafficReply += outTraffic;
	  osTrafficReply += "</column>\n";

	  if (col < maxCol)
	    {
	      inTraffic = intStr (random () % 100, 10, 2);
	      outTraffic = intStr (random () % 100, 10, 2);
	    }
	  else
	    {
	      inTraffic = "--";
	      outTraffic = "--";
	    }

	  osTrafficReply +=
	    "    <column name=\"East In\">";
	  osTrafficReply += inTraffic;
	  osTrafficReply += "</column>\n"
	    "    <column name=\"East Out\">";
	  osTrafficReply += outTraffic;
	  osTrafficReply += "</column>\n";

	  if (col > 0)
	    {
	      inTraffic = intStr (random () % 100, 10, 2);
	      outTraffic = intStr (random () % 100, 10, 2);
	    }
	  else
	    {
	      inTraffic = "--";
	      outTraffic = "--";
	    }

	  osTrafficReply +=
	    "    <column name=\"West In\">";
	  osTrafficReply += inTraffic;
	  osTrafficReply += "</column>\n"
	    "    <column name=\"West Out\">";
	  osTrafficReply += outTraffic;
	  osTrafficReply += "</column>\n"
	    "  </item>\n";
	}

      osTrafficReply += "</osdata>";

      if (si->debugTrapAndRspCon ())
	{
	  cerr << "RSP trace: OS traffic info length "
	       << osTrafficReply.size () << endl;
	  cerr << osTrafficReply << endl;
	}
    }

  // Send the reply (or part reply) back
  unsigned int  len = osTrafficReply.size ();

  if (offset >= len)
    pkt->packStr ("l");
  else
    {
      unsigned int pktlen = len - offset;
      char pkttype = 'l';

      if (pktlen > length)
	{
	  /* Will need more packets */
	  pktlen = length;
	  pkttype = 'm';
	}

      pkt->packNStr (&(osTrafficReply.c_str ()[offset]), pktlen, pkttype);
    }
}	// rspOsDataTraffic ()


//-----------------------------------------------------------------------------
//! Handle a RSP set request
//-----------------------------------------------------------------------------
void
GdbServer::rspSet ()
{
  if (0 == strncmp ("QPassSignals:", pkt->data, strlen ("QPassSignals:")))
    {
      // Passing signals not supported
      pkt->packStr ("");
      rsp->putPkt (pkt);
    }
  else if ((0 == strcmp ("QTStart", pkt->data)))
    {
      if (fTargetControl->startTrace ())
	{
	  pkt->packStr ("OK");
	  rsp->putPkt (pkt);
	}
      else
	{
	  pkt->packStr ("");
	  rsp->putPkt (pkt);
	}
    }
  else if ((0 == strcmp ("QTStop", pkt->data)))
    {
      if (fTargetControl->stopTrace ())
	{
	  pkt->packStr ("OK");
	  rsp->putPkt (pkt);
	}
      else
	{
	  pkt->packStr ("");
	  rsp->putPkt (pkt);
	}
    }
  else if ((0 == strcmp ("QTinit", pkt->data)))
    {
      if (fTargetControl->initTrace ())
	{
	  pkt->packStr ("OK");
	  rsp->putPkt (pkt);
	}
      else
	{
	  pkt->packStr ("");
	  rsp->putPkt (pkt);
	}
    }
  else if ((0 == strncmp ("QTDP", pkt->data, strlen ("QTDP"))) ||
	   (0 == strncmp ("QFrame", pkt->data, strlen ("QFrame"))) ||
	   (0 == strncmp ("QTro", pkt->data, strlen ("QTro"))))
    {
      // All tracepoint features are not supported. This reply is really only
      // needed to 'QTDP', since with that the others should not be
      // generated.

      // TODO support trace .. VCD dump
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
    }
  else
    {
      cerr << "Unrecognized RSP set request: ignored" << endl << flush;
      delete pkt;
    }
}				// rspSet()


//-----------------------------------------------------------------------------
//! Handle a RSP restart request

//! For now we just put the program counter back to zero. If we supported the
//! vRun request, we should use the address specified there. There is no point
//! in unstalling the processor, since we'll never get control back.
//-----------------------------------------------------------------------------
void
GdbServer::rspRestart ()
{
  writePc (0);

}				// rspRestart()


//-----------------------------------------------------------------------------
//! Handle a RSP step request

//! This version is typically used for the 's' packet, to continue without
//! signal, in which case TARGET_SIGNAL_NONE is passed in as the exception to use.

//! @param[in] except  The exception to use. Only TARGET_SIGNAL_NONE should be set
//!                    this way.
//-----------------------------------------------------------------------------
void
GdbServer::rspStep (uint32_t except)
{
  uint32_t addr;		// The address to step from, if any

  // Reject all except 's' packets
  if ('s' != pkt->data[0])
    {
      cerr << "Warning: Step with signal not currently supported: "
	<< "ignored" << endl << flush;
      return;
    }

  if (0 == strcmp ("s", pkt->data))
    {
      addr = readPc ();		// Default uses current PC
    }
  else if (1 != sscanf (pkt->data, "s%x", &addr))
    {
      cerr << "Warning: RSP step address " << pkt->data
	<< " not recognized: ignored" << endl << flush;
      addr = readPc ();		// Default uses current PC
    }

  rspStep (addr, TARGET_SIGNAL_NONE);

}				// rspStep()


//-----------------------------------------------------------------------------
//! Handle a RSP step with signal request

//! @todo Currently null. Will use the underlying generic step function.
//-----------------------------------------------------------------------------
void
GdbServer::rspStep ()
{
  cerr << "WARNING: RSP step with signal '" << pkt->
    data << "' received, the server will ignore the step" << endl << flush;

  //return the same exception
  rsp->putPkt (pkt);

}				// rspStep()


//---------------------------------------------------------------------------
//! Test if we have a 32-bit instruction in our hand.

//! @param[in] iab_instr  The instruction to test
//! @return TRUE if this is a 32-bit instruction, FALSE otherwise.
//---------------------------------------------------------------------------
bool
GdbServer::is32BitsInstr (uint32_t iab_instr)
{

  bool de_extended_instr = (getfield (iab_instr, 3, 0) == uint8_t (0xf));

  bool de_regi = (getfield (iab_instr, 2, 0) == uint8_t (3));
  bool de_regi_long = de_regi && (getfield (iab_instr, 3, 3) == 1);

  bool de_loadstore = (getfield (iab_instr, 2, 0) == uint8_t (0x4))
    || (getfield (iab_instr, 1, 0) == uint8_t (1));
  bool de_loadstore_long = de_loadstore && (getfield (iab_instr, 3, 3) == 1);

  bool de_branch = (getfield (iab_instr, 2, 0) == uint8_t (0));
  bool de_branch_long_sel = de_branch && (getfield (iab_instr, 3, 3) == 1);

  bool res = (de_extended_instr ||	// extension
	      de_loadstore_long ||	// long load/store
	      de_regi_long ||	// long imm reg
	      de_branch_long_sel);	// long branch

  return res;

}	// is32BitsInstr ()


//! Created as a wrapper to overcome external memory problems.

//! The original comment labelled this as NOT IN USE, but it is evidently used
//! in this class.
void
GdbServer::printfWrapper (char *result_str, const char *fmt,
			  const char *args_buf)
{
  char *p = (char *) fmt;
  char *b = (char *) fmt;
  //char *perc;        = (char *) fmt;
  char *p_args_buf = (char *) args_buf;

  unsigned a, a1, a2, a3, a4;

  int found_percent = 0;

  char buf[2048];
  char tmp_str[2048];

  strcpy (result_str, "");
  //sprintf(result_str, "");

  //sprintf(buf, fmt);

  //printf("fmt ----%d ----\n", strlen(fmt));

  //puts(fmt);

  //printf("Parsing\n");

  while (*p)
    {
      if (*p == '%')
	{
	  found_percent = 1;
	  //perc = p;
	}
      else if (*p == 's' && (found_percent == 1))
	{
	  found_percent = 0;

	  strncpy (buf, b, (p - b) + 1);
	  buf[p - b + 1] = '\0';
	  b = p + 1;

	  //puts(buf);

	  //printf("args_buf ----%d ----\n", strlen(p_args_buf));
	  //puts(p_args_buf);

	  sprintf (tmp_str, buf, p_args_buf);
	  sprintf (result_str, "%s%s", result_str, tmp_str);

	  p_args_buf = p_args_buf + strlen (p_args_buf) + 1;

	}
      else
	if ((*p == 'p' || *p == 'X' || *p == 'u' || *p == 'i' || *p == 'd'
	     || *p == 'x' || *p == 'f') && (found_percent == 1))
	{
	  found_percent = 0;

	  strncpy (buf, b, (p - b) + 1);
	  buf[p - b + 1] = '\0';
	  b = p + 1;

	  //print out buf
	  //puts(buf);

	  a1 = (p_args_buf[0]);
	  a1 &= 0xff;
	  a2 = (p_args_buf[1]);
	  a2 &= 0xff;
	  a3 = (p_args_buf[2]);
	  a3 &= 0xff;
	  a4 = (p_args_buf[3]);
	  a4 &= 0xff;

	  //printf("INT <a1> %x <a2> %x <a3> %x <a4> %x\n", a1, a2,a3, a4);
	  a = ((a1 << 24) | (a2 << 16) | (a3 << 8) | a4);

	  if (*p == 'i')
	    {
	      //printf("I %i\n", a);
	    }
	  else if (*p == 'd')
	    {
	      //printf("D %d\n", a);
	    }
	  else if (*p == 'x')
	    {
	      //printf("X %x\n", a);
	    }
	  else if (*p == 'f')
	    {
	      //printf("F %f \n", *((float*)&a));
	    }
	  else if (*p == 'f')
	    {
	      sprintf (tmp_str, buf, *((float *) &a));
	    }
	  else
	    {
	      sprintf (tmp_str, buf, a);
	    }
	  sprintf (result_str, "%s%s", result_str, tmp_str);

	  p_args_buf = p_args_buf + 4;
	}

      p++;
    }

  //tail
  //puts(b);
  sprintf (result_str, "%s%s", result_str, b);

  //printf("------------- %s ------------- %d ", result_str, strlen(result_str));
}	// printf_wrappper ()


//! Halt the target

//! Done by putting the processor into debug mode.

//! @return  TRUE if we halt successfully, FALSE otherwise.
bool
GdbServer::targetHalt ()
{
  if (!fTargetControl->writeMem32 (CORE_DEBUGCMD, ATDSP_DEBUG_HALT))
    cerr << "Warning: targetHalt failed to write HALT to DEBUGCMD." << endl;

  if (si->debugStopResume ())
      cerr << "DebugStopResume: Write HALT to DEBUGCMD" << endl;

  if (!isTargetInDebugState ())
    {
      sleep (1);
    }

  if (!isTargetInDebugState ())
    {
      cerr << "Warning: Target has not halted after 1 sec " << endl;
      uint32_t val;
      if (fTargetControl->readMem32 (CORE_DEBUG, val))
	{
	  cerr << "           DEBUG= 0x" << hex << setw (8) << setfill ('0')
	       << val << setfill (' ') << setw (0) << dec << endl;
	}
      else
	cerr << "            Unable to access DEBUG register." << endl;

      return false;

    }

  if (si->debugStopResume ())
    cerr << "DebugStopResume: Target halted." << flush;

  return true;

}	// targetHalt ()


//---------------------------------------------------------------------------
//! Put Breakpoint instruction
//
//-----------------------------------------------------------------------------
void
GdbServer::putBreakPointInstruction (unsigned long bkpt_addr)
{
  fTargetControl->writeMem16 (bkpt_addr, ATDSP_BKPT_INSTR);

  if (si->debugStopResumeDetail ())
    cerr <<
      " put break point " << hex << bkpt_addr << " " << ATDSP_BKPT_INSTR <<
      dec << endl << flush;

}


//---------------------------------------------------------------------------
//! Check if hit on Breakpoint instruction
//
//-----------------------------------------------------------------------------
bool
GdbServer::isHitInBreakPointInstruction (unsigned long bkpt_addr)
{
  uint16_t val;
  fTargetControl->readMem16 (bkpt_addr, val);
  //bool st = fTargetControl->readMem16(bkpt_addr, val);
  return (ATDSP_BKPT_INSTR == val);

}


//-----------------------------------------------------------------------------
//! Check is core has been stopped at debug state
bool
GdbServer::isTargetInDebugState ()
{

  uint32_t val;
  fTargetControl->readMem32 (CORE_DEBUG, val);
  //bool retSt = fTargetControl->readMem32(CORE_DEBUG, val);

  uint32_t valueOfDebugReg = val;

  bool ret = ((getfield (valueOfDebugReg, 0, 0) == ATDSP_DEBUG_HALT)
	      && (getfield (valueOfDebugReg, 1, 1) == ATDSP_OUT_TRAN_FALSE));

  return ret;
}


//-----------------------------------------------------------------------------
//! Check is core has been stopped at exception state
bool
GdbServer::isTargetExceptionState (unsigned &exCause)
{

  bool ret = false;

  //check if idle state
  uint32_t coreStatus = readCoreStatus ();
  uint32_t exStat = getfield (coreStatus, 18, 16);
  if (exStat != 0)
    {

      ret = true;
      //cerr << "Exception " << hex << coreStatus(18,16) << endl << flush;

      exCause = TARGET_SIGNAL_ABRT;

      if (exStat == E_UNALIGMENT_LS)
	{
	  exCause = TARGET_SIGNAL_BUS;
	}
      if (exStat == E_FPU)
	{
	  exCause = TARGET_SIGNAL_FPE;
	}
      if (exStat == E_UNIMPL)
	{
	  exCause = TARGET_SIGNAL_ILL;
	}
    }

  return ret;
}


//-----------------------------------------------------------------------------
//! Check is core has been stopped at idle state
bool
GdbServer::isTargetInIldeState ()
{
  bool ret = false;

  //check if idle state
  uint32_t coreStatus = readCoreStatus ();
  if (getfield (coreStatus, 18, 16) != 0)
    {
      cerr << "EXception " << hex << getfield (coreStatus, 18,
					       16) << endl << flush;
    }

  //cerr << " CORE status" << hex << coreStatus << endl << flush;

  if ((coreStatus & CORE_IDLE_BIT) == CORE_IDLE_VAL)
    {

      ret = true;

      //cerr << " IDLE state " << endl << flush;

      //get instruction and check if we are in ilde
    }

  return ret;
}


//-----------------------------------------------------------------------------
//! Put bkpt instructions to IVT

// The single step mode can be broken when interrupt is fired. (ISR call)
// The instructions in IVT should be saved and replaced by BKPT
void
GdbServer::saveIVT ()
{
  fTargetControl->readBurst (0, fIVTSaveBuff, sizeof (fIVTSaveBuff));

  //for (unsigned i=1; i<ATDSP_NUM_ENTRIES_IN_IVT-1; i++) { //skip reset ISR
  //      uint32_t bkpt_addr = i * ATDSP_INST32LEN;
  //
  //      if (mpHash->lookup(BP_MEMORY,bkpt_addr) == NULL) {
  //              uint16_t val16t;
  //              bool stvlBpMem = fTargetControl->readMem16(bkpt_addr, val16t);
  //              uint<16> vlBpMem = val16t;
  //
  //              mpHash->add(BP_MEMORY, bkpt_addr, vlBpMem);
  //
  //              putBreakPointInstruction(bkpt_addr);
  //      }
  //}
}


//-----------------------------------------------------------------------------
//! Restore bkpt instructions to IVT

// The single step mode can be broken when interrupt is fired, (ISR call)
// The BKPT instructions in IVT should be restored by real instructions
void
GdbServer::restoreIVT ()
{

  fTargetControl->writeBurst (0, fIVTSaveBuff, sizeof (fIVTSaveBuff));
  //remove "hidden" bk
  //for (unsigned i=1; i<ATDSP_NUM_ENTRIES_IN_IVT-1; i++) { // skip reset ISR
  //      uint32_t bkpt_addr = i*ATDSP_INST32LEN;
  //      uint16_t instr_saved;
  //      if (mpHash->remove(BP_MEMORY, bkpt_addr, &instr_saved)) {
  //              fTargetControl->writeMem16(bkpt_addr, instr_saved);
  //      }
  //}
}


//-----------------------------------------------------------------------------
//! Generic processing of a step request

//! The signal may be TARGET_SIGNAL_NONE if there is no exception to be
//! handled. Currently the exception is ignored.

//! The single step flag is set in the debug registers which has the effect of
//! unstalling the processor for one instruction.

//! Flush the SCR cache

//! @param[in] addr    Address from which to step
//! @param[in] except  The exception to use (if any)
//-----------------------------------------------------------------------------
void
GdbServer::rspStep (uint32_t addr, uint32_t except)
{
  if (si->debugStopResumeDetail ())
    cerr << dec <<
      "GdbServer::rspStep PC 0x" << hex << addr << dec << endl << flush;

  //check if core in debug state
  if (!isTargetInDebugState ())
    {
      cerr <<
	"e-server Internal Error: Assertion failed: The step request can not be acknowledged when the core is not in HALT state (non stopped)"
	<< endl << flush;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      exit (8);
    }

  //get PC
  unsigned reportedPc = readPc ();

  unsigned exCause;

  //check the exception state
  bool isExState = isTargetExceptionState (exCause);

  if (isExState)
    {
      //stopped due to some exception -- just report to gdb and return -- - can't step --the silicon problem
      rspReportException (reportedPc, 0 /*all threads */ , exCause);
      return;
    }

  //fetch instruction opcode on PC
  uint16_t val16;
  fTargetControl->readMem16 (reportedPc, val16);
  //bool st1 = fTargetControl->readMem16(reportedPc, val16);
  uint16_t instrOpcode = val16;

  //Skip/Care Idle instruction
  bool stoppedAtIdleInstr = (getfield (instrOpcode, 8, 0) == IDLE_OPCODE);
  if (stoppedAtIdleInstr)
    {
      cerr << "POINT on IDLE " << " ADDR " << hex << reportedPc << dec << endl
	<< flush;

      //check if global ISR enable state
      uint32_t coreStatus = readCoreStatus ();

      uint32_t imaskReg = readScrGrp0 (ATDSP_SCR_IMASK);
      uint32_t ilatReg = readScrGrp0 (ATDSP_SCR_ILAT);

      //next cycle should be jump to IVT
      if (getfield (coreStatus, 1, 1) == 0 /*global ISR enable */  &&
	  (((~imaskReg) & ilatReg) != 0))
	{

	  //take care of ISR call
	  saveIVT ();

	  for (unsigned i = 1; i < ATDSP_NUM_ENTRIES_IN_IVT; i++)
	    {			// skip reset ISR
	      putBreakPointInstruction (i * ATDSP_INST32LEN);
	    }

	  //do step

	  //resume
	  targetResume ();
	  while (true)
	    {
	      if (isTargetInDebugState ())
		{
		  break;
		}
	    }
	  //restore IVT
	  restoreIVT ();
	  readCoreStatus ();
	  //uint32_t coreStatus = readCoreStatus();

	  readScrGrp0 (ATDSP_SCR_IMASK);
	  //uint32_t imaskReg = readScrGrp0(ATDSP_SCR_IMASK);
	  readScrGrp0 (ATDSP_SCR_ILAT);
	  //uint32_t ilatReg  = readScrGrp0(ATDSP_SCR_ILAT);
	}

      // report to gdb the target has been stopped
      unsigned pc_ = readPc () - ATDSP_BKPT_INSTLEN;
      writePc (pc_);
      rspReportException (pc_, 0 /*all threads */ , TARGET_SIGNAL_TRAP);

      return;
    }

  //Execute the instruction trap
  bool stoppedAtTrap = (getfield (instrOpcode, 9, 0) == ATDSP_TRAP_INSTR);
  if (stoppedAtTrap)
    {

      fIsTargetRunning = false;

      uint8_t trapNumber = getfield (instrOpcode, 15, 10);
      redirectSdioOnTrap (trapNumber);
      //increment pc by size of TRAP instruction
      writePc (addr + ATDSP_TRAP_INSTLEN);
      return;
    }

  //set PC
  writePc (addr);

  //fetch PC
  uint32_t pc_ = readPc ();

  //check if core in debug state
  if ((addr != pc_))
    {
      cerr << "e-server Internal Error: PC access failure" << endl << flush;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      exit (8);
    }


  if (si->debugStopResumeDetail ())
    cerr << dec <<
      " get PC " << hex << pc_ << endl << flush;

  //fetch instruction opcode on PC

  fTargetControl->readMem16 (pc_, val16);
  //st1 = fTargetControl->readMem16(pc_, val16);
  instrOpcode = val16;

  fTargetControl->readMem16 (pc_ + 2, val16);
  //bool st2 = fTargetControl->readMem16(pc_+2, val16);
  uint16_t instrExt = val16;

  if (si->debugStopResumeDetail ())
    cerr << dec <<
      " opcode 0x" << hex << instrOpcode << dec << endl << flush;

  uint32_t bkpt_addr = addr + 2;	//put breakpoint to addr + instruction length

  bool is32 = is32BitsInstr (instrOpcode);
  if (is32)
    {
      bkpt_addr += 2;		//this is extension: 4 bytes instruction
    }

  //put sequential breakpoint

  if (mpHash->lookup (BP_MEMORY, bkpt_addr) == NULL)
    {
      uint16_t bpVal_;
      fTargetControl->readMem16 (bkpt_addr, bpVal_);
      //bool st1 = fTargetControl->readMem16(bkpt_addr, bpVal_);
      mpHash->add (BP_MEMORY, bkpt_addr, bpVal_);
    }
  if (si->debugTrapAndRspCon ())
    cerr << dec <<
      "put (SEQ) bkpt on 0x" << hex << bkpt_addr << dec << endl << flush;
  putBreakPointInstruction (bkpt_addr);


  //put breakpoint to jump target in case of change of flow
  uint32_t bkpt_jump_addr = bkpt_addr;

  //check if jump by value
  if (getfield (instrOpcode, 2, 0) == 0)
    {
      uint32_t immExt = 0;
      setfield (immExt, 7, 0, getfield (instrOpcode, 15, 8));
      if (is32)
	{
	  setfield (immExt, 23, 8, getfield (instrExt, 15, 0));
	  if (getfield (immExt, 23, 23) == 1)
	    {
	      setfield (immExt, 31, 24, 0xff);
	    }
	}
      else
	{
	  if (getfield (immExt, 7, 7) == 1)
	    {
	      setfield (immExt, 31, 8, 0xffffff);
	    }
	}

      long jAddr = long (pc_) + ((long (immExt)) <<1);
      bkpt_jump_addr = jAddr;

      //cerr << " calculated Jump based on ImmExt 0x" << hex << immExt << dec << endl << flush;
    }

  //RTI
  if (getfield (instrOpcode, 8, 0) == 0x1d2)
    {
      bkpt_jump_addr = readScrGrp0 (ATDSP_SCR_IRET);
      //cerr << "RTI " << hex << bkpt_jump_addr << dec << endl << flush;
    }

  //check if jump by reg
  //16 bits jump
  if (getfield (instrOpcode, 8, 0) == 0x142
      || getfield (instrOpcode, 8, 0) == 0x152)
    {
      uint8_t regShortNum = getfield (instrOpcode, 12, 10);
      bkpt_jump_addr = readGpr (regShortNum);
      //cerr << "PC <-< " << regShortNum << endl << flush;
    }
  //32 bits jump
  if (getfield (instrOpcode, 8, 0) == 0x14f
      || getfield (instrOpcode, 8, 0) == 0x15f)
    {
      uint8_t regLongNum;
      regLongNum =
	(getfield (instrExt, 12, 10) << 3) | (getfield (instrOpcode, 12, 10)
					      << 0);
      bkpt_jump_addr = readGpr (regLongNum);
      //cerr << "PC <-< " << regLongNum << endl << flush;
    }

  //take care of change of flow
  if (bkpt_jump_addr != bkpt_addr)
    {
      if (si->debugStopResumeDetail ())
	cerr << dec <<
	  "put bkpt on (change of flow) " << hex << bkpt_jump_addr << dec <<
	  endl << flush;
      if (mpHash->lookup (BP_MEMORY, bkpt_jump_addr) == NULL)
	{

	  uint16_t val16t;
	  fTargetControl->readMem16 (bkpt_jump_addr, val16t);
	  //bool stvlBpMem = fTargetControl->readMem16(bkpt_jump_addr, val16t);
	  uint16_t vlBpMem = val16t;

	  mpHash->add (BP_MEMORY, bkpt_jump_addr, vlBpMem);
	}
      if (si->debugStopResumeDetail ())
	cerr << dec <<
	  "put (JMP) bkpt on 0x" << hex << bkpt_jump_addr << dec << endl <<
	  flush;

      putBreakPointInstruction (bkpt_jump_addr);

    }
  //take care of ISR call
  saveIVT ();
  for (unsigned i = 1; i < ATDSP_NUM_ENTRIES_IN_IVT; i++)
    {				//skip reset ISR
      uint32_t bkpt_addr = i * ATDSP_INST32LEN;
      if (pc_ != bkpt_addr)
	{			// don't overwrite the PC
	  putBreakPointInstruction (bkpt_addr);
	}
    }

  //do step

  //resume
  targetResume ();

  if (si->debugTrapAndRspCon ())
    cerr << dec <<
      " resume at PC " << hex << readPc () << endl << flush;
  if (si->debugStopResumeDetail ())
    {
      uint32_t pcReadVal;
      fTargetControl->readMem32 (readPc (), pcReadVal);
      //bool pcReadSt = fTargetControl->readMem32(readPc(), pcReadVal);

      cerr << dec <<
	" opcode << " << pcReadVal << dec << endl << flush;
    }


  while (true)
    {
      if (isTargetInDebugState ())
	{
	  break;
	}
    }

  //restore IVT
  restoreIVT ();


  // If it's a breakpoint, then we need to back up one instruction, so
  // on restart we execute the actual instruction.
  uint32_t prevPc = readPc () - ATDSP_BKPT_INSTLEN;

  //always stop on hidden breakpoint or stopped on bkpt @ prev_pc
  assert ((NULL != mpHash->lookup (BP_MEMORY, prevPc))
	  || isHitInBreakPointInstruction (bkpt_jump_addr));
  if (si->debugStopResumeDetail ())
    cerr << dec <<
      "set prevPc after stop 0x" << hex << prevPc << dec << endl << flush;
  writePc (prevPc);

  //remove "hidden" bk
  uint16_t instr_saved;
  assert (mpHash->remove (BP_MEMORY, bkpt_addr, &instr_saved));	//should be in cache
  fTargetControl->writeMem16 (bkpt_addr, instr_saved);
  if (bkpt_jump_addr != bkpt_addr)
    {
      assert (mpHash->remove (BP_MEMORY, bkpt_jump_addr, &instr_saved));	//should be in cache
      fTargetControl->writeMem16 (bkpt_jump_addr, instr_saved);
    }

  if (si->debugTrapAndRspCon ())
    cerr << dec <<
      "After wait STEP GdbServer::Step 0x" << hex << prevPc << dec << endl
      << flush;

  // report to gdb the target has been stopped
  rspReportException (prevPc, 0 /*all threads */ , TARGET_SIGNAL_TRAP);
}				// rspStep()


//-----------------------------------------------------------------------------
//! Handle a RSP 'v' packet

//! These are commands associated with executing the code on the target
//-----------------------------------------------------------------------------
void
GdbServer::rspVpkt ()
{
  if (0 == strncmp ("vAttach;", pkt->data, strlen ("vAttach;")))
    {
      // Attaching is a null action, since we have no other process. We just
      // return a stop packet (as a TRAP exception) to indicate we are stopped.
      pkt->packStr ("S05");
      rsp->putPkt (pkt);
      return;
    }
  else if (0 == strcmp ("vCont?", pkt->data))
    {
      // For now we don't support this.
      pkt->packStr ("");
      rsp->putPkt (pkt);
      return;
    }
  else if (0 == strncmp ("vCont", pkt->data, strlen ("vCont")))
    {
      // This shouldn't happen, because we've reported non-support via vCont?
      // above
      cerr << "Warning: RSP vCont not supported: ignored" << endl << flush;
      return;
    }
  else if (0 == strncmp ("vFile:", pkt->data, strlen ("vFile:")))
    {
      // For now we don't support this.
      cerr << "Warning: RSP vFile not supported: ignored" << endl << flush;
      pkt->packStr ("");
      rsp->putPkt (pkt);
      return;
    }
  else if (0 == strncmp ("vFlashErase:", pkt->data, strlen ("vFlashErase:")))
    {
      // For now we don't support this.
      cerr << "Warning: RSP vFlashErase not supported: ignored" << endl <<
	flush;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }
  else if (0 == strncmp ("vFlashWrite:", pkt->data, strlen ("vFlashWrite:")))
    {
      // For now we don't support this.
      cerr << "Warning: RSP vFlashWrite not supported: ignored" << endl <<
	flush;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }
  else if (0 == strcmp ("vFlashDone", pkt->data))
    {
      // For now we don't support this.
      cerr << "Warning: RSP vFlashDone not supported: ignored" << endl <<
	flush;;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }
  else if (0 == strncmp ("vRun;", pkt->data, strlen ("vRun;")))
    {
      // We shouldn't be given any args, but check for this
      if (pkt->getLen () > (int) strlen ("vRun;"))
	{
	  cerr << "Warning: Unexpected arguments to RSP vRun "
	    "command: ignored" << endl << flush;
	}

      // Restart the current program. However unlike a "R" packet, "vRun"
      // should behave as though it has just stopped. We use signal 5 (TRAP).
      rspRestart ();
      pkt->packStr ("S05");
      rsp->putPkt (pkt);
    }
  else
    {
      cerr << "Warning: Unknown RSP 'v' packet type " << pkt->data
	<< ": ignored" << endl << flush;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }
}				// rspVpkt()


//-----------------------------------------------------------------------------
//! Handle a RSP write memory (binary) request

//! Syntax is:

//!   X<addr>,<length>:

//! Followed by the specified number of bytes as raw binary. Response should be
//! "OK" if all copied OK, E<nn> if error <nn> has occurred.

//! The length given is the number of bytes to be written. The data buffer has
//! already been unescaped, so will hold this number of bytes.

//! The data is in model-endian format, so no transformation is needed.

//! @todo This implementation writes everything as individual bytes/words. A
//!       more efficient implementation would stream the accesses, thereby
//!       saving one cycle/word.
//-----------------------------------------------------------------------------
void
GdbServer::rspWriteMemBin ()
{
  uint32_t addr;		// Where to write the memory
  int len;			// Number of bytes to write

  if (2 != sscanf (pkt->data, "X%x,%x:", &addr, &len))
    {
      cerr << "Warning: Failed to recognize RSP write memory command: %s"
	<< pkt->data << endl << flush;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  // Find the start of the data and "unescape" it. Bindat must be unsigned, or
  // all sorts of horrible sign extensions will happen when val is computed
  // below!
  uint8_t *bindat =
    (uint8_t *) (memchr (pkt->data, ':', pkt->getBufSize ())) + 1;
  int off = (char *) bindat - pkt->data;
  int newLen = Utils::rspUnescape ((char *) bindat, pkt->getLen () - off);

  // Sanity check
  if (newLen != len)
    {
      int minLen = len < newLen ? len : newLen;

      cerr << "Warning: Write of " << len << " bytes requested, but "
	<< newLen << " bytes supplied. " << minLen << " will be written" <<
	endl << flush;
      len = minLen;
    }

  //cerr << "rspWriteMemBin" << hex << addr << dec << " (" << len << ")" << endl << flush;
  if (!fTargetControl->writeBurst (addr, bindat, len))
    {
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  pkt->packStr ("OK");
  rsp->putPkt (pkt);
}				// rspWriteMemBin()


//-----------------------------------------------------------------------------
//! Handle a RSP remove breakpoint or matchpoint request

//! For now only memory breakpoints are implemented, which are implemented by
//! substituting a breakpoint at the specified address. The implementation must
//! cope with the possibility of duplicate packets.

//! @todo This doesn't work with icache/immu yet
//-----------------------------------------------------------------------------
void
GdbServer::rspRemoveMatchpoint ()
{
  MpType type;			// What sort of matchpoint
  uint32_t addr;		// Address specified
  uint16_t instr;		// Instruction value found
  int len;			// Matchpoint length (not used)

  // Break out the instruction
  if (3 != sscanf (pkt->data, "z%1d,%x,%1d", (int *) &type, &addr, &len))
    {
      cerr << "Warning: RSP matchpoint deletion request not "
	<< "recognized: ignored" << endl << flush;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  // Sanity check that the length is that of a BKPT instruction
  if (ATDSP_BKPT_INSTLEN != len)
    {
      cerr << "Warning: RSP matchpoint deletion length " << len
	<< " not valid: " << ATDSP_BKPT_INSTLEN << " assumed" << endl <<
	flush;
      len = ATDSP_BKPT_INSTLEN;
    }

  // Sort out the type of matchpoint
  switch (type)
    {
    case BP_MEMORY:
      //Memory breakpoint - replace the original instruction.
      if (mpHash->remove (type, addr, &instr))
	{
	  fTargetControl->writeMem16 (addr, instr);
	}

      pkt->packStr ("OK");
      rsp->putPkt (pkt);
      return;

    case BP_HARDWARE:
      pkt->packStr ("");	// Not supported
      rsp->putPkt (pkt);
      return;

    case WP_WRITE:
      pkt->packStr ("");	// Not supported
      rsp->putPkt (pkt);
      return;

    case WP_READ:
      pkt->packStr ("");	// Not supported
      rsp->putPkt (pkt);
      return;

    case WP_ACCESS:
      pkt->packStr ("");	// Not supported
      rsp->putPkt (pkt);
      return;

    default:
      cerr << "Warning: RSP matchpoint type " << type
	<< " not recognized: ignored" << endl << flush;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }
}				// rspRemoveMatchpoint()


//---------------------------------------------------------------------------*/
//! Handle a RSP insert breakpoint or matchpoint request

//! For now only memory breakpoints are implemented, which are implemented by
//! substituting a breakpoint at the specified address. The implementation must
//! cope with the possibility of duplicate packets.

//! @todo This doesn't work with icache/immu yet
//---------------------------------------------------------------------------*/
void
GdbServer::rspInsertMatchpoint ()
{
  MpType type;			// What sort of matchpoint
  uint32_t addr;		// Address specified
  int len;			// Matchpoint length (not used)

  // Break out the instruction
  if (3 != sscanf (pkt->data, "Z%1d,%x,%1d", (int *) &type, &addr, &len))
    {
      cerr << "Warning: RSP matchpoint insertion request not "
	<< "recognized: ignored" << endl << flush;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  // Sanity check that the length is that of a BKPT instruction
  if (ATDSP_BKPT_INSTLEN != len)
    {
      cerr << "Warning: RSP matchpoint insertion length " << len
	<< " not valid: " << ATDSP_BKPT_INSTLEN << " assumed" << endl <<
	flush;
      len = ATDSP_BKPT_INSTLEN;
    }

  // Sort out the type of matchpoint
  uint16_t bpMemVal;
  //bool bpMemValSt;
  switch (type)
    {
    case BP_MEMORY:
      // Memory breakpoint - substitute a BKPT instruction

      fTargetControl->readMem16 (addr, bpMemVal);
      //bpMemValSt= fTargetControl->readMem16(addr, bpMemVal);
      mpHash->add (type, addr, bpMemVal);

      putBreakPointInstruction (addr);

      pkt->packStr ("OK");
      rsp->putPkt (pkt);
      return;

    case BP_HARDWARE:
      pkt->packStr ("");	// Not supported
      rsp->putPkt (pkt);
      return;

    case WP_WRITE:
      pkt->packStr ("");	// Not supported
      rsp->putPkt (pkt);
      return;

    case WP_READ:
      pkt->packStr ("");	// Not supported
      rsp->putPkt (pkt);
      return;

    case WP_ACCESS:
      pkt->packStr ("");	// Not supported
      rsp->putPkt (pkt);
      return;

    default:
      cerr << "Warning: RSP matchpoint type " << type
	<< "not recognized: ignored" << endl << flush;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }
}				// rspInsertMatchpoint()


//! Sotware reset of the processor

//! This is achieved by repeatedly writing 1 (RESET exception) and finally 0
//! to the  MESH_SWRESET register
void
GdbServer::targetSwReset ()
{
  for (unsigned ncyclesReset = 0; ncyclesReset < 12; ncyclesReset++)
      fTargetControl->writeMem32 (MESH_SWRESET, 1);

  fTargetControl->writeMem32 (MESH_SWRESET, 0);

}	// targetSWreset()


//-----------------------------------------------------------------------------
//! HW specific (board) reset

//! The Platform driver is responsible for the actual implementation

//-----------------------------------------------------------------------------
void
GdbServer::targetHWReset ()
{
  fTargetControl->platformReset ();
}				// hw_reset, ESYS_RESET


//-----------------------------------------------------------------------------
//! Read the value of the Core ID (a Mesh grp)

//! A convenience routine and internal to external conversion

//! @return  The value of the Status
//-----------------------------------------------------------------------------
uint32_t
GdbServer::readCoreId ()
{
  uint32_t val;

  fTargetControl->readMem32 (MESH_COREID, val);
  //bool retSt = fTargetControl->readMem32(MESH_COREID, val);

  return val;
}				// readCoreStatus()


//-----------------------------------------------------------------------------
//! Read the value of the Core Status (a SCR)

//! A convenience routine and internal to external conversion

//! @return  The value of the Status
//-----------------------------------------------------------------------------
uint32_t
GdbServer::readCoreStatus ()
{
  uint32_t val;

  fTargetControl->readMem32 (CORE_CONFIG + ATDSP_SCR_STATUS * ATDSP_INST32LEN,
			     val);
  //bool retSt = fTargetControl->readMem32(CORE_CONFIG + ATDSP_SCR_STATUS * ATDSP_INST32LEN, val);

  return val;
}				// readCoreStatus()


//-----------------------------------------------------------------------------
//! Read the value of the Program Counter (a SCR)

//! A convenience routine and internal to external conversion

//! @return  The value of the PC
//-----------------------------------------------------------------------------
uint32_t
GdbServer::readPc ()
{
  uint32_t val;

  fTargetControl->readMem32 (CORE_CONFIG + ATDSP_SCR_PC * ATDSP_INST32LEN,
			     val);
  //bool retSt = fTargetControl->readMem32(CORE_CONFIG + ATDSP_SCR_PC * ATDSP_INST32LEN, val);

  //if (fIsMultiCoreSupported && (val < CORE_SPACE*NCORES)) {
  //      cout << "--" << endl << flush;
  //      val = MAKE_ADDR_GLOBAL(val, fTargetControl->GetCoreID());
  //}

  //cout << "RE PC 0x" << hex << val << dec << endl << flush;
  return val;
}				// readPc()


//-----------------------------------------------------------------------------
//! Read the value of the Link register (a GR)

//! A convenience routine and internal to external conversion

//! @return  The value of the link register
//-----------------------------------------------------------------------------
uint32_t
GdbServer::readLr ()
{
  uint32_t val;

  fTargetControl->readMem32 (CORE_R0 + ATDSP_LR_REGNUM * ATDSP_INST32LEN,
			     val);
  //bool retSt = fTargetControl->readMem32(CORE_R0 + ATDSP_LR_REGNUM * ATDSP_INST32LEN, val);

  //if (fIsMultiCoreSupported && (val < CORE_SPACE*NCORES)) {
  //      cout << "--" << endl << flush;
  //      val = MAKE_ADDR_GLOBAL(val, fTargetControl->GetCoreID());
  //}

  //cout << "RE LR 0x" << hex << val << dec << endl << flush;
  return val;
}				// readLr()


//-----------------------------------------------------------------------------
//! Read the value of the FP register (a GR)

//! A convenience routine and internal to external conversion

//! @return  The value of the frame pointer register
//-----------------------------------------------------------------------------
uint32_t
GdbServer::readFp ()
{
  uint32_t val;

  fTargetControl->readMem32 (CORE_R0 + ATDSP_FP_REGNUM * ATDSP_INST32LEN,
			     val);
  //bool retSt = fTargetControl->readMem32(CORE_R0 + ATDSP_FP_REGNUM * ATDSP_INST32LEN, val);

  //if (val < CORE_SPACE*NCORES) {
  //      cout << "--" << endl << flush;
  //      val = MAKE_ADDR_GLOBAL(val, fTargetControl->GetCoreID());
  //}

  //cout << "RE FP 0x" << hex << val << dec << endl << flush;
  return val;
}				// readFp()


//-----------------------------------------------------------------------------
//! Read the value of the SP register (a GR)

//! A convenience routine and internal to external conversion

//! @return  The value of the frame pointer register
//-----------------------------------------------------------------------------
uint32_t
GdbServer::readSp ()
{
  uint32_t val;

  fTargetControl->readMem32 (CORE_R0 + ATDSP_SP_REGNUM * ATDSP_INST32LEN,
			     val);
  //bool retSt = fTargetControl->readMem32(CORE_R0 + ATDSP_SP_REGNUM * ATDSP_INST32LEN, val);

  //if (val < CORE_SPACE*NCORES) {
  //      cout << "--" << endl << flush;
  //      val = MAKE_ADDR_GLOBAL(val, fTargetControl->GetCoreID());
  //}

  //cout << "RE SP 0x" << hex << val << dec << endl << flush;
  return val;
}				// readSp()


//-----------------------------------------------------------------------------
//! Write the value of the Program Counter (a SCR)

//! A convenience function and internal to external conversion

//! @param[in]  The address to write into the PC
//-----------------------------------------------------------------------------
void
GdbServer::writePc (uint32_t addr)
{
  //cout << "WR PC 0x" << hex << addr << dec << endl << flush;
  //if (fIsMultiCoreSupported && (addr >= CHIP_BASE && addr < CHIP_BASE+NCORES*CORE_SPACE)) {
  //      // make address internal
  //      addr = MAKE_ADDRESS_INTERNAL(addr);
  //      cout << "-- maked PC internal " << hex << addr << dec << endl << flush;
  //}
  fTargetControl->writeMem32 (CORE_CONFIG + ATDSP_SCR_PC * ATDSP_INST32LEN,
			      addr);

}				// writePc()


//-----------------------------------------------------------------------------
//! Write the value of the Link register (GR)

//! A convenience function and internal to external conversion

//! @param[in]  The address to write into the Link register
//-----------------------------------------------------------------------------
void
GdbServer::writeLr (uint32_t addr)
{
  //cout << "Warning writing to link register: " << hex << addr << dec << endl << flush;

  fTargetControl->writeMem32 (CORE_R0 + ATDSP_LR_REGNUM * ATDSP_INST32LEN,
			      addr);

}				// writeLr()


//-----------------------------------------------------------------------------
//! Write the value of the Frame register (GR)

//! A convenience function and internal to external conversion

//! @param[in]  The address to write into the Frame pointer register
//-----------------------------------------------------------------------------
void
GdbServer::writeFp (uint32_t addr)
{
  //cout << "Warning writing to frame pointer register: " << hex << addr << dec << endl << flush;

  fTargetControl->writeMem32 (CORE_R0 + ATDSP_FP_REGNUM * ATDSP_INST32LEN,
			      addr);

}				// writeFp()


//-----------------------------------------------------------------------------
//! Write the value of the Stack register (GR)

//! A convenience function and internal to external conversion

//! @param[in]  The address to write into the Stack pointer register
//-----------------------------------------------------------------------------
void
GdbServer::writeSp (uint32_t addr)
{
  //cout << "Warning writing to stack pointer register: " << hex << addr << dec << endl << flush;

  fTargetControl->writeMem32 (CORE_R0 + ATDSP_SP_REGNUM * ATDSP_INST32LEN,
			      addr);

}				// writeSp()


//-----------------------------------------------------------------------------
//! Read the value of an ATDSP General Purpose Register

//! A convenience function. This is just a wrapper for reading memory, since
//! the GPR's are mapped into core memory

//! @param[in]  regNum  The GPR to read
//! @return  The value of the GPR
//-----------------------------------------------------------------------------
uint32_t
GdbServer::readGpr (unsigned int regNum)
{
  uint32_t r;
  if ((int) regNum == ATDSP_LR_REGNUM)
    {
      r = readLr ();
    }
  else if ((int) regNum == ATDSP_FP_REGNUM)
    {
      r = readFp ();
    }
  else if ((int) regNum == ATDSP_SP_REGNUM)
    {
      r = readSp ();
    }
  else
    {
      fTargetControl->readMem32 (CORE_R0 + regNum * ATDSP_INST32LEN, r);
      //bool retSt = fTargetControl->readMem32(CORE_R0 + regNum * ATDSP_INST32LEN, r);
    }

  return r;
}				// readGpr()


//-----------------------------------------------------------------------------
//! Write the value of an ATDSP General Purpose Register

//! A convenience function. This is just a wrapper for writing memory, since
//! the GPR's are mapped into core memory

//! @param[in]  regNum  The GPR to write
//! @param[in]  value   The value to be written
//-----------------------------------------------------------------------------
void
GdbServer::writeGpr (unsigned int regNum, uint32_t value)
{
  if ((int) regNum == ATDSP_LR_REGNUM)
    {
      writeLr (value);
    }
  else if ((int) regNum == ATDSP_FP_REGNUM)
    {
      writeFp (value);
    }
  else if ((int) regNum == ATDSP_SP_REGNUM)
    {
      writeSp (value);
    }
  else
    {
      fTargetControl->writeMem32 (CORE_R0 + regNum * ATDSP_INST32LEN, value);
    }


}				// writeGpr()


//-----------------------------------------------------------------------------
//! Read the value of an ATDSP Special Core Register, group 0

//! A convenience function. This is just a wrapper for reading memory, since
//! the SPR's are mapped into core memory

//! @param[in]  regNum  The SCR to read

//! @return  The value of the SCR
//-----------------------------------------------------------------------------
uint32_t
GdbServer::readScrGrp0 (unsigned int regNum)
{

  assert ((int) regNum < ATDSP_NUM_SCRS_0);

  if (regNum == ATDSP_SCR_PC)
    {
      return readPc ();
    }
  else
    {
      uint32_t val;
      fTargetControl->readMem32 (CORE_CONFIG + regNum * ATDSP_INST32LEN, val);
      //bool retSt = fTargetControl->readMem32(CORE_CONFIG + regNum * ATDSP_INST32LEN, val);
      return val;
    }
}				// readScrGrp0()


//-----------------------------------------------------------------------------
//! Read the value of an ATDSP Special Core Register, DMA group

//! A convenience function. This is just a wrapper for reading memory, since
//! the SPR's are mapped into core memory

//! @param[in]  regNum  The SCR to read

//! @return  The value of the SCR
//-----------------------------------------------------------------------------
uint32_t
GdbServer::readScrDMA (unsigned int regNum)
{
  assert ((int) regNum < ATDSP_NUM_SCRS_1);

  uint32_t val;

  fTargetControl->readMem32 (DMA0_CONFIG + regNum * ATDSP_INST32LEN, val);
  //bool st = fTargetControl->readMem32(DMA0_CONFIG + regNum * ATDSP_INST32LEN, val);

  return val;
}				// readScrDMA()


//-----------------------------------------------------------------------------
//! Write the value of an ATDSP Special Core Register, group 0

//! A convenience function. This is just a wrapper for writing memory, since
//! the SPR's are mapped into core memory

//! @param[in]  regNum  The SCR to write
//! @param[in]  value   The value to be written
//-----------------------------------------------------------------------------
void
GdbServer::writeScrGrp0 (unsigned int regNum, uint32_t value)
{
  assert ((int) regNum < ATDSP_NUM_SCRS_0);

  if (regNum == ATDSP_SCR_PC)
    {
      writePc (value);
    }
  else
    {
      fTargetControl->writeMem32 (CORE_CONFIG + regNum * ATDSP_INST32LEN,
				  value);
    }
}				// writeScrGrp0()


//-----------------------------------------------------------------------------
//! Write the value of an ATDSP Special Core Register, DMA group

//! A convenience function. This is just a wrapper for writing memory, since
//! the SPR's are mapped into core memory

//! @param[in]  regNum  The SCR to write
//! @param[in]  value   The value to be written
//-----------------------------------------------------------------------------
void
GdbServer::writeScrDMA (unsigned int regNum, uint32_t value)
{
  assert ((int) regNum < ATDSP_NUM_SCRS_1);

  fTargetControl->writeMem32 (DMA0_CONFIG + regNum * ATDSP_INST32LEN, value);
}				// writeScrDMA()


//-----------------------------------------------------------------------------
//! Handle a RSP qThreadExtraInfo query

//! Syntax is:

//!   qThreadExtraInfo<threadID>:

//!
//-----------------------------------------------------------------------------
void
GdbServer::rspQThreadExtraInfo ()
{
  unsigned int threadID;

  if (1 != sscanf (pkt->data, "qThreadExtraInfo,%x", &threadID))
    {
      cerr << "Warning: Failed to recognize RSP qThreadExtraInfo command : "
	<< pkt->data << endl << flush;
      pkt->packStr ("E01");
      return;
    }

  char tread_info_str[300];
  sprintf (tread_info_str, "ATDSP --");
  for (unsigned i = 0; i < strlen (tread_info_str); i++)
    {
      sprintf ((pkt->data + 2 * i), "%02x", tread_info_str[i]);
    }

  sprintf ((pkt->data + 2 * strlen (tread_info_str)), "%02x", char (0));

  pkt->setLen (strlen (pkt->data));
  rsp->putPkt (pkt);

  return;
}


//-----------------------------------------------------------------------------
//! Handle a RSP Set thread for subsequent operations (`m', `M', `g', `G', et.al.).
//! c depends on the operation to be performed: it should be `c' for step and continue operations, `g' for other operations.
//! The thread designator thread-id has the format and interpretation described in thread-id syntax.

//! Syntax is:

//!   H<op><threadID>:

//! The response is the bytes, lowest address first, encoded as pairs of hex
//! digits.

//!       at present.
//-----------------------------------------------------------------------------
void
GdbServer::rspThreadSubOperation ()
{
  int threadID;

  int scanfRet = sscanf (pkt->data + 2, "%x", &threadID);

  if (1 != scanfRet)
    {
      cerr << "Warning: Failed to recognize RSP H command : "
	<< pkt->data << endl << flush;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }
  if (threadID == -1)
    {
      //cout << "apply for all thread " << endl << flush;

    }

  pkt->packStr ("OK");
  rsp->putPkt (pkt);

  return;
}



// These functions replace the intrinsic SystemC bitfield operators.
uint8_t
GdbServer::getfield (uint8_t x, int _lt, int _rt)
{
  return (x & ((1 << (_lt + 1)) - 1)) >> _rt;
}


uint16_t
GdbServer::getfield (uint16_t x, int _lt, int _rt)
{
  return (x & ((1 << (_lt + 1)) - 1)) >> _rt;
}


uint32_t
GdbServer::getfield (uint32_t x, int _lt, int _rt)
{
  return (x & ((1 << (_lt + 1)) - 1)) >> _rt;
}


uint64_t
GdbServer::getfield (uint64_t x, int _lt, int _rt)
{
  return (x & ((1 << (_lt + 1)) - 1)) >> _rt;
}


void
GdbServer::setfield (uint32_t & x, int _lt, int _rt, uint32_t val)
{
  uint32_t mask;

  mask = ((1 << (_lt - _rt + 1)) - 1) << _rt;

  x = (x & (~mask)) | (val << _rt);

  return;
}


//! Convenience function to turn an integer into a string

//! @param[in] val    The value to convert
//! @param[in] base   The base for conversion. Default 10, valid values 8, 10
//!                   or 16. Other values will reset the iostream flags.
//! @param[in] width  The width to pad (with zeros).
string
GdbServer::intStr (int  val,
		   int  base,
		   int  width)
{
  ostringstream  os;

  os << setbase (base) << setfill ('0') << setw (width) << val;
  return os.str ();

}	// intStr ()


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// End:
