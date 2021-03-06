/*
** Copyright 2007-2018 RTE
** Authors: Antares_Simulator Team
**
** This file is part of Antares_Simulator.
**
** Antares_Simulator is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** There are special exceptions to the terms and conditions of the
** license as they are applied to this software. View the full text of
** the exceptions in file COPYING.txt in the directory of this software
** distribution
**
** Antares_Simulator is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with Antares_Simulator. If not, see <http://www.gnu.org/licenses/>.
**
** SPDX-License-Identifier: licenceRef-GPL3_WITH_RTE-Exceptions
*/

#include <yuni/yuni.h>
#include <yuni/core/math.h>
#include <antares/study.h>
#include <antares/logs.h>
#include "xcast.h"
#include "constants.h"
#include "../../misc/cholesky.h"
#include "../../misc/matrix-dp-make.h"
#include "math.hxx"


using namespace Yuni;



namespace Antares
{
namespace Solver
{
namespace TSGenerator
{
namespace XCast
{


	bool XCast::generateValuesForTheCurrentDay()
	{
		enum { nbHoursADay = 24, };

		
		uint processCount = (uint) pData.localareas.size();
		
		float shrink;

		
		float x;
		
		uint Compteur_ndp = 0;

		
		
		if (pNeverInitialized)
		{
			pNeverInitialized = false;
			pNewMonth = true;
			for (uint s = 0; s != processCount; ++s)
			{
				if (!verification(A[s], B[s], G[s], D[s], L[s], T[s]))
				{
					logs.error() << "TS " << pTSName << " generator: invalid local parameters (" << (s + 1) << ')';
					return false;
				}
				
				POSI[s] = 0.f;
			}
		}

		
		if (pNewMonth)
		{
			if (Cholesky<float>(Triangle_courant, pCorrMonth->entry, processCount, pQCHOLTotal))
			{
				
				
				for (uint i = 0; i != processCount; ++i)
				{
					
					for (uint j = 0; j < i; ++j)
						pCorrMonth->entry[i][j] *= 0.999f;
				}

				if (Cholesky<float>(Triangle_courant, pCorrMonth->entry, processCount, pQCHOLTotal))
				{
					
					logs.error() << "TS " << pTSName << " generator: invalid correlation matrix";
					return false;
				}
			}

			for (uint s = 0; s != processCount; ++s)
			{
				MAXI[s]  = maximum(   A[s], B[s], G[s], D[s], L[s]);
				MINI[s]  = 0.f;  
				ESPE[s]  = esperance( A[s], B[s], G[s], D[s], L[s]);
				STDE[s]  = standard(  A[s], B[s], G[s], D[s], L[s]);

				Presque_maxi[s] = ESPE[s] + (1.f - EPSIBOR) * (MAXI[s] - ESPE[s]);
				Presque_mini[s] = ESPE[s] + (1.f - EPSIBOR) * (MINI[s] - ESPE[s]);

				if (Presque_mini[s] > Presque_maxi[s])
				{
					
					
					logs.error() << "TS " << pTSName << " generator: invalid local parameters";
					return false;
				}
				D_COPIE[s] = diffusion(A[s], B[s], G[s], D[s], L[s], T[s], ESPE[s]);
			}

			
			for (uint s = 0; s != processCount; ++s)
			{
				for (uint t = 0; t < s; ++t)
				{
					float z = D_COPIE[t] * STDE[s];
					if (Math::Zero(z))
						CORR[s][t] = 0.f;
					else
					{
						x = D_COPIE[s] * STDE[t] / z;
						CORR[s][t] = (*pCorrMonth)[s][t] * (x + 1.f / x) / 2.f;
						if (CORR[s][t] >  1.f)
						{
							CORR[s][t] = 1.f;
							++pLevellingCount;
						}
						else
						{
							if (CORR[s][t] < -1.f)
							{
								CORR[s][t] = -1.f;
								++pLevellingCount;
							}
						}
					}
				}

				
				CORR[s][s] = 1.f;
			}

			
			shrink = MatrixDPMake<float>(Triangle_courant, CORR, Carre_reference, pCorrMonth->entry, processCount, pQCHOLTotal);
			if (shrink == -1.f)
			{
				
				logs.error() << "TS " << pTSName << " generator: invalid correlation matrix";
				return false;
			}
			
			Compteur_ndp = (shrink < 1.f) ? 100 : 0;

			
			STEP = 1.f;
			for (uint s = 0; s != processCount; ++s)
			{
				x = 1.f;
				if (T[s] > PETIT)
					x = PETIT / T[s];
				if (x < STEP)
				{
					
					STEP = x;
				}

				x = maxiDiffusion(A[s], B[s], G[s], D[s], L[s], T[s]);
				if (x > 0.f)
				{
					x = STDE[s] / x;
					x *= x;
					
					x *= 4.f * PETIT;
					if (x < STEP)
						STEP = x;
				}
			}
			if (STEP < float(1e-2))
			{
				
				STEP = float(1e-2);
				Nombre_points_intermediaire = 100;
			}
			else
			{
				
				Nombre_points_intermediaire = (uint)(1.f / STEP);
				STEP = 1.f / float(Nombre_points_intermediaire);
			}

			SQST = sqrt(STEP);

			
			
			
			for (uint s = 0; s != processCount; ++s)
			{
				if (POSI[s] > 0.f)
					POSI[s] *= (MAXI[s] - ESPE[s]);
				else
					POSI[s] *= (ESPE[s] - MINI[s]);

				POSI[s] += ESPE[s];

				if (POSI[s] >= MAXI[s])
					POSI[s] = Presque_maxi[s];
				if (POSI[s] <= MINI[s])
					POSI[s] = Presque_mini[s];

				
				if (M[s] > 1.f)
				{
					for (uint i = 0; i < nbHoursADay; ++i)
					{
						LISS[s][i] = POSI[s] / M[s];
					}
					DATL[s][nbHoursADay - 1] = POSI[s];
				}
			}
		}
		else
		{
			for (uint s = 0; s != processCount; ++s)
			{
				if (POSI[s] > 0.f)
					POSI[s] *= (MAXI[s]-ESPE[s]);
				else
					POSI[s] *= (ESPE[s]-MINI[s]);

				POSI[s] += ESPE[s];

				if (POSI[s] >= MAXI[s])
					POSI[s] = Presque_maxi[s];
				if (POSI[s] <= MINI[s])
					POSI[s] = Presque_mini[s];
			}
		}

		
		
		
		

		for (uint i = 0; i != nbHoursADay; ++i)
		{
			
			for (uint l = 0; l != Nombre_points_intermediaire; ++l)
			{
				++pComputedPointCount;

				
				for (uint s = 0; s != processCount; ++s)
					DIFF[s] = diffusion(A[s], B[s], G[s], D[s], L[s], T[s], POSI[s]);

				
				if (pAccuracyOnCorrelation)
				{
					
					float c;
					float z;

					for (uint s = 0; s != processCount; ++s)
					{
						float* corr_s = CORR[s];
						auto& userMonthlyCorr = pCorrMonth->column(s);
						for (uint t = 0; t < s; ++t)
						{
							if (Math::Zero(DIFF[s]) || Math::Zero(DIFF[t]))
								corr_s[t] = 0;
							else
							{
								z = DIFF[t] * STDE[s];
								x = DIFF[s] * STDE[t] / z;
								c = userMonthlyCorr[t] * (x + 1.f / x) / 2.f;

								if (c > 1.f)
								{
									c = 1.f;
									++pLevellingCount;
								}
								else
								{
									if (c < -1.f)
									{
										c = -1.f;
										++pLevellingCount;
									}
								}
								corr_s[t] = c;
							}
						}
						
						corr_s[s] = 1.f;
					}

					shrink = MatrixDPMake<float>(Triangle_courant, CORR, Carre_courant, Carre_reference, processCount, pQCHOLTotal);
					if (shrink <= 1.f)
					{
						if (shrink == -1.f)
						{
							
							logs.error() << "TS " << pTSName << " generator: invalid correlation matrix";
							return false;
						}
						if (shrink < 1.f)
							++pNDPMatrixCount;
					}
				} 


				
				uint j = processCount;
				if ((processCount - 2 * (processCount / 2)) != 0)
					++j;
				for (uint k = 0; k < j; ++k)
					normal(WIEN[k], WIEN[j - (1 + k)]);

				
				for (uint s = 0; s != processCount; ++s)
				{
					BROW[s] = 0.f;
					for (uint t = 0; t < s + 1; ++t)
						BROW[s] += Triangle_courant[s][t] * WIEN[t]; 
				}

				
				for (uint s = 0; s != processCount; ++s)
				{
					TREN[s] = T[s] * (ESPE[s] - POSI[s]);
					DIFF[s] = DIFF[s] * BROW[s];

				}

				
				for (uint s = 0; s != processCount; ++s)
				{
					POSI[s] += (TREN[s] * STEP) + (DIFF[s] * SQST);
					if (POSI[s] >= MAXI[s])
						POSI[s] = Presque_maxi[s];
					if (POSI[s] <= MINI[s])
						POSI[s] = Presque_mini[s];
				}
			}

			for (uint s = 0; s != processCount; ++s)
			{
				float data_si;
				data_si  = POSI[s] + G[s]; 
				data_si *= FO[s][i];


				
				if (BO[s] == true)
				{
					if (data_si > MA[s])
					{
						data_si = MA[s];
						if (Math::Abs(FO[s][i]) > 0.f)
						{
							POSI[s]  = MA[s] / FO[s][i];
							POSI[s] -= G[s];
							if (POSI[s] >= MAXI[s])
								POSI[s] = Presque_maxi[s];
							if (POSI[s] <= MINI[s])
								POSI[s] = Presque_mini[s];
						}
					}
					if (data_si < MI[s])
					{
						data_si = MI[s];
						if (Math::Abs(FO[s][i]) > 0.f)
						{
							POSI[s]  = MI[s] / FO[s][i];
							POSI[s] -= G[s];
							if (POSI[s] >= MAXI[s])
								POSI[s] = Presque_maxi[s];
							if (POSI[s] <= MINI[s])
								POSI[s] = Presque_mini[s];
						}
					}
				}

				
				if (M[s] > 1)
				{
					LISS[s][i]  = POSI[s] / M[s];
					DATL[s][i]  = DATL[s][(nbHoursADay + i - 1)    % nbHoursADay];
					DATL[s][i] -= LISS[s][(nbHoursADay + i - M[s]) % nbHoursADay];
					DATL[s][i] += LISS[s][i];
					
					data_si     = FO[s][i] * (DATL[s][i] + G[s]);

					if (BO[s]) 
					{
						if (data_si > MA[s])
							data_si = MA[s];
						if (data_si < MI[s])
							data_si = MI[s];
					}
				}

				assert(0 == Math::Infinite(data_si) && "Infinite value");
				DATA[s][i] = data_si;
			}
		}

		
		
		
		
		
		
		
		
		for (uint s = 0; s != processCount; ++s)
		{
			
			if (POSI[s] > ESPE[s])
				POSI[s] = (POSI[s] - ESPE[s]) / (MAXI[s] - ESPE[s]);
			else
			{
				
				if (POSI[s] < ESPE[s])
					POSI[s] = (ESPE[s] - POSI[s]) / (MINI[s] - ESPE[s]);
				else 
					POSI[s] = 0;
			}
		}

		
		if (!pAccuracyOnCorrelation && Compteur_ndp == 100)
			++pNDPMatrixCount;

		return true;
	}






} 
} 
} 
} 

