/*  This file is part of the OpenLB library
 *
 *  Copyright (C) 2006, 2007 Jonas Latt
 *  E-mail contact: info@openlb.net
 *  The most recent release of OpenLB can be downloaded at
 *  <http://www.openlb.net/>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this program; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA  02110-1301, USA.
*/

#ifndef SIMULATION_SETUP_3D_HH
#define SIMULATION_SETUP_3D_HH

#include <cmath>
#include <vector>
#include <limits>

#include "simulationSetup3D.h"
#include "blockLattice3D.h"
#include "lbHelpers.h"
#include "firstOrderLbHelpers.h"
#include "util.h"
#include "io/ostreamManager.h"

namespace olb {

template <typename T, template<typename U> class Lattice>
void iniFirstOrder3D(BlockLatticeView3D<T,Lattice> lattice) {
  TensorFieldBase3D<T,6> const& strainRate = lattice.getDataAnalysis().getStrainRate();

  for (int iX=0; iX<lattice.getNx(); ++iX) {
    for (int iY=0; iY<lattice.getNy(); ++iY) {
      for (int iZ=0; iZ<lattice.getNz(); ++iZ) {
        T rho, u[3];
        lattice.get(iX,iY,iZ).computeRhoU(rho, u);
        T omega = lattice.get(iX,iY,iZ).getDynamics()->getOmega();
        const T uSqr = util::normSqr<T,Lattice<T>::d>(u);
        for (int iPop=0; iPop<Lattice<T>::q; ++iPop) {
          lattice.get(iX,iY,iZ)[iPop] =
            lbHelpers<T,Lattice>::equilibrium(iPop, rho, u, uSqr) +
            firstOrderLbHelpers<T,Lattice>::fromStrainToFneq (
              iPop, strainRate.get(iX,iY,iZ), omega);
        }
      }
    }
  }
}

template <typename T, template<typename U> class Lattice>
void iniPressure3D(BlockLatticeView3D<T,Lattice> lattice, T epsilon, T lambda) {
  OstreamManager clout(std::cout,"iniPressure3D");
  int lx = lattice.getNx();
  int ly = lattice.getNy();
  int lz = lattice.getNz();

  ScalarFieldBase3D<T> const& poissonTerm = lattice.getDataAnalysis().getPoissonTerm();

  ScalarField3D<T> pressure(lx, ly, lz);
  pressure.construct();

  T averagePoisson = T();
  for (int iX=0; iX<lx; ++iX) {
    for (int iY=0; iY<ly; ++iY) {
      for (int iZ=0; iZ<lz; ++iZ) {
        pressure.get(iX,iY,iZ) = T();
        averagePoisson += util::sqr(poissonTerm.get(iX,iY,iZ));
      }
    }
  }
  averagePoisson /= (lx*ly*lz);
  averagePoisson = sqrt(averagePoisson);

  int iter=0;
  T maxResidue = (T)1;
  do {
    for (int iX=1; iX<lx-1; ++iX) {
      for (int iY=1; iY<ly-1; ++iY) {
        pressure.get(iX,iY,0)    = pressure.get(iX,iY,1);
        pressure.get(iX,iY,lz-1) = pressure.get(iX,iY,lz-2);
      }
    }

    for (int iX=1; iX<lx-1; ++iX) {
      for (int iZ=0; iZ<lz; ++iZ) {
        pressure.get(iX,0,iZ)    = pressure.get(iX,1,iZ);
        pressure.get(iX,ly-1,iZ) = pressure.get(iX,ly-2,iZ);
      }
    }

    for (int iY=0; iY<ly; ++iY) {
      for (int iZ=0; iZ<lz; ++iZ) {
        pressure.get(0,iY,iZ)    = pressure.get(1,iY,iZ);
        pressure.get(lx-1,iY,iZ) = pressure.get(lx-2,iY,iZ);
      }
    }

    for (int iX=1; iX<lx-1; ++iX) {
      for (int iY=1; iY<ly-1; ++iY) {
        for (int iZ=1; iZ<lz-1; ++iZ) {
          T sumPressure =
            pressure.get(iX+1,iY  ,iZ  ) +
            pressure.get(iX-1,iY  ,iZ  ) +
            pressure.get(iX  ,iY+1,iZ  ) +
            pressure.get(iX  ,iY-1,iZ  ) +
            pressure.get(iX  ,iY  ,iZ+1) +
            pressure.get(iX  ,iY  ,iZ-1);

          pressure.get(iX,iY,iZ) =
            ((T)1-lambda) * pressure.get(iX,iY,iZ) +
            (lambda/(T)6) * (sumPressure + poissonTerm.get(iX,iY,iZ) );
        }
      }
    }

    maxResidue = std::numeric_limits<T>::min();
    for (int iX=1; iX<lx-1; ++iX) {
      for (int iY=1; iY<ly-1; ++iY) {
        for (int iZ=1; iZ<lz-1; ++iZ) {
          T sumPressure =
            pressure.get(iX+1,iY  ,iZ  ) +
            pressure.get(iX-1,iY  ,iZ  ) +
            pressure.get(iX  ,iY+1,iZ  ) +
            pressure.get(iX  ,iY-1,iZ  ) +
            pressure.get(iX  ,iY  ,iZ+1) +
            pressure.get(iX  ,iY  ,iZ-1);
          T residue = std::abs(sumPressure -(T)6*pressure.get(iX,iY,iZ)
                               + poissonTerm.get(iX,iY,iZ));
          if (residue > maxResidue) maxResidue = residue;
        }
      }
    }

    if (iter%20==0) {
      clout << "SOR iteration " << iter
            << ": max residue= "
            << maxResidue/averagePoisson << std::endl;
    }
    ++iter;
  }
  while (maxResidue/averagePoisson>epsilon);

  for (int iX=0; iX<lx; ++iX) {
    for (int iY=0; iY<ly; ++iY) {
      for (int iZ=0; iZ<lz; ++iZ) {
        lattice.get(iX,iY,iZ).defineRho (
          pressure.get(iX,iY,iZ)*Lattice<T>::invCs2 + (T)1 );
      }
    }
  }
}


template <typename T, template<typename U> class Lattice>
void convergeFixedVelocity(BlockLattice3D<T,Lattice>& lattice,
                           T epsilon, int step)
{
  OstreamManager clout(std::cout,"convergeFixedVelocity");
  BlockLatticeView3D<T,Lattice> latticeView(lattice);
  TensorFieldBase3D<T,3> const& velocity = latticeView.getDataAnalysis().getVelocity();

  T maxF = T();
  for (int iX=0; iX<latticeView.getNx(); iX+=step) {
    for (int iY=0; iY<latticeView.getNy(); iY+=step) {
      for (int iZ=0; iZ<latticeView.getNz(); iZ+=step) {
        for (int iF=0; iF<Lattice<T>::q; ++iF) {
          T f = latticeView.get(iX,iY,iZ)[iF];
          if (f>maxF) {
            maxF = f;
          }
        }
      }
    }
  }

  std::vector<T> oldValues, currentValues;
  T diff=(T)1;
  do {
    lattice.staticCollide(velocity);
    lattice.stream();

    for (int iX=0; iX<latticeView.getNx(); iX+=step) {
      for (int iY=0; iY<latticeView.getNy(); iY+=step) {
        for (int iZ=0; iZ<latticeView.getNz(); iZ+=step) {
          currentValues.push_back (
            latticeView.get(iX,iY,iZ)[(iX+iY+iZ) % (Lattice<T>::q)] );
        }
      }
    }

    if (oldValues.size() == currentValues.size()) {
      diff = T();
      for (unsigned iVal=0; iVal<oldValues.size(); ++iVal) {
        T candidate = std::abs(currentValues[iVal]-oldValues[iVal])
                      / maxF;
        if (candidate>diff) {
          diff=candidate;
        }
      }
    }
    oldValues.swap(currentValues);
    currentValues.clear();
    clout << "diff=" << diff << std::endl;
  }
  while (diff > epsilon);
}

}  // namespace olb

#endif
