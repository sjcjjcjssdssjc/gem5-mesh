#include "custom/event_driven_fsm.hh"
#include "cpu/minor/cpu.hh"
#include "debug/Mesh.hh"
#include "custom/vector_forward.hh"

EventDrivenFSM::EventDrivenFSM(VectorForward *vec, MinorCPU *cpu, SensitiveStage stage) :
  _vec(vec), _cpu(cpu), _state(IDLE), _oldState(IDLE), _didTransition(false),
  _stage(stage), _tickEvent([this] { stateTransition(); }, cpu->name()) 
{}


// as soon as a bind takes place, this FSM is in control
// of the execution flow, i.e. it does the ticks and calls functions?

// or when stalled due to mesh this state machine takes over execution


/*----------------------------------------------------------------------
 * This state machine is sensitive to the following events
 * 
 * 1) A new bind/config
 * 2) Neighbor val/rdy updates
 * 3) Inst + data req
 * 4) Inst + data resp
 *--------------------------------------------------------------------*/
void
EventDrivenFSM::neighborEvent() {
  DPRINTF(Mesh, "%s neighbor update\n", _cpu->name());
  
  sensitiveUpdate();
}

void
EventDrivenFSM::configEvent() {
  //DPRINTF(Mesh, "%s config update\n", _cpu->name());
  
  sensitiveUpdate();
}

/*----------------------------------------------------------------------
 * 
 *--------------------------------------------------------------------*/


// TODO do we even need a state machine or can just check val rdy before sending?
bool
EventDrivenFSM::isMeshActive() {
  return 
    ((getInVal() && getOutRdy()) && getConfigured())
    //&& 
    //((_state == RUNNING_BIND) || (_state == BEGIN_SEND))) 
    
    //|| (!getConfigured())
    ;
}

// allowed to be ticked multiple times per cycle
// does not modify the current state, but the potential next state
void
EventDrivenFSM::sensitiveUpdate() {
  // try to update the next state
  //_nextState = updateState();
  //setNextState(pendingNextState());
  
  tryScheduleUpdate();
}

// TODO needs to transition at the end of this cycle so ready for any
// early event on the next cycle


// ticked once per cycle, do state update
// state outputs only change once per cycle --> moore machine
bool
EventDrivenFSM::tick() {
  //bool updateOut = (_nextState != _state);
  
  // 
  // _state = _nextState;

  //_state = _nextState;
  
  //_inputs.clear();
  
  // stateOutput();

  // try to update the state
  //_nextState = updateState();

  // handle the state transition
  //stateTransition();

  // if there was a state update, inform caller
  // it will likely want to inform neighbors that something happened
  if (_didTransition) {
    DPRINTF(Mesh, "%s %d on clk edge state %s -> %s\n", _cpu->name(), _stage, stateToStr(_oldState), stateToStr(_state));
    
    _didTransition = false;
    
    // if there is a state update, going to want to inform neighbors
    return true;
  }
  else {
    return false;
  }
  
  
  //DPRINTF(Mesh, "state %s -> %s\n", stateToStr(_state), stateToStr(_nextState));
  tryScheduleUpdate();
  
}


void 
EventDrivenFSM::tryScheduleUpdate() {
  // do update if there will be a new state
  /*if (_state != pendingNextState()) {
    if (!_tickEvent.scheduled()) {
      _cpu->schedule(_tickEvent, _cpu->clockEdge());
    }
  }*/
  
  // more efficient to reorder branches b/c don't want to waste lookup work?
  if (!_tickEvent.scheduled()) {
    if (_state != pendingNextState()) {
      _cpu->schedule(_tickEvent, _cpu->clockEdge());
    }
  }
}


EventDrivenFSM::Outputs_t
EventDrivenFSM::stateOutput() {
  //bool inVal = getInVal();
  //bool outRdy = getOutRdy();
  //DPRINTF(Mesh, "%s state output\n", _cpu->name());
  switch(_state) {
    case IDLE: {
      return Outputs_t(false, false);
    }
    case BEGIN_SEND: {
      return Outputs_t(true, false);
    }
    case RUNNING_BIND: {
      return Outputs_t(true, true);
    }
    case WAIT_MESH_VALRDY: {
      return Outputs_t(false, false);
    }
    case WAIT_MESH_RDY: {
      return Outputs_t(false, false);
    }
    case WAIT_MESH_VAL: {
      return Outputs_t(true, false);
    }
    default: {
      return Outputs_t(false, false);
    }
  }
  
  // inform neighbors about any state change here?
  //_vec->informNeighbors();
}

EventDrivenFSM::State
EventDrivenFSM::meshState(State onValRdy, bool inVal, bool outRdy) {
  //if (_stage == EXECUTE && _inputs.dataReq == true) return WAIT_DATA_RESP;
  //else if (_stage == FETCH && _inputs.instReq == true) return WAIT_INST_RESP;
  if (inVal && outRdy) return onValRdy;
  else if (inVal) return WAIT_MESH_RDY;
  else if (outRdy) return WAIT_MESH_VAL;
  else return WAIT_MESH_VALRDY;
}

EventDrivenFSM::State
EventDrivenFSM::pendingNextState() {
  
  bool inVal = getInVal();
  bool outRdy = getOutRdy();
  bool configured = getConfigured();
  
  // fully connected state machine?
  /*if (inVal && outRdy) return SENDING;
  else if (inVal) return WAIT_RDY;
  else if (outRdy) return WAIT_VAL;
  else return WAIT_ALL;*/
  
  if (!configured) return IDLE;
  
  switch(_state) {
    case IDLE: {
      if (configured) return WAIT_MESH_VALRDY;
      else return IDLE;
    }
    case BEGIN_SEND: {
      return meshState(RUNNING_BIND, inVal, outRdy);
    }
    case RUNNING_BIND: {
      return meshState(RUNNING_BIND, inVal, outRdy);
    }
    /*case WAIT_DATA_RESP: {
      if (_inputs.dataResp) {
        return meshState(BEGIN_SEND, inVal, outRdy);
      }
      else {
        return WAIT_DATA_RESP;
      }
    }
    // this one needs work, on resp tries the send, so shouldnt
    // do again here
    case WAIT_INST_RESP: {
      if (_inputs.instResp && outRdy) {
        return meshState(BEGIN_SEND, inVal, outRdy);
      }
      else if (_inputs.instResp) {
        return meshState(WAIT_MESH_RDY, inVal, outRdy);
      }
      else {
        return WAIT_INST_RESP;
      }
    }*/
    default: {
      return meshState(BEGIN_SEND, inVal, outRdy);
    }
  }
  
}

/*State
EventDrivenFSM::getNextState() {
  return _nextState[_nextStateIdx];
}

void
EventDrivenFSM::setNextState(State state) {
  // if this update is on a later cycle dont update the current buffer
  if (_nextState[_nextStateIdx].cycleUpdated < curTick()) {
    
  }
  else {
    _nextState[_nextStateIdx].nextState = state;
  }
}*/

void
EventDrivenFSM::stateTransition() {
  _didTransition = true;
  _oldState = _state;
  _state = pendingNextState();
}

bool
EventDrivenFSM::getInVal() {
  return _vec->getInVal();
}

bool
EventDrivenFSM::getOutRdy() {
  return _vec->getOutRdy();
}

bool
EventDrivenFSM::getConfigured() {
  return _vec->getNumPortsActive() > 0;
}

std::string
EventDrivenFSM::stateToStr(State state) {
  switch(state) {
    case IDLE: {
      return "IDLE";
    }
    case WAIT_MESH_VALRDY: {
      return "WAIT_MESH_VALRDY";
    }
    case WAIT_MESH_RDY: {
      return "WAIT_MESH_RDY";
    }
    case WAIT_MESH_VAL: {
      return "WAIT_MESH_VAL";
    }
    case WAIT_DATA_RESP: {
      return "WAIT_DATA_RESP";
    }
    case WAIT_INST_RESP: {
      return "WAIT_INST_RESP";
    }
    case RUNNING_BIND: {
      return "RUNNING_BIND";
    }
    case BEGIN_SEND: {
      return "BEGIN_SEND";
    }
    default: {
      return "NONE";
    }
  }
}






