# Copyright (c) 2012-2013 ARM Limited
# All rights reserved.
#
# The license below extends only to copyright in the software and shall
# not be construed as granting a license to any other intellectual
# property including but not limited to intellectual property relating
# to a hardware implementation of the functionality of the software
# licensed hereunder.  You may use the software subject to the license
# terms below provided that you ensure that this notice is replicated
# unmodified and in its entirety in all distributions of the software,
# modified or unmodified, in source code or in binary form.
#
# Copyright (c) 2006-2008 The Regents of The University of Michigan
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Authors: Steve Reinhardt

# Simple test script
#
# "m5 test.py"

from __future__ import print_function
from __future__ import absolute_import

import optparse
import sys
import os

import m5
from m5.defines import buildEnv
from m5.objects import *
from m5.util import addToPath, fatal, warn

addToPath('../')

from ruby import Ruby

from common import Options
from common import Simulation
from common import CacheConfig
from common import CpuConfig
from common import BPConfig
from common import MemConfig
from common.Caches import *
from common.cpu2000 import *

def get_processes(options):
    """Interprets provided options and returns a list of processes"""

    multiprocesses = []
    inputs = []
    outputs = []
    errouts = []
    pargs = []

    workloads = options.cmd.split(';')
    if options.input != "":
        inputs = options.input.split(';')
    if options.output != "":
        outputs = options.output.split(';')
    if options.errout != "":
        errouts = options.errout.split(';')
    if options.options != "":
        pargs = options.options.split(';')

    idx = 0
    for wrkld in workloads:
        process = Process(pid = 100 + idx)
        process.executable = wrkld
        process.cwd = os.getcwd()

        if options.env:
            with open(options.env, 'r') as f:
                process.env = [line.rstrip() for line in f]

        if len(pargs) > idx:
            process.cmd = [wrkld] + pargs[idx].split()
        else:
            process.cmd = [wrkld]

        if len(inputs) > idx:
            process.input = inputs[idx]
        if len(outputs) > idx:
            process.output = outputs[idx]
        if len(errouts) > idx:
            process.errout = errouts[idx]

        multiprocesses.append(process)
        idx += 1

    if options.smt:
        assert(options.cpu_type == "DerivO3CPU")
        return multiprocesses, idx
    else:
        return multiprocesses, 1


parser = optparse.OptionParser()
Options.addCommonOptions(parser)
Options.addSEOptions(parser)

if '--ruby' in sys.argv:
    Ruby.define_options(parser)

(options, args) = parser.parse_args()

if args:
    print("Error: script doesn't take any positional arguments")
    sys.exit(1)

multiprocesses = []
numThreads = 1

if options.bench:
    apps = options.bench.split("-")
    if len(apps) != options.num_cpus:
        print("number of benchmarks not equal to set num_cpus!")
        sys.exit(1)

    for app in apps:
        try:
            if buildEnv['TARGET_ISA'] == 'alpha':
                exec("workload = %s('alpha', 'tru64', '%s')" % (
                        app, options.spec_input))
            elif buildEnv['TARGET_ISA'] == 'arm':
                exec("workload = %s('arm_%s', 'linux', '%s')" % (
                        app, options.arm_iset, options.spec_input))
            else:
                exec("workload = %s(buildEnv['TARGET_ISA', 'linux', '%s')" % (
                        app, options.spec_input))
            multiprocesses.append(workload.makeProcess())
        except:
            print("Unable to find workload for %s: %s" %
                  (buildEnv['TARGET_ISA'], app),
                  file=sys.stderr)
            sys.exit(1)
elif options.cmd:
    multiprocesses, numThreads = get_processes(options)
else:
    print("No workload specified. Exiting!\n", file=sys.stderr)
    sys.exit(1)


(CPUClass, test_mem_mode, FutureClass) = Simulation.setCPUClass(options)
CPUClass.numThreads = numThreads

# Check -- do not allow SMT with multiple CPUs
if options.smt and options.num_cpus > 1:
    fatal("You cannot use SMT with multiple CPUs!")

np = options.num_cpus

# pbb
# if using Minor CPU then adjust some settings
# 1) defaults to 2 way???, would prefer single way CPU

if options.cpu_type == 'MinorCPU':
    system = System(
        cpu = [
            CPUClass(
                cpu_id=i, 
                
                # these were originally 2 (default in MinorCPU.py)
                
                fetch2InputBufferSize = 1,
                decodeInputWidth = 1,
                executeInputWidth = 1,
                executeIssueLimit = 1,
                executeCommitLimit = 1,
                #executeMaxAccessesInMemory = 1,
                executeLSQMaxStoreBufferStoresPerCycle = 1,
                
                
            ) for i in range(np)],
                mem_mode = test_mem_mode,
                mem_ranges = [AddrRange(options.mem_size)],
                cache_line_size = options.cacheline_size)
else:
    system = System(cpu = [CPUClass(cpu_id=i) for i in range(np)],
                mem_mode = test_mem_mode,
                mem_ranges = [AddrRange(options.mem_size)],
                cache_line_size = options.cacheline_size)

if numThreads > 1:
    system.multi_thread = True

# Create a top-level voltage domain
system.voltage_domain = VoltageDomain(voltage = options.sys_voltage)

# Create a source clock for the system and set the clock period
system.clk_domain = SrcClockDomain(clock =  options.sys_clock,
                                   voltage_domain = system.voltage_domain)

# Create a CPU voltage domain
system.cpu_voltage_domain = VoltageDomain()

# Create a separate clock domain for the CPUs
system.cpu_clk_domain = SrcClockDomain(clock = options.cpu_clock,
                                       voltage_domain =
                                       system.cpu_voltage_domain)

# If elastic tracing is enabled, then configure the cpu and attach the elastic
# trace probe
if options.elastic_trace_en:
    CpuConfig.config_etrace(CPUClass, system.cpu, options)

# All cpus belong to a common cpu_clk_domain, therefore running at a common
# frequency.
for cpu in system.cpu:
    cpu.clk_domain = system.cpu_clk_domain

if CpuConfig.is_kvm_cpu(CPUClass) or CpuConfig.is_kvm_cpu(FutureClass):
    if buildEnv['TARGET_ISA'] == 'x86':
        system.kvm_vm = KvmVM()
        for process in multiprocesses:
            process.useArchPT = True
            process.kvmInSE = True
    else:
        fatal("KvmCPU can only be used in SE mode with x86")

# Sanity check
if options.simpoint_profile:
    if not CpuConfig.is_noncaching_cpu(CPUClass):
        fatal("SimPoint/BPProbe should be done with an atomic cpu")
    if np > 1:
        fatal("SimPoint generation not supported with more than one CPUs")

for i in range(np):
    if options.smt:
        system.cpu[i].workload = multiprocesses
    elif len(multiprocesses) == 1:
        system.cpu[i].workload = multiprocesses[0]
    else:
        system.cpu[i].workload = multiprocesses[i]

    if options.simpoint_profile:
        system.cpu[i].addSimPointProbe(options.simpoint_interval)

    if options.checker:
        system.cpu[i].addCheckerCpu()

    if options.bp_type:
        bpClass = BPConfig.get(options.bp_type)
        system.cpu[i].branchPred = bpClass()

    system.cpu[i].createThreads()

if options.ruby:
    Ruby.create_system(options, False, system)
    assert(options.num_cpus == len(system.ruby._cpu_ports))

    system.ruby.clk_domain = SrcClockDomain(clock = options.ruby_clock,
                                        voltage_domain = system.voltage_domain)
    for i in range(np):
        ruby_port = system.ruby._cpu_ports[i]

        # Create the interrupt controller and connect its ports to Ruby
        # Note that the interrupt controller is always present but only
        # in x86 does it have message ports that need to be connected
        system.cpu[i].createInterruptController()

        # Connect the cpu's cache ports to Ruby
        system.cpu[i].icache_port = ruby_port.slave
        system.cpu[i].dcache_port = ruby_port.slave
        if buildEnv['TARGET_ISA'] == 'x86':
            system.cpu[i].interrupts[0].pio = ruby_port.master
            system.cpu[i].interrupts[0].int_master = ruby_port.slave
            system.cpu[i].interrupts[0].int_slave = ruby_port.master
            system.cpu[i].itb.walker.port = ruby_port.slave
            system.cpu[i].dtb.walker.port = ruby_port.slave
else:
    MemClass = Simulation.setMemClass(options)
    system.membus = SystemXBar()
    system.system_port = system.membus.slave
    CacheConfig.config_cache(options, system)
    MemConfig.config_mem(options, system)


# pbb

# for each cpu let's add the mesh checker!

# gem5 only allows new attributes if they are SimObjects!
# otherwise must be declared beforehand, see params in src/sim/System.py
# http://gem5.org/Configuration_/_Simulation_Scripts

'''
# create the harnesses up front because we have to (appending to empty list not allowed)
system.harness = [Harness() for i in range(np)]

# connect the harnesses, one to each cpu
for i in range(np):
  
  # a vector port automatically adds a new port each time its assigned
  # assign the full duplex connections
  system.cpu[i].to_mesh_port = system.harness[i].from_cpu
  
  system.cpu[i].from_mesh_port = system.harness[i].to_cpu
'''

# get cpus that will be involved in the mesh (all but cpu 0)

# make into a grid
num_mcpus = np - 1
num_mcpus_x = (int)(num_mcpus**(0.5))
num_mcpus_y = num_mcpus / num_mcpus_x

print('dim: {0} x {1}'.format(num_mcpus_x, num_mcpus_y))

assert ( num_mcpus_x * num_mcpus_y <= num_mcpus )

mesh_cpus = [system.cpu[i] for i in range(1, np)]

# edges harnesses
system.harness = [Harness() for i in range(2 * num_mcpus_x + 2 * num_mcpus_y)]
harness_idx = 0

print ('harnesses: {0}'.format(len(system.harness)))

for y in range(num_mcpus_y):
  for x in range(num_mcpus_x):
    # do all mesh connections
    # it's important that these are done a particular order
    # so that vector idx are consistent
    # edges need to be connected to something! will do harness for now!
    idx   = x     + y         * num_mcpus_x
    idx_r = x + 1 + y         * num_mcpus_x
    idx_l = x - 1 + y         * num_mcpus_x
    idx_u = x     + ( y - 1 ) * num_mcpus_x
    idx_d = x     + ( y + 1 ) * num_mcpus_x
    
    print('Connecting ({0}, {1}) = {2}'.format(x, y, idx))
    
    to_right = 0
    from_left = 2
    
    to_below = 1
    from_above = 3
    
    to_left = 2
    from_right = 0
    
    to_above = 3
    from_below = 1
    
    
    # connect to the right!
    if (x + 1 < num_mcpus_x):
      mesh_cpus[idx].to_mesh_port[to_right] = mesh_cpus[idx_r].from_mesh_port[from_left]
    else:
      mesh_cpus[idx].to_mesh_port[to_right] = system.harness[harness_idx].from_cpu
      mesh_cpus[idx].from_mesh_port[from_right] = system.harness[harness_idx].to_cpu
      harness_idx += 1
      
    # connect to below
    if (y + 1 < num_mcpus_y):
      mesh_cpus[idx].to_mesh_port[to_below] = mesh_cpus[idx_d].from_mesh_port[from_above]
    else:
      mesh_cpus[idx].to_mesh_port[to_below] = system.harness[harness_idx].from_cpu
      mesh_cpus[idx].from_mesh_port[from_below] = system.harness[harness_idx].to_cpu
      harness_idx += 1
      
    # connect to the left 
    if (x - 1 >= 0):
      mesh_cpus[idx].to_mesh_port[to_left] = mesh_cpus[idx_l].from_mesh_port[from_right]
    else:
      mesh_cpus[idx].to_mesh_port[to_left] = system.harness[harness_idx].from_cpu
      mesh_cpus[idx].from_mesh_port[from_left] = system.harness[harness_idx].to_cpu
      harness_idx += 1
      
    # connect to above
    if (y - 1 >= 0):
      mesh_cpus[idx].to_mesh_port[to_above] = mesh_cpus[idx_u].from_mesh_port[from_below]
    else:
      mesh_cpus[idx].to_mesh_port[to_above] = system.harness[harness_idx].from_cpu
      mesh_cpus[idx].from_mesh_port[from_above] = system.harness[harness_idx].to_cpu
      harness_idx += 1
  



root = Root(full_system = False, system = system)
Simulation.run(options, root, system, FutureClass)
