#include "cpu/io/stage.hh"
#include "cpu/io/cpu.hh"

Stage::Stage(IOCPU* _cpu_p, size_t inputBufSize, size_t outputBufSize,
    StageIdx stageIdx, bool isSequential)
    : m_cpu_p(_cpu_p),
      m_is_active(false),
      m_input_queue_size(inputBufSize),
      m_max_num_credits(outputBufSize),
      m_num_credits(m_max_num_credits),
      m_stage_idx(stageIdx),
      m_is_sequential(isSequential)
{ }

void
Stage::setCommBuffers(TimeBuffer<InstComm>& inst_buffer,
                       TimeBuffer<CreditComm>& credit_buffer,
                       TimeBuffer<SquashComm>& squash_buffer,
                       TimeBuffer<InfoComm>& info_buffer)
{
  m_outgoing_inst_wire = inst_buffer.getWire(0);
  m_incoming_inst_wire = inst_buffer.getWire(-1);

  m_outgoing_credit_wire = credit_buffer.getWire(0);
  m_incoming_credit_wire = credit_buffer.getWire(-1);

  m_outgoing_squash_wire = squash_buffer.getWire(0);
  m_incoming_squash_wire = squash_buffer.getWire(-1);
  
  m_outgoing_info_wire = info_buffer.getWire(0);
  m_incoming_info_wire   = info_buffer.getWire(-1);
}

bool
Stage::hasNextStage() {
  return ((int)m_stage_idx + 1 < (int)StageIdx::NumStages);
}

bool
Stage::hasPrevStage() {
  return ((int)m_stage_idx - 1 >= 0);
}
    
std::list<std::shared_ptr<IODynInst>>&
Stage::inputInst() {
  // if the stage takes one cycle (don't know why it would) then get from
  // incoming buffer as other stages do
  if (m_is_sequential) {
    return m_incoming_inst_wire->from_prev_stage(m_stage_idx);
  }
  // if vector stage takes zero cycles, then we should pull from outgoing
  // and not wait for it to switch to incoming on the next cycle
  else {
    return m_outgoing_inst_wire->from_prev_stage(m_stage_idx);
  }
}

std::list<std::shared_ptr<IODynInst>>&
Stage::outputInst() {
  return m_outgoing_inst_wire->to_next_stage(m_stage_idx);
}
    
size_t&
Stage::inputCredit() {
  return m_incoming_credit_wire->from_next_stage(m_stage_idx);
}

// TODO this might be problematic i.e. fetch happens before vector, but wont know about credits until vec?
// but combo can just give a credit every cycle to keep going
size_t&
Stage::outputCredit() {
  return m_outgoing_credit_wire->to_prev_stage(m_stage_idx);
}

void
Stage::queueInsts() {
  if (!hasPrevStage()) return;
  
  for (auto inst : inputInst())
    m_insts.push(inst);
  
  assert(m_insts.size() <= m_input_queue_size);
}

void
Stage::sendInstToNextStage(IODynInstPtr inst) {
  if (!hasNextStage()) return;
  
  // sanity check: make sure we have enough credit before we sent the inst
  assert(m_num_credits > 0);
  // Place inst into the buffer
  outputInst().push_back(inst);
  // consume one credit
  m_num_credits--;
}

void
Stage::consumeInst() {
  if (!hasPrevStage()) return;
  
  // Remove the inst from the queue and increment the credit to the previous
  // stage.
  m_insts.pop();
  outputCredit()++;
}

// TODO should put in tick
void
Stage::readCredits() {
  if (!hasNextStage()) return;
  
  // read and update my number of credits
  m_num_credits += inputCredit();
  assert(m_num_credits <= m_max_num_credits);
}

void
Stage::tick() {
  // sanity check
  assert(m_is_active);

  // put all coming instructions to process in m_insts queue
  queueInsts();

  // read credits from the next stage
  readCredits();
}

void
Stage::wakeup()
{
  assert(!m_is_active);
  m_is_active = true;
}

void
Stage::suspend()
{
  assert(m_is_active);
  m_is_active = false;
}

bool
Stage::nextStageRdy() {
  return (m_num_credits > 0);
}
