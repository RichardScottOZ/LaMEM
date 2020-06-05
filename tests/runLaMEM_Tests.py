#!/usr/bin/env python
import os,sys
import subprocess
sys.path.insert(0, "./pythontestharness/lib")

import argparse

#import pyTestHarness.unittest as pth
import pyTestHarness.harness as pthharness
import pyTestHarness.launcher as launch

# Build optimized and debug versions of LaMEM
os.system('cd ../src/;  make mode=opt all; cd ../tests')
os.system('cd ../src/;  make mode=deb all; cd ../tests')

# Import separate tests
sys.path.append(os.path.join(os.environ['PWD'], 't1_FB1_Direct'))
sys.path.append(os.path.join(os.environ['PWD'], 't2_FB2_MG'))
sys.path.append(os.path.join(os.environ['PWD'], 't4_Loc'))
sys.path.append(os.path.join(os.environ['PWD'], 't5_Perm'))
sys.path.append(os.path.join(os.environ['PWD'], 't6_AdjointGradientScaling'))
sys.path.append(os.path.join(os.environ['PWD'], 't7_AdjointGradientInversion'))
sys.path.append(os.path.join(os.environ['PWD'], 't8_AdjointGradients'))
sys.path.append(os.path.join(os.environ['PWD'], 't9_FB1_Direct_PhaseDiagrams'))
sys.path.append(os.path.join(os.environ['PWD'], 't10_Compressibility'))
sys.path.append(os.path.join(os.environ['PWD'], 't11_Subgrid'))
sys.path.append(os.path.join(os.environ['PWD'], 't12_Temperature_diffusion'))
sys.path.append(os.path.join(os.environ['PWD'], 't13_Rheology0D'))
sys.path.append(os.path.join(os.environ['PWD'], 't15_RTI'))

# add matlab-tests if matlab is available as ENVIRONMENTAL variable MATLAB
if os.environ.get('MATLAB') != None:
    sys.path.append(os.path.join(os.environ['PWD'], 't3_SubductionMATLABinput'))
else:
    print('MATLAB tests cannot be executed, as the environmental variable $MATLAB is not set')

import test_1_FB1 as FB1
import test_2_FB2 as FB2
import test_4_localization as Loc1
import test_5_permeability as Permeability
import test_6_AdjointGradientScalingLaws  as Adj1 
import test_7_AdjointGradientInversion  as Adj2 
import test_8_AdjointGradients  as Adj3 
import test_9_FB_PhaseDiagrams1 as FBPD1
import test_10_Compressibility as Comp1
import test_11_SubGrid as Subgrid
import test_12_TpD as Diffusion
import test_13_Rheology0D as Rheology0D
import test_15_RTI as RTI


if os.environ.get('MATLAB') != None:
  import test_3_Subduction1               as Sub1 # import test that requires MATLAB

def makeLocalPathAbsolute(localRelPath) :
  thisDir = os.path.split(os.path.abspath(__file__))[0]
  return(os.path.join(thisDir,localRelPath))

def run_tests():
  os.environ['PYTHONUNBUFFERED'] = str('1')

  registeredTests = [ FB1.test_a(),   FB1.test_b(),  FB1.test_c(),  FB1.test_d(),
                      FB2.test_a(),   Loc1.test_a(), Loc1.test_b(), Loc1.test_c(), FBPD1.test_a(),
                      Comp1.test_a(), Comp1.test_b(), Subgrid.test_a(), Diffusion.test_1D(), 
                      Adj1.test_RTI_1(), Adj1.test_RTI_2(),
                      Adj2.SubductionInversion_GD(), Adj2.SubductionInversion_TAO(), Adj2.SubductionInversion_FD_TAO(),
                      Adj3.rho_SensitivityKernel(), Adj3.FallingSphere_ND_all(), Adj3.FallingSphere_ND_CompareGradients_1(),
                      Adj3.FallingSphere_ND_CompareGradients_2(), Adj3.SubductionSetup_Dimensional(),Adj3.PSD_ND(),
                      Permeability.test_a(), 
                      Rheology0D.ViscoElastic(),   Rheology0D.ViscoElastoPlastic(), Rheology0D.ViscoElastoPlastic_DislocationCreep(),
                      Rheology0D.LinearViscous(),  Rheology0D.DislocationCreeplaw(), Rheology0D.ViscoElastic_DislocationCreep(),
                      RTI.RTI_isovisous_NoSlip()];
 

# Add matlab tests (There should be a better way to do this for a range of files at the same time)
  if os.environ.get('MATLAB') != None:
    registeredTests.append(Sub1.test_a());
    registeredTests.append(Sub1.test_b());
    registeredTests.append(Sub1.test_c());
    registeredTests.append(Sub1.test_d());
    #registeredTests.append(Adj1.test_a());
    #registeredTests.append(Adj2.test_a());

  # Run the tests:
  h = pthharness.Harness(registeredTests)
  h.execute()
  h.verify()


if __name__ == "__main__":
  run_tests()

  os.system('make clean')

