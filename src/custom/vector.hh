#ifndef __CUSTOM_VECTOR_UNIT_HH__
#define __CUSTOM_VECTOR_UNIT_HH__

/*
 * Handles instruction communication over the mesh network:
 * sends, recvs, and syncs with neighbors.
 * 
 */ 

#include "cpu/io/stage.hh"
#include "custom/mesh_helper.hh"
#include "custom/mesh_ports.hh"

class Vector : public Stage {
  // inheritance
  public:
    Vector(IOCPU *_cpu_p, IOCPUParams *params);
    ~Vector() = default;

    /** Init (this is called after all CPU structures are created) */
    void init() override;

    /** Return name of this stage object */
    std::string name() const override;

    /** Register stats */
    void regStats() override;

    /** Main tick function */
    void tick() override;
    
    /** Line trace */
    void linetrace(std::stringstream& ss) override;

  // inheritance
  protected:
    /** Place the given instruction into the buffer to the next stage */
    void sendInstToNextStage(IODynInstPtr inst) override;
    
    /** Squash all instructions younger than the given squash instruction */
    void doSquash(SquashComm::BaseSquash &squashInfo, StageIdx initiator) override;

  public:
    
    // setup which mesh ports are active
    void setupConfig(int csrId, RegVal csrVal);
    
    // cpu needs to bind ports to names, so expose them via a reference
    Port &getMeshPort(int idx, bool isOut);
    int getNumMeshPorts();
  
    // check if the output nodes are ready
    bool getOutRdy();
    
    // check if the input nodes are valid
    bool getInVal();
    
    // the fsm is sensitive to status updates in neighboring nodes
    // update it when neccesary
    void neighborUpdate();
    
    // get num ports being used
    int getNumPortsActive();
    
    // inform neighboring nodes we had a status change and should attempt
    // a new state for their local fsms
    void informNeighbors();
    
    // set appropriate mesh node as ready
    void setRdy(bool rdy);
    
    // set appropriate mesh node ports as valid
    void setVal(bool val);
    
    // check if we need to stall due to the internal pipeline
    bool isInternallyStalled();
    
    // get if this is configured beyond default behavior
    bool getConfigured();
    
    // if cpu allows idling, need to call this to make sure active when stuff to do
    // currently does nothing in IOCPU
    void signalActivity() {}
    
  protected:
    // information to use to create local IODynInst
    // currenlty cheating and using all possible info
    struct MasterData {
        IODynInstPtr inst;
    };
    
    // create a dynamic instruction
    IODynInstPtr createInstruction(const MasterData &instInfo);
    
    // make a dynamic instruction then push it
    void pushInstToNextStage(const MasterData &instInfo);
    
    // master calls this to broadcast to neighbors
    void forwardInstruction(const MasterData &instInfo);
    
    // pull an instruction from the mesh network (figure out the right dir)
    void pullInstruction(MasterData &instInfo);
    
    // pass the instruction through this stage because not in vec mode
    // wont this add buffer slots in !sequentialMode which is undesirable?
    // need to reflect in credits
    void passInstructions();
    
    // extract (functional decode) the machinst to ExtMachInst
    StaticInstPtr extractInstruction(const TheISA::MachInst inst);
    
    // create a packet to send over the mesh network
    PacketPtr createMeshPacket(RegVal payload);
    // cheat version for experiments
    PacketPtr createMeshPacket(IODynInstPtr inst);
    
    // reset all mesh node port settings
    void resetActive();
    
    // TEMP get an instruction from the master
    IODynInstPtr getMeshPortInst(Mesh_Dir dir);
    
    // get received data from the specified mesh port
    uint64_t getMeshPortData(Mesh_Dir dir);
    
    // get the received packet
    PacketPtr getMeshPortPkt(Mesh_Dir dir);
    
    // get a machinst from the local fetch2
    IODynInstPtr getFetchInput();
    
    // check if this stage can proceed
    bool shouldStall();
    
    // encode + decode of information over mesh net
    // requires 32 + 1 = 33bits currently
    //Minor::ForwardVectorData decodeMeshData(uint64_t data);
    //uint64_t encodeMeshData(const Minor::ForwardVectorData &instInfo);
    
    // when setup a configuration, may need to stall or unstall the frontend
    void stallFetchInput(ThreadID tid);
    void unstallFetchInput(ThreadID tid);
     
  protected:
  
    // define the ports we're going to use for to access the mesh net
    std::vector<ToMeshPort> _toMeshPort;
    std::vector<FromMeshPort> _fromMeshPort;
    
    // number of ports currently actively communicating
    int _numInPortsActive;
    int _numOutPortsActive;

    // the stage this vector unit is representing
    SensitiveStage _stage;
  
    // cache the most recently set csr value
    RegVal _curCsrVal;
    
    // need to steal credits to force stall the previous stage
    // effectively the mesh network has these credits, 
    // TODO? maybe make that more explicit in code
    int _stolenCredits;
    
    // TEMP to make all relevant info available
    struct SenderState : public Packet::SenderState
    {
      // important this is shared_ptr so wont be freed while packet 'in-flight'
      std::shared_ptr<IODynInst> master_inst;
      SenderState(IODynInstPtr _master_inst)
        : master_inst(_master_inst)
      { }
    };
  
};

#endif
