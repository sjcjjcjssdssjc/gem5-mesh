/*
 * Copyright (c) 2008 Princeton University
 * Copyright (c) 2016 Georgia Institute of Technology
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Niket Agarwal
 *          Tushar Krishna
 */


#include "mem/ruby/network/garnet2.0/CrossbarSwitch.hh"

#include "base/stl_helpers.hh"
#include "debug/RubyNetwork.hh"
#include "mem/ruby/network/garnet2.0/OutputUnit.hh"
#include "mem/ruby/network/garnet2.0/Router.hh"

#include "mem/ruby/scratchpad/MemMessage.hh"
#include "debug/Mesh.hh"

using m5::stl_helpers::deletePointers;

CrossbarSwitch::CrossbarSwitch(Router *router)
    : Consumer(router)
{
    m_router = router;
    m_num_vcs = m_router->get_num_vcs();
    m_crossbar_activity = 0;
    m_num_flits_pushed_this_cycle = 0;
}

CrossbarSwitch::~CrossbarSwitch()
{
    deletePointers(m_switch_buffer);
}

void
CrossbarSwitch::init()
{
    m_output_unit = m_router->get_outputUnit_ref();

    m_num_inports = m_router->get_num_inports();
    m_switch_buffer.resize(m_num_inports);
    for (int i = 0; i < m_num_inports; i++) {
        m_switch_buffer[i] = new flitBuffer();
    }
}

/*
 * The wakeup function of the CrossbarSwitch loops through all input ports,
 * and sends the winning flit (from SA) out of its output port on to the
 * output link. The output link is scheduled for wakeup in the next cycle.
 */

void
CrossbarSwitch::wakeup()
{
    DPRINTF(RubyNetwork, "CrossbarSwitch at Router %d woke up "
            "at time: %lld\n",
            m_router->get_id(), m_router->curCycle());
    m_num_flits_pushed_this_cycle = 0;
    for (int inport = 0; inport < m_num_inports; inport++) {
        // TODO need to check tick instead of cycle here???
        // if (!m_switch_buffer[inport]->isReady(m_router->curCycle()))
        if (!m_switch_buffer[inport]->isReady(curTick()))
            continue;

        flit *t_flit = m_switch_buffer[inport]->peekTopFlit();
        if (t_flit->is_stage(ST_, m_router->curCycle())) {
            int outport = t_flit->get_outport();

            // flit performs LT_ in the next cycle
            t_flit->advance_stage(LT_, m_router->curCycle() + Cycles(1));
            // t_flit->set_time(m_router->curCycle() + Cycles(1));

            m_num_flits_pushed_this_cycle++;

            // auto mem_msg = std::dynamic_pointer_cast<MemMessage>(t_flit->get_msg_ptr());
            // if (mem_msg != nullptr && mem_msg->getPacket()->getAddr() >= 0x20000000)  {
            //     DPRINTF(Mesh, "Router switch %d inject %#x\n", m_router->get_id(), mem_msg->getPacket()->getAddr());
            //     t_flit->set_time(m_router->curCycle() + Cycles(0));
            //     // t_flit->set_time(m_router->curCycle() + Cycles(1));
            // }
            // else
            //     t_flit->set_time(m_router->curCycle() + Cycles(1));
            t_flit->set_time(m_router->curCycle() + Cycles(0));

            DPRINTF(RubyNetwork, "Router switch %d inject flit ptr %p\n", m_router->get_id(), t_flit);

            // This will take care of waking up the Network Link
            // in the next cycle
            m_output_unit[outport]->insert_flit(t_flit);
            m_switch_buffer[inport]->getTopFlit();
            m_crossbar_activity++;
        }
    }
    if (m_num_flits_pushed_this_cycle > 1) {
        DPRINTF(RubyNetwork, "[[WARNING]] number of pushed flits exceeds 1, is %d\n", m_num_flits_pushed_this_cycle);
    }
}

uint32_t
CrossbarSwitch::functionalWrite(Packet *pkt)
{
   uint32_t num_functional_writes = 0;

   for (uint32_t i = 0; i < m_switch_buffer.size(); ++i) {
       num_functional_writes += m_switch_buffer[i]->functionalWrite(pkt);
   }

   return num_functional_writes;
}

void
CrossbarSwitch::resetStats()
{
    m_crossbar_activity = 0;
}