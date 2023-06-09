'''
  Gives info on layouts used in vector configs for analysis

  TODO would be cool if a single file that both software and this script read to know layouts
  But for now replicate here
'''

from math import sqrt

v4_layout = {
  'template_dim' : [ 4, 4 ],
  'groups' : [
    {
      'scalar' : [ 0, 0 ],
      'vector' : [ 1, 0 ],
      'dim'    : [ 2, 2 ]
    },
    {
      'scalar' : [ 0, 1 ],
      'vector' : [ 0, 2 ],
      'dim'    : [ 2, 2 ]
    },
    {
      'scalar' : [ 3, 1 ],
      'vector' : [ 2, 2 ],
      'dim'    : [ 2, 2 ]
    }
  ]
}

v16_layout = {
  'template_dim' : [ 8, 8 ],
  'groups' : [
    {
      'scalar' : [ 0, 4 ],
      'vector' : [ 0, 0 ],
      'dim'    : [ 4, 4 ]
    },
    {
      'scalar' : [ 7, 4 ],
      'vector' : [ 4, 0 ],
      'dim'    : [ 4, 4 ]
    },
    {
      'scalar' : [ 1, 4 ],
      'vector' : [ 2, 4 ],
      'dim'    : [ 4, 4 ]
    }
  ]
}

# names that are associated with vector configs
v4_configs = [
  'V4',
  'VEC_4_SIMD_VERTICAL',
  'VEC_4_REUSE_VERTICAL',
  'VEC_4_SIMD_UNROLL'
]
v16_configs = [
  'V16'
]

# get the layout correspnding to the name
def get_layout(config_name):
  if (config_name == 'V4'):
    return v4_layout
  else:
    return v16_layout

# get if loc is contained in rectangle with bot-left _base_ and dimensions _dim_
def loc_contained(loc, base, dim):
  return \
    (base[0] <= loc[0] and loc[0] < base[0] + dim[0]) and \
    (base[1] <= loc[1] and loc[1] < base[1] + dim[1])
    

# give how far this core is from scalar in terms of hops
def get_mesh_dist(config_name, core_id, num_cpus=64):
  # unflatten
  grid_dim = [ int(sqrt(num_cpus)), int(sqrt(num_cpus)) ]
  core_loc = [ core_id % grid_dim[0], core_id / grid_dim[0] ]
  
  # get where this core is within the template
  template_dim = get_layout(config_name)['template_dim']
  virt_core_loc = [
    core_loc[0] % template_dim[0],
    core_loc[1] % template_dim[1]
  ]

  # determine which group we are in
  for group in get_layout(config_name)['groups']:
    # scalar core has no distance
    if loc_contained(virt_core_loc, group['scalar'], [ 1, 1 ]):
      return 0
    # figure out distance from the scalar core in the group, assumes optimal
    # forwarding path (in terms of number of hops) which I think we're doing
    elif loc_contained(virt_core_loc, group['vector'], group['dim']):
      scalar_loc = group['scalar']

      disp = [
        abs(virt_core_loc[0] - scalar_loc[0]),
        abs(virt_core_loc[1] - scalar_loc[1])
      ]
      # manhattan dist
      dist = disp[0] + disp[1]
      return dist

  return -1

# get a sequence of hops based on core id in order
def get_mesh_dist_sequence(config_name, num_cpus=64):
  seq = []
  for i in range(num_cpus):
    seq.append(get_mesh_dist(config_name, i, num_cpus))
  return seq

def is_vec_4_config(config_name):
  return config_name in v4_configs

def is_vec_16_config(config_name):
  return config_name in v16_configs

def is_vec_config(config_name):
  return is_vec_4_config(config_name) or is_vec_16_config(config_name)
