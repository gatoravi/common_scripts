#!/bin/bash
#
#$ -l mem_free=3G
#$ -S /bin/bash -cwd
#$ -M r.avinash@ufl.edu -m e


for r in 1 2 3 4 5
do
  
  time /home/r.avinash/MFAST/Apr3_comb/NW_changed_min/src/nw_match /home/r.avinash/MFAST/Apr3_comb/NW_changed_min/src/seeds/$1$2${3}rep${r}70_cfs /home/r.avinash/MFAST/Apr3_comb/NW_changed_min/src/trees/$1$2${3}rep${r} 70 

done
