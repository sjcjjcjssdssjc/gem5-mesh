#include "custom/mesh_ports.hh"
#include "cpu/io/cpu.hh"
#include "custom/vector.hh"
#include "debug/Mesh.hh"

#define PROTOCOL(active, val, rdy) \
  (active && val && rdy) || (!active)

#define MESH_QUEUE_SLOTS 16

/*----------------------------------------------------------------------
 * Define mesh master port behavior
 *--------------------------------------------------------------------*/

ToMeshPort::ToMeshPort(IOCPU *_cpu, int idx)
    : MasterPort(
          _cpu->name() + ".mesh_out_port" + csprintf("[%d]", idx), _cpu),
      cpu(_cpu), idx(idx), active(NONE), vec(nullptr)
{
}

// if you want to send a packet, used <MasterPort_Inst>.sendTimingReq(pkt);

// override what should happen when the packet we sent on this port has returned
bool ToMeshPort::recvTimingResp(PacketPtr pkt)
{
  return true;
}

// override what should happen when the packet we sent on this port failed to send
void ToMeshPort::recvReqRetry()
{
}

/*void
ToMeshPort::setVal(bool val) {
  this->val = val;
}*/

// if active we want val/rdy, otherwise we don't care
/*bool
ToMeshPort::checkHandshake(){
  bool rdy = getPairRdy();
  return (PROTOCOL(active, val, rdy));
}*/

bool ToMeshPort::getPairRdy()
{
  BaseSlavePort *slavePort = &(getSlavePort());
  if (FromMeshPort *slaveMeshPort = dynamic_cast<FromMeshPort *>(slavePort))
  {
    return slaveMeshPort->getRdy();
  }
  // if not connected to a mesh port, then probably an edge harness
  // in this case, its always rdy!
  else
  {
    return true;
  }
}

void ToMeshPort::tryUnblockNeighbor()
{
  BaseSlavePort *slavePort = &(getSlavePort());
  if (FromMeshPort *slaveMeshPort = dynamic_cast<FromMeshPort *>(slavePort))
  {
    slaveMeshPort->tryUnblockCPU();
  }
}

/*void
ToMeshPort::setValIfActive(bool val, SensitiveStage stage) {
  if (active == stage) { 
    setVal(val);
  }
  // if not active then allow to go to false, this occurs when deconfigure
  else if (active == NONE && !val) {
    setVal(false);
  }
}*/

/*----------------------------------------------------------------------
 * Define mesh slave port behavior
 *--------------------------------------------------------------------*/

// NEVER give THIS pointer
FromMeshPort::FromMeshPort(IOCPU *_cpu, int idx)
    : SlavePort(
          _cpu->name() + ".mesh_in_port" + csprintf("[%d]", idx), _cpu),
      cpu(_cpu), idx(idx), recvPkt_d(nullptr), recvEvent([this] { process(); }, name()),
      wakeupCPUEvent([this] { tryUnblockCPU(); }, name()),
      active(NONE), _meshQueue(name(), "pkt", MESH_QUEUE_SLOTS),
      vec(nullptr)
{
  //DPRINTF(Mesh, "init %d %d %ld %ld\n", rdy, active, (uint64_t)recvPkt, (uint64_t)this);
  //DPRINTF(Mesh, "init idx %d\n", idx);
}

// This isn't working when multiple FromMesh ports schedule this event!!!!
// how to handle a request after waiting for some delay
void FromMeshPort::process()
{

  /*assert(cpu != nullptr);
  // crashes the session :(
  if (idx < 4){
    DPRINTF(Mesh, "process idx %d\n", idx);
  }
  else {
    assert(0);
  }*/
  // save the received packet
  setPacket(recvPkt_d);
}

bool FromMeshPort::recvTimingReq(PacketPtr pkt)
{
  //DPRINTF(Mesh, "recvresp packet %ld %ld\n", (uint64_t)recvPkt, (uint64_t)this);
  //DPRINTF(Mesh, "Received mesh request %#x for idx %d\n", pkt->getAddr(), idx);
  // we should only ever see one response per cycle since we only
  // issue a new request once this response is sunk

  //assert(!tickEvent.scheduled());
  assert(!recvEvent.scheduled());
  // delay processing of returned data until next CPU clock edge
  //tickEvent.schedule(pkt, cpu->clockEdge());
  //recvPkt_d = pkt;
  //recvEvent.schedule(cpu->clockEdge());
  recvPkt_d = pkt;
  cpu->schedule(recvEvent, cpu->clockEdge(Cycles(1)));
  // temp
  //setPacket(pkt);

  // TODO ERROR wrong need to do this on the next cycle!
  // try to unblock when recv a packet
  //cpu->tryUnblock(false);

  // inform there is local activity and should wakeup entire pipeline to try
  // stuff on the next cycle
  //cpu->activityRecorder->activity();
  //cpu->activityRecorder->activateStage(Minor::Pipeline::VectorStageId);

  //cpu->wakeupOnEvent(Minor::Pipeline::VectorStageId);
  if (vec)
    vec->signalActivity();

  return true;
}

void FromMeshPort::recvRespRetry()
{
  // ?
  assert(0);
}

// the this pointer changes after constructing due to being transfered
// to vector mem management
void FromMeshPort::setupEvents()
{
  recvEvent = EventFunctionWrapper([this] { process(); }, name());
  wakeupCPUEvent = EventFunctionWrapper([this] { tryUnblockCPU(); }, name());
}

void FromMeshPort::recvFunctional(PacketPtr pkt)
{
  // ? just call MITickEvent::proccess?
  assert(0);
}

// might want to do bsg_manycore eva treament here for addressing
AddrRangeList
FromMeshPort::getAddrRanges() const
{
  //return cpu->getAddrRanges();
  return std::list<AddrRange>();
}

// get recv pkts
PacketPtr
FromMeshPort::getPacket()
{
  /*if (recvPkt == nullptr) {
    DPRINTF(Mesh, "[[WARNING]] Did not recv packet\n");
    return nullptr;
  }
  
  if (cyclePktRecv >= cpu->clockEdge()) {
    DPRINTF(Mesh, "[[WARNING]] Packet not ready for use\n");
    return nullptr;
  }
  
  PacketPtr curPacket = recvPkt;
  
  // destructive read on packet
  recvPkt = nullptr;
  
  return curPacket;*/

  PacketPtr pkt = nullptr;
  if (pktExists())
  {
    pkt = _meshQueue.front().getPacket();
    _meshQueue.pop();
  }
  else
  {
    DPRINTF(Mesh, "[[WARNING]] No packet available\n");
  }

  return pkt;
}

// extract data from packet
uint64_t
FromMeshPort::getPacketData(PacketPtr pkt)
{
  return pkt->getUintX(LittleEndianByteOrder);
}

/*uint64_t
FromMeshPort::getPacketData() {
  PacketPtr curPacket = getPacket();
  if (curPacket == nullptr) return 0;
  
  // get data from packet
  uint64_t data = curPacket->getUintX(LittleEndianByteOrder);
  
  // destructive read on packet
  recvPkt = nullptr;
  
  // this might update val/rdy interface
  cpu->informNeighbors();
  
  return data;
}*/

void FromMeshPort::setPacket(PacketPtr pkt)
{
  /*if (recvPkt != nullptr) {
    DPRINTF(Mesh, "[[WARNING]] Overwrite packet %#x in port %d\n", recvPkt->getAddr(), idx);
  }
  
  recvPkt = pkt;*/

  // push packet onto a 2 element queue to be stall-proof
  if (_meshQueue.canReserve())
  {
    auto pktData = MeshPacketData(pkt);
    _meshQueue.push(pktData);
    //DPRINTF(Mesh, "set packet, size now %d\n", _meshQueue.occupiedSpace());
  }
  else
  {
    DPRINTF(Mesh, "[[WARNING]] Dropping packet %#x in port %d\n",
            pkt->getAddr(), idx);
  }
}

bool FromMeshPort::pktExists()
{
  //return ((recvPkt != nullptr) && (cyclePktRecv < cpu->clockEdge()));
  //return recvPkt != nullptr;
  return !_meshQueue.empty();
}

/*void
FromMeshPort::setRdy(bool rdy) {
  this->rdy = rdy;
}*/

// if active we want val/rdy, otherwise we don't care
/*bool
FromMeshPort::checkHandshake(){
  bool val = getPairVal();
  return (PROTOCOL(active, val, rdy));
}*/

bool FromMeshPort::getPairVal()
{
  return pktExists();
  /*BaseMasterPort *masterPort = &(getMasterPort());
  if (ToMeshPort *masterMeshPort = dynamic_cast<ToMeshPort*>(masterPort)) {
    return masterMeshPort->getVal();
  }
  else {
    return false;
  }*/
}

void FromMeshPort::tryUnblockCPU()
{
  if (vec)
    vec->neighborUpdate();
}

bool FromMeshPort::getRdy()
{
  if (!vec)
    return false;

  //if (rdy) {
  if (_meshQueue.canReserve() && vec->getConfigured())
  {
    // if we have a packet but don't have a packet in other ports or
    // we have packets but can't send them anywhere b/c output not rdy
    // then we can't accept anymore packets on this port b/c we can't tick

    // should look at next_val?
    // if (next_pairval && !next_pairval, then not rdy?)

    if (getPairVal() &&
        vec->getInVal() && vec->getOutRdy())
      return true;
    else if (getPairVal() &&
             (!vec->getInVal() || !vec->getOutRdy()))
      return false;
    else
      return true;
  }
  else
  {
    return false;
  }
}

/*void
FromMeshPort::setRdyIfActive(bool rdy, SensitiveStage stage) {
  if (active == stage) {
    setRdy(rdy);
  }
  // if not active then allow to go to false, this occurs when deconfigure
  else if (active == NONE && !rdy) {
    setRdy(false);
  }
}*/
