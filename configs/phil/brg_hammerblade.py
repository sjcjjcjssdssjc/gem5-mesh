#==============================================================================
# brg_scratchpad.py
#==============================================================================
# Python configuration file for BRG scratchpad system
#
# Author: Tuan Ta
# Date  : 19/07/09

import optparse
import sys
import os
import math

import m5
from m5.defines import buildEnv
from m5.objects import *
from m5.util import addToPath, fatal, warn

addToPath('../')

from ruby import Ruby
from topologies.Crossbar import *
from network.Network import *

from common import Options
from common import Simulation
from common import CacheConfig
from common import CpuConfig
from common import MemConfig
from common.Caches import *

from math import log

#------------------------------------------------------------------------------
# Get workload process
#------------------------------------------------------------------------------

def get_processes(options):
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

    return multiprocesses

#------------------------------------------------------------------------------
# Make network
#------------------------------------------------------------------------------
# This creates a mesh network n_rows x n_cols
# The first (n_rows - 1) rows are connected to either CPUs and/or xcels
# The last row is connected to L2 banks

def makeMeshTopology(n_rows, n_cols, n_cpus, n_xcels, system, network,
                     IntLink, ExtLink, Router):
  assert(n_rows >= 2)
  assert(n_cols >= 1)

  num_routers = n_rows * n_cols;

  link_latency = 1
  router_latency = 1

  #n_cpus = len(system.cpu)
  #n_xcels = len(system.xcel)

  # all controllers
  icaches   = system.icaches
  cpu_sps   = system.scratchpads[:n_cpus]
  xcel_sps  = system.scratchpads[n_cpus:]
  l2s       = system.l2_cntrls


  print('cpu {} router {} pad {} l2s {}'.format(
    n_cpus, num_routers, len(cpu_sps), len(l2s)))
  assert(len(icaches) == n_cpus)
  assert(len(cpu_sps) == n_cpus)
  assert(n_cpus <= num_routers)
  assert(n_xcels <= num_routers)
  assert(len(l2s) <= n_cols)
  #assert(len(l2s) + n_cpus + n_xcels == num_routers)

  # create all routers
  routers = [ Router(router_id = i,
                     latency = router_latency) for i in range(num_routers) ]
  network.routers = routers

  # link count used to set unique link IDs
  link_count = 0

  #--------------------------
  # Set up all external links
  #--------------------------

  ext_links = []

  # add all CPU I-caches and SPs to the first few routers
  for i in xrange(n_cpus):
    icache_ext_link = ExtLink(link_id   = link_count,
                              ext_node  = icaches[i],
                              int_node  = routers[i],
                              latency   = link_latency)
    link_count += 1
    ext_links.append(icache_ext_link)

    cpu_sp_ext_link = ExtLink(link_id   = link_count,
                              ext_node  = cpu_sps[i],
                              int_node  = routers[i],
                              latency   = link_latency)
    link_count += 1
    ext_links.append(cpu_sp_ext_link)

  # add all Xcel SPs to all routers
  for i in xrange(n_xcels):
    xcel_ext_link = ExtLink(link_id   = link_count,
                            ext_node  = xcel_sps[i],
                            int_node  = routers[i],
                            latency   = link_latency)
    link_count += 1
    ext_links.append(xcel_ext_link)

  # add all l2s to bottom routers
  l2_idx = 0
  for i in xrange(n_cols * (n_rows - 1), num_routers):
    if l2_idx < len(l2s):
      l2_ext_link = ExtLink(link_id   = link_count,
                            ext_node  = l2s[l2_idx],
                            int_node  = routers[i],
                            latency   = link_latency)
      l2_idx += 1
      link_count += 1
      ext_links.append(l2_ext_link)

  network.ext_links = ext_links

  #--------------------------
  # Set up all internal links
  #--------------------------

  int_links = []

  horiz_weight = 2
  towards_l2_weight = 1
  away_l2_weight = 3

  # East output to West input links (weight = 1)
  for row in xrange(n_rows):
    for col in xrange(n_cols):
      if (col + 1 < n_cols):
        east_out  = col + (row * n_cols)
        west_in   = (col + 1) + (row * n_cols)
        int_links.append(IntLink(link_id      = link_count,
                                 src_node     = routers[east_out],
                                 dst_node     = routers[west_in],
                                 src_outport  = "East",
                                 dst_inport   = "West",
                                 latency      = link_latency,
                                 weight       = horiz_weight ))
        link_count += 1

  # West output to East input links (weight = 1)
  for row in xrange(n_rows):
    for col in xrange(n_cols):
      if (col + 1 < n_cols):
        east_in  = col + (row * n_cols)
        west_out = (col + 1) + (row * n_cols)
        int_links.append(IntLink(link_id      = link_count,
                                 src_node     = routers[west_out],
                                 dst_node     = routers[east_in],
                                 src_outport  = "West",
                                 dst_inport   = "East",
                                 latency      = link_latency,
                                 weight       = horiz_weight ))
        link_count += 1

  # North output to South input links (weight = 2)
  for col in xrange(n_cols):
    for row in xrange(n_rows):
      if (row + 1 < n_rows):
        north_out = col + (row * n_cols)
        south_in = col + ((row + 1) * n_cols)
        int_links.append(IntLink(link_id      = link_count,
                                 src_node     = routers[north_out],
                                 dst_node     = routers[south_in],
                                 src_outport  = "North",
                                 dst_inport   = "South",
                                 latency      = link_latency,
                                 weight       = away_l2_weight ))
        link_count += 1

  # South output to North input links (weight = 2)
  for col in xrange(n_cols):
    for row in xrange(n_rows):
      if (row + 1 < n_rows):
        north_in  = col + (row * n_cols)
        south_out = col + ((row + 1) * n_cols)
        int_links.append(IntLink(link_id      = link_count,
                                 src_node     = routers[south_out],
                                 dst_node     = routers[north_in],
                                 src_outport  = "South",
                                 dst_inport   = "North",
                                 latency      = link_latency,
                                 weight       = towards_l2_weight ))
        link_count += 1

  network.int_links = int_links

def makeSystolicTopology(system, tiles_x, tiles_y):

  # edges harnesses to take bad packets
  system.harness = [Harness() for i in range(2 * tiles_x + 2 * tiles_y)]
  harness_idx = 0

  # get reference to the cpus already in the system
  cpus = system.cpu

  #print ('harnesses: {0}'.format(len(system.harness)))

  for y in range(tiles_y):
    for x in range(tiles_x):
      # do all mesh connections
      # it's important that these are done a particular order
      # so that vector idx are consistent
      # edges need to be connected to something! will do harness for now!
      idx   = x     + y         * tiles_x
      idx_r = x + 1 + y         * tiles_x
      idx_l = x - 1 + y         * tiles_x
      idx_u = x     + ( y - 1 ) * tiles_x
      idx_d = x     + ( y + 1 ) * tiles_x
    
      #print('Connecting ({0}, {1}) = {2}'.format(x, y, idx))
    
      to_right = 0
      from_left = 2
    
      to_below = 1
      from_above = 3
    
      to_left = 2
      from_right = 0
    
      to_above = 3
      from_below = 1
    
    
      # connect to the right!
      if (x + 1 < tiles_x):
        cpus[idx].to_mesh_port[to_right] = cpus[idx_r].from_mesh_port[from_left]
      else:
        cpus[idx].to_mesh_port[to_right] = system.harness[harness_idx].from_cpu
        cpus[idx].from_mesh_port[from_right] = system.harness[harness_idx].to_cpu
        harness_idx += 1
      
      # connect to below
      if (y + 1 < tiles_y):
        cpus[idx].to_mesh_port[to_below] = cpus[idx_d].from_mesh_port[from_above]
      else:
        cpus[idx].to_mesh_port[to_below] = system.harness[harness_idx].from_cpu
        cpus[idx].from_mesh_port[from_below] = system.harness[harness_idx].to_cpu
        harness_idx += 1
      
      # connect to the left 
      if (x - 1 >= 0):
        cpus[idx].to_mesh_port[to_left] = cpus[idx_l].from_mesh_port[from_right]
      else:
        cpus[idx].to_mesh_port[to_left] = system.harness[harness_idx].from_cpu
        cpus[idx].from_mesh_port[from_left] = system.harness[harness_idx].to_cpu
        harness_idx += 1
      
      # connect to above
      if (y - 1 >= 0):
        cpus[idx].to_mesh_port[to_above] = cpus[idx_u].from_mesh_port[from_below]
      else:
        cpus[idx].to_mesh_port[to_above] = system.harness[harness_idx].from_cpu
        cpus[idx].from_mesh_port[from_above] = system.harness[harness_idx].to_cpu
        harness_idx += 1


#------------------------------------------------------------------------------
# Parse options
#------------------------------------------------------------------------------

parser = optparse.OptionParser()
Options.addCommonOptions(parser)
Options.addSEOptions(parser)
#Options.addBRGOptions(parser)
Ruby.define_options(parser)

# Scratchpad size
parser.add_option("--spm-size", action="store", type="string", 
  default="4kB", help="Specify the scratchpad memory size")

# number of pending requests allowed by scratchpad
parser.add_option("--stream-width", type = "int", default = 8)

# whether to use vector stage or not
parser.add_option("--vector", action="store_true", default=False,
  help="Use vector stage in pipe")

(options, args) = parser.parse_args()

# figure out system size
n_cpus  = options.num_cpus
n_xcels = 0 #options.num_xcels
n_tiles = n_cpus + n_xcels

# mesh size is determined by the number of xcels and device cpus
n_cols  = int(math.sqrt(n_tiles))

# this extra row of routers is for the l2s
n_rows  = n_cols + 1

n_l2s   = n_cols

# network classes
#assert(options.network == "garnet2.0")
options.network = "garnet2.0"
# virt_channels = 64
# options.vcs_per_vnet = virt_channels
NetworkClass = GarnetNetwork
IntLinkClass = GarnetIntLink
ExtLinkClass = GarnetExtLink
RouterClass = GarnetRouter
InterfaceClass = GarnetNetworkInterface

# Do not support multi-process simulation
process = get_processes(options)[0]

#------------------------------------------------------------------------------
# Construct CPUs
#------------------------------------------------------------------------------

# CPU class
CPUClass = IOCPU (
  includeVector = options.vector,
  meshBufferSize = 2
  ,
  numROBEntries = 8
)

# Create top-level system
system = System(cpu = [ CPUClass(cpu_id = i) for i in xrange(n_cpus) ],
                        mem_mode = CPUClass.memory_mode(),
                        mem_ranges = [ AddrRange(options.mem_size) ],
                        cache_line_size = 64) #options.cacheline_size)

# Create a top-level voltage domain
system.voltage_domain = VoltageDomain(voltage = options.sys_voltage)

# Create a source clock for the system and set the clock period
system.clk_domain = SrcClockDomain(clock =  options.sys_clock,
                                   voltage_domain = system.voltage_domain)

# Create a CPU voltage domain
system.cpu_voltage_domain = VoltageDomain()

# Create a separate clock domain for the CPUs
system.cpu_clk_domain = SrcClockDomain(clock = options.sys_clock,
                                       voltage_domain =
                                       system.cpu_voltage_domain)

# All cpus belong to a common cpu_clk_domain, therefore running at a common
# frequency.
for cpu in system.cpu:
    cpu.clk_domain = system.cpu_clk_domain

# Assign workload to CPUs
for i in xrange(n_cpus):
  system.cpu[i].workload = process
  system.cpu[i].createThreads()

#------------------------------------------------------------------------------
# Construct Ruby memory system
#------------------------------------------------------------------------------

system.ruby = RubySystem()

# Construct network
network = NetworkClass (ruby_system = system.ruby,
                        routers = [],
                        ext_links = [],
                        int_links = [],
                        netifs = [],
                        number_of_virtual_networks = 2, # what does it mean to have two networks??
                        #vcs_per_vnet=virt_channels
                        )
                        

# Scratchpads
#n_scratchpads = n_cpus + n_xcels
n_scratchpads = n_tiles
scratchpads = []

for i in xrange(n_scratchpads):
  sp = Scratchpad(version           = i,
                  ruby_system       = system.ruby,
                  sp_size           = AddrRange(options.spm_size).size(),
                  dram_size         = AddrRange(options.mem_size).size(),
                  num_l2s           = n_l2s,
                  grid_dim_x        = n_cols,
                  grid_dim_y        = n_cols,
                  # might be too big but can solve by having spad not remember
                  # store noacks like it does for prefetch
                  maxNumPendingReqs = options.stream_width,
                  prefetchBufSize   = 10000, # make ridic number
                  numFrameCntrs     = 48,
                  cpu               = system.cpu[i])

  sp.memReqBuffer             = MessageBuffer(ordered = True)
  sp.memReqBuffer.master      = network.slave

  sp.memRespBuffer            = MessageBuffer(ordered = True)
  sp.memRespBuffer.slave      = network.master

  sp.remoteReqBuffer          = MessageBuffer(ordered = True)
  sp.remoteReqBuffer.slave    = network.master

  sp.remoteRespBuffer         = MessageBuffer(ordered = True)
  sp.remoteRespBuffer.master  = network.slave

  scratchpads.append(sp)

system.scratchpads = scratchpads

# L1-I caches and sequencers
n_icaches = n_cpus
icache_cntrls = []
sequencers = []
for i in xrange(n_icaches):
  icache = RubyCache(size = '4kB', assoc = 2)
  icache_cntrl = L1Cache_Controller(version = i,
                                    L1cache = icache,
                                    transitions_per_cycle = options.ports,
                                    ruby_system = system.ruby)

  icache_cntrl.requestToNetwork          = MessageBuffer(ordered = True)
  icache_cntrl.requestToNetwork.master   = network.slave
  icache_cntrl.responseFromNetwork       = MessageBuffer(ordered = True)
  icache_cntrl.responseFromNetwork.slave = network.master
  icache_cntrl.mandatoryQueue            = MessageBuffer(ordered = True)

  sequencer = RubySequencer()
  sequencer.version = i
  sequencer.icache = icache
  # only 1 cycle resp latency now (so 1 total)
  # need to hack sequencer to turn 1 cycle into a 1 tick wait 
  # (effectively 0 cycles but plays nicely with wakeup queues)
  sequencer.icache_hit_latency = 0
  sequencer.dcache = icache
  sequencer.ruby_system = system.ruby
  sequencer.is_cpu_sequencer = True

  icache_cntrl.sequencer = sequencer

  icache_cntrls.append(icache_cntrl)
  sequencers.append(sequencer)

system.icaches = icache_cntrls
system.ruby.num_of_sequencers = len(sequencers)
system.ruby.number_of_virtual_networks = 2

# L2 cache
l2_cntrls = []

if n_l2s == 1:
  l2_size = '256kB'
elif n_l2s == 2:
  l2_size = '128kB'
elif n_l2s == 4:
  l2_size = '64kB'
elif n_l2s == 8:
  l2_size = '32kB'
else:
  fatal("Invalid number of L2 banks")

for i in xrange(n_l2s):
  l2_cache = RubyCache(size = l2_size, assoc = 4)
  l2_cntrl = L2Cache_Controller(version = i,
                                cacheMemory = l2_cache,
                                transitions_per_cycle = 16,
                                meshDimX = n_cols,
                                meshDimY = n_cols,
                                ruby_system = system.ruby
                                ,
                                cache_resp_latency = 1,
                                to_memory_controller_latency = 1,
                                mem_to_cpu_latency = 1 # TODO this needs to be the same as cache_resp_latency b/c same ordered queue?
                                )
                                #number_of_TBEs = 1)

  l2_cntrl.requestToLLC           = MessageBuffer(ordered = True)
  l2_cntrl.requestToLLC.slave     = network.master

  l2_cntrl.responseFromLLC        = MessageBuffer(ordered = True)
  l2_cntrl.responseFromLLC.master = network.slave

  l2_cntrl.responseFromMemory     = MessageBuffer(ordered = True)

  l2_cntrls.append(l2_cntrl)


system.l2_cntrls = l2_cntrls

#------------------------------------------------------------------------------
# Connect all controllers to network
#------------------------------------------------------------------------------

makeMeshTopology(n_rows, n_cols, n_cpus, n_xcels, system, network,
                 IntLinkClass, ExtLinkClass, RouterClass)

init_network(options, network, InterfaceClass)

system.network = network

system.ruby.sys_port_proxy = RubyPortProxy(ruby_system = system.ruby)
system.system_port = system.ruby.sys_port_proxy.slave

#------------------------------------------------------------------------------
# Construct systolic network
#------------------------------------------------------------------------------

if (options.vector):
  makeSystolicTopology(system, n_rows - 1, n_cols)

#------------------------------------------------------------------------------
# Construct memory controller
#------------------------------------------------------------------------------

system.mem_mode = 'timing'
system.mem_ranges = [ AddrRange(options.mem_size) ]

system.mem_ctrl = SimpleMemory()
system.mem_ctrl.range = system.mem_ranges[0]
system.mem_ctrl.latency = '60ns'
system.mem_ctrl.bandwidth = '16GB/s'

#------------------------------------------------------------------------------
# Construct a crossbar that connects L2s and mem_ctrl
#------------------------------------------------------------------------------

system.l2_bus = NoncoherentXBar()

# 16 bytes per cycle. This is set to match with the mem_ctrl.bandwidth
system.l2_bus.width = 16
system.l2_bus.frontend_latency = 1
system.l2_bus.forward_latency = 1
system.l2_bus.response_latency = 1
system.l2_bus.clk_domain = system.clk_domain

for i in xrange(n_l2s):
  system.l2_bus.slave = system.l2_cntrls[i].memory

system.l2_bus.master = system.mem_ctrl.port

#------------------------------------------------------------------------------
# Connect memory controller and CPUs to the Ruby system
#------------------------------------------------------------------------------

#system.mem_ctrl.port = l2_cntrl.memory

for i in xrange(n_cpus):
  system.cpu[i].createInterruptController()
  system.cpu[i].icache_port = sequencers[i].slave
  system.cpu[i].dcache_port = system.scratchpads[i].cpu_port

for i in xrange(n_xcels):
  system.xcel[i].mem_port = system.scratchpads[n_cpus + i].cpu_port

#------------------------------------------------------------------------------
# Simulation
#------------------------------------------------------------------------------

# set up the root SimObject and start the simulation
root = Root(full_system = False, system = system)

# instantiate all of the objects we've created above
m5.instantiate()

print("Beginning simulation!")
exit_event = m5.simulate()
print('Exiting @ tick %i because %s' % (m5.curTick(), exit_event.getCause()))
print('Exit code %i' % exit_event.getCode())
