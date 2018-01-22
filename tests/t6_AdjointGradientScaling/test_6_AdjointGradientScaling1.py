
import os
import pyTestHarness.unittest as pth
import pyTestHarness.launcher as launch
import re
import subprocess

# This tests a simple Rayleigh Taylor instability setup, in which we create the setup with a MATLAB script and comute the adjoint gradients.
# It requires MATLAB to be present on the command-line & a way to ensure that this tests is only executed when this is the case

# Test is performed on 2 cores, using opt
def test_a():

  # 1) Create a partitioning file and do not show any output of this
  os.system('mpiexec -n 2 ../bin/opt/LaMEM -ParamFile ./t6_AdjointGradientScaling/t6_AdjointGradientScaling.dat -mode save_grid > /dev/null');

  # 2) Run MATLAB to create the Particles input  (requires the environmental variable $MATLAB to be defined!)
  os.system('$MATLAB -nojvm -r "cd t6_AdjointGradientScaling; CreateMarkers_Perturbation3D(2); exit" > /dev/null')

  # Run the input script wth matlab-generated particles
  ranks = 2
  launch = '../bin/opt/LaMEM -ParamFile ./t6_AdjointGradientScaling/t6_AdjointGradientScaling.dat'
  expected_file = 't6_AdjointGradientScaling/t6_AdjointGradientScaling_p2.expected'

  def comparefunc(unittest):

    key = re.escape("|Div|_inf")
    unittest.compareFloatingPoint(key,1e-7)

    key = re.escape("|Div|_2")
    unittest.compareFloatingPoint(key,1e-5)

    key = re.escape("|mRes|_2")
    unittest.compareFloatingPoint(key,1e-4)

    key = re.escape("1.Gradient (dimensional)")
    unittest.compareFloatingPoint(key,1e-8)

    key = re.escape("2.Gradient (dimensional)")
    unittest.compareFloatingPoint(key,1e-8)

    key = re.escape("3.Gradient (dimensional)")
    unittest.compareFloatingPoint(key,1e-8)

    key = re.escape("4.Gradient (dimensional)")
    unittest.compareFloatingPoint(key,1e-8)

    key = re.escape("Computation variable [Vz] (dimensional)")
    unittest.compareFloatingPoint(key,1e-8)

  # Create unit test object
  ex1 = pth.pthUnitTest('AdjointGradientScaling_p2',ranks,launch,expected_file)
  ex1.setVerifyMethod(comparefunc)
  ex1.appendKeywords('@')

  return(ex1)
