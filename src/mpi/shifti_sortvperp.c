#include <stdio.h>
#if USE_GOLDRUSH
#include "goldrush.h"
#endif
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include "bench_gtc.h"
#if USE_MPI
#include <mpi.h>
#endif
#ifdef _OPENMP
#include <omp.h>
#endif

int radial_bin_particles(gtc_bench_data_t * gtc_input)
{

	gtc_global_params_t *params;
	gtc_field_data_t *field_data;
	gtc_particle_data_t *particle_data;
	gtc_aux_particle_data_t *aux_particle_data;
	gtc_particle_decomp_t *parallel_decomp;
	gtc_radial_decomp_t *radial_decomp;

	real *z0, *z1, *z2, *z3, *z4, *z5;
	real *z00, *z01, *z02, *z03, *z04, *z05;

	params = &(gtc_input->global_params);
	field_data = &(gtc_input->field_data);
	particle_data = &(gtc_input->particle_data);
	aux_particle_data = &(gtc_input->aux_particle_data);
	parallel_decomp = &(gtc_input->parallel_decomp);
	radial_decomp = &(gtc_input->radial_decomp);

	if (((istep % params->radial_bin_freq) != 0) || (irk == 1)) {
		return 0;
	}

	z0 = particle_data->z0;
	z1 = particle_data->z1;
	z2 = particle_data->z2;
	z3 = particle_data->z3;
	z4 = particle_data->z4;
	z5 = particle_data->z5;
	z00 = particle_data->z00;
	z01 = particle_data->z01;
	z02 = particle_data->z02;
	z03 = particle_data->z03;
	z04 = particle_data->z04;
	z05 = particle_data->z05;
	int *psi_count = particle_data->psi_count;
	int *psi_offsets = particle_data->psi_offsets;

	int *ipval = aux_particle_data->kzi;
	real *ztmp = particle_data->ztmp;
	real *ztmp2 = particle_data->ztmp2;
	int mpsi = params->mpsi;
	real a0 = params->a0;
	real delr = params->delr;
	real smu_inv = params->smu_inv;
	int mpsi_loc = params->mpsi_loc;
	int mumax2 = params->mumax2;
	real delvperp = params->delvperp;
	real delvperpr = 1.0 / delvperp;

	int mstep = params->mstep;
	int mi = params->mi;
	int mi_new = mi;
	int ipsi_valid_in = radial_decomp->ipsi_valid_in;
	int ipsi_valid_out = radial_decomp->ipsi_valid_out;
	//int mpsi_loc = ipsi_valid_out-ipsi_valid_in+1;

#pragma omp parallel
	{
#if USE_GOLDRUSH
		gr_mainloop_start();
#endif
		int tid, nthreads;
		int *psi_count_l, *psi_offsets_l;
		int i, j, m;

#ifdef _OPENMP
		tid = omp_get_thread_num();
		nthreads = omp_get_num_threads();
#else
		tid = 0;
		nthreads = 1;
#endif

		//    psi_count_l   = psi_count + (mpsi+1)*tid;
		//    psi_offsets_l = psi_offsets + (mpsi+1)*tid;
		psi_count_l = psi_count + mpsi_loc * tid;
		psi_offsets_l = psi_offsets + mpsi_loc * tid;

		//    for (i=0; i<mpsi+1; i++) 
		for (i = 0; i < mpsi_loc; i++) {
			psi_count_l[i] = 0;
			psi_offsets_l[i] = 0;
		}

#pragma omp barrier

#pragma omp for
		for (m = 0; m < mi; m++) {
#if USE_GOLDRUSH
			gr_mainloop_start();
#endif
			real zetatmp = z2[m];
			if (zetatmp != HOLEVAL) {
				real psitmp = z0[m];
				real thetatmp = z1[m];
				real z5tmp = z5[m];
				real rhoi = smu_inv * z5tmp;

#if SQRT_PRECOMPUTED
				real r = psitmp;
#else
				real r = sqrt(2.0 * psitmp);
#endif
				real b = 1.0 / (1.0 + r * cos(thetatmp));

				int iptmp = (int)((r - a0) * delr + 0.5);
				int ip = abs_min_int(mpsi, iptmp);

				int ivperptmp =
				    (int)(rhoi * sqrt(b) * delvperpr);
				int ivperp = abs_min_int(mumax2 - 1, ivperptmp);

				ip = ip - ipsi_valid_in;
				assert(ip >= 0);
				assert(ip < mpsi_loc / mumax2);
				psi_count_l[ip * mumax2 + ivperp]++;
				ipval[m] = ip * mumax2 + ivperp;

			} else {
				ipval[m] = -1;
			}
#if USE_GOLDRUSH
			gr_mainloop_end();
#endif
		}

		if (tid == 0) {
			//for (i=0; i<mpsi; i++) 
			for (i = 0; i < mpsi_loc - 1; i++) {
				int full_bin_count = 0;
				for (j = 0; j < nthreads; j++) {
					//full_bin_count += psi_count[j*(mpsi+1)+i];
					full_bin_count +=
					    psi_count[j * mpsi_loc + i];
				}
				psi_offsets[i + 1] =
				    psi_offsets[i] + full_bin_count;
			}

			//for (i=0; i<mpsi+1; i++) 
			for (i = 0; i < mpsi_loc; i++) {
				for (j = 1; j < nthreads; j++) {
					//psi_offsets[j*(mpsi+1)+i] = psi_offsets[(j-1)*(mpsi+1)+i] + psi_count[(j-1)*(mpsi+1)+i];
					psi_offsets[j * mpsi_loc + i] =
					    psi_offsets[(j - 1) * mpsi_loc +
							i] + psi_count[(j -
									1) *
								       mpsi_loc
								       + i];
				}
			}
			/*
			   if (psi_offsets[(nthreads-1)*(mpsi+1)+mpsi]+
			   psi_count[(nthreads-1)*(mpsi+1)+mpsi] !=
			   (mi-(params->holecount))) {
			   fprintf(stderr, "psi offset sum %d, mi %d\n",
			   psi_offsets[(nthreads-1)*(mpsi+1)+mpsi]+psi_count[(nthreads-1)*(mpsi+1)+mpsi], mi);
			   exit(1);
			   }
			 */
			if (psi_offsets
			    [(nthreads - 1) * mpsi_loc + mpsi_loc - 1] +
			    psi_count[(nthreads - 1) * mpsi_loc + mpsi_loc -
				      1] != (mi - (params->holecount))) {
				fprintf(stderr, "psi offset sum %d, mi %d\n",
					psi_offsets[(nthreads - 1) * mpsi_loc +
						    mpsi_loc - 1] +
					psi_count[(nthreads - 1) * mpsi_loc +
						  mpsi_loc - 1], mi);
				exit(1);
			}
			mi_new = mi - params->holecount;
			params->mi = mi_new;
			params->holecount = 0;
		}
#pragma omp barrier

		//for (i=0; i<mpsi+1; i++) 
		for (i = 0; i < mpsi_loc; i++) {
			psi_count_l[i] = 0;
		}

#pragma omp barrier

#pragma omp for
		for (m = 0; m < mi; m++) {
#if USE_GOLDRUSH
			gr_mainloop_start();
#endif

			int ip = ipval[m];

			if (ip != -1) {
				//assert(ip>=0);
				//assert(ip<mpsi_loc);
				int new_pos =
				    psi_offsets_l[ip] + psi_count_l[ip]++;
				z00[new_pos] = z0[m];
				z01[new_pos] = z1[m];
				z02[new_pos] = z2[m];
				z03[new_pos] = z3[m];
				z04[new_pos] = z4[m];
				ztmp[new_pos] = z5[m];
				ztmp2[new_pos] = z05[m];
			}
#if USE_GOLDRUSH
			gr_mainloop_end();
#endif
		}

#pragma omp for
		for (m = 0; m < mi_new; m++) {
#if USE_GOLDRUSH
			gr_mainloop_start();
#endif
			z0[m] = z00[m];
			z1[m] = z01[m];
			z2[m] = z02[m];
			z3[m] = z03[m];
			z4[m] = z04[m];
#if USE_GOLDRUSH
			gr_mainloop_end();
#endif
		}
#if USE_GOLDRUSH
		gr_mainloop_end();
#endif

	}			/* end of parallel region */

	real *tmp_ptr, *tmp_ptr2;
	/* swap ztmp and z5 */
	tmp_ptr = particle_data->z5;
	particle_data->z5 = particle_data->ztmp;
	particle_data->ztmp = tmp_ptr;

	/* swap ztmp2 and z05 */
	tmp_ptr2 = particle_data->z05;
	particle_data->z05 = particle_data->ztmp2;
	particle_data->ztmp2 = tmp_ptr2;

	return 0;
}

int shifti_toroidal(gtc_bench_data_t * gtc_input)
{
#if USE_MPI
	gtc_global_params_t *params;
	gtc_particle_data_t *particle_data;
	gtc_aux_particle_data_t *aux_particle_data;
	gtc_particle_decomp_t *parallel_decomp;

	int mi, mimax;
	int ntoroidal;
	real zetamax, zetamin;

	int mi_end;

	real *z0, *z1, *z2, *z3, *z4, *z5;
	real *z00, *z01, *z02, *z03, *z04, *z05, *ztmp;
	int *kzi;

	int *mshift;

	int nparam;
	real pi, pi2, pi_inv;

	int m0, iteration;
	int max_threads;

	int mstep;

    /*******/

	params = &(gtc_input->global_params);
	particle_data = &(gtc_input->particle_data);
	aux_particle_data = &(gtc_input->aux_particle_data);
	parallel_decomp = &(gtc_input->parallel_decomp);

	mi = params->mi;
	mimax = params->mimax;
	pi = params->pi;
	zetamax = params->zetamax;
	zetamin = params->zetamin;
	mstep = params->mstep;

	z0 = particle_data->z0;
	z1 = particle_data->z1;
	z2 = particle_data->z2;
	z3 = particle_data->z3;
	z4 = particle_data->z4;
	z5 = particle_data->z5;

	z00 = particle_data->z00;
	z01 = particle_data->z01;
	z02 = particle_data->z02;
	z03 = particle_data->z03;
	z04 = particle_data->z04;
	z05 = particle_data->z05;
	ztmp = particle_data->ztmp;

	kzi = aux_particle_data->kzi;

	ntoroidal = parallel_decomp->ntoroidal;

	nparam = 12;
	pi_inv = 1.0 / pi;
	pi2 = 2.0 * pi;

    /********/

	if (ntoroidal == 1)
		return 0;

	m0 = 0;
	mi_end = mi;
	iteration = 0;

	max_threads = 64;
	mshift = (int *)_mm_malloc(max_threads * 8 * sizeof(int),
				   IDEAL_ALIGNMENT);
	assert(mshift != NULL);

	while (iteration <= ntoroidal) {
		{

			real *sendright, *sendleft;
			real *recvleft, *recvright;
			int msend, mrecv, msendright, msendleft, pos;
			int mrecvleft, mrecvright;
			int isendtag, irecvtag;
			MPI_Status istatus1, istatus2;

			msend = msendright = msendleft = 0;

#pragma omp parallel
			{
#if USE_GOLDRUSH
				gr_mainloop_start();
#endif
				int mlstack_max, mrstack_max;
				int *lstack, *rstack, *stack_tmp;
				int lpos, rpos;

				int i;
				int tid, nthreads;
				real zetaright, zetaleft;
				real z2val;

#ifdef _OPENMP
				tid = omp_get_thread_num();
				nthreads = omp_get_num_threads();
#else
				tid = 0;
				nthreads = 1;
#endif

				assert(nthreads == parallel_decomp->nthreads);
				assert(nthreads <= max_threads);

				mshift[8 * tid] = mshift[8 * tid + 1]
				    = mshift[8 * tid + 2] = mshift[8 * tid + 3]
				    = mshift[8 * tid + 4] = 0;

				mlstack_max = mrstack_max = 32768;
				lstack =
				    (int *)malloc(mlstack_max * sizeof(int));
				rstack =
				    (int *)malloc(mrstack_max * sizeof(int));
				assert(lstack != NULL);
				assert(rstack != NULL);

#pragma omp barrier

#pragma omp for
				for (i = m0; i < mi_end; i++) {
#if USE_GOLDRUSH
					gr_mainloop_start();
#endif

					z2val = z2[i];
					if (z2val == HOLEVAL) {
						continue;
					}

					zetaright = min(pi2, z2val) - zetamax;
					zetaleft = z2val - zetamin;

					if (zetaright * zetaleft > 0.0) {

						zetaright =
						    zetaright * 0.5 * pi_inv;
						zetaright =
						    zetaright -
						    floor(zetaright);

						mshift[8 * tid]++;

						/* particle to move right */
						if (zetaright < 0.5) {

							rpos =
							    mshift[8 * tid +
								   1]++;
							if (rpos == mrstack_max) {
								//                      printf("mype=%d mrstack_max=%d\n",parallel_decomp->mype,mrstack_max);
								/* double the size */
								stack_tmp =
								    (int *)
								    malloc(2 *
									   mrstack_max
									   *
									   sizeof
									   (int));
								assert(stack_tmp
								       != NULL);
								memcpy
								    (stack_tmp,
								     rstack,
								     mrstack_max
								     *
								     sizeof
								     (int));
								free(rstack);
								rstack =
								    stack_tmp;
								mrstack_max =
								    2 *
								    mrstack_max;
							}
							rstack[rpos] = i;

							/* particle to move left */
						} else {

							lpos =
							    mshift[8 * tid +
								   2]++;
							if (lpos == mlstack_max) {
								//printf("mype=%d mlstack_max=%d\n",parallel_decomp->mype,mlstack_max);
								/* double the size */
								stack_tmp =
								    (int *)
								    malloc(2 *
									   mlstack_max
									   *
									   sizeof
									   (int));
								assert(stack_tmp
								       != NULL);
								memcpy
								    (stack_tmp,
								     lstack,
								     mlstack_max
								     *
								     sizeof
								     (int));
								free(lstack);
								lstack =
								    stack_tmp;
								mlstack_max =
								    2 *
								    mlstack_max;
							}
							lstack[lpos] = i;
						}

					}
#if USE_GOLDRUSH
					gr_mainloop_end();
#endif
				}

				/* Merge partial arrays */
				if (tid == 0) {
					mshift[8 * 0 + 3] = 0;
					mshift[8 * 0 + 4] = 0;
					for (i = 1; i < nthreads; i++) {
						mshift[8 * i + 3] =
						    mshift[8 * (i - 1) + 3] +
						    mshift[8 * (i - 1) + 1];
						mshift[8 * i + 4] =
						    mshift[8 * (i - 1) + 4] +
						    mshift[8 * (i - 1) + 2];
					}
					msendright =
					    mshift[8 * (nthreads - 1) + 3] +
					    mshift[8 * (nthreads - 1) + 1];
					msendleft =
					    mshift[8 * (nthreads - 1) + 4] +
					    mshift[8 * (nthreads - 1) + 2];
					msend = msendright + msendleft;

					if (msend >= mi) {
						fprintf(stderr,
							"Error! mype %d, msend %d, left %d, right"
							" %d, mi %d\n",
							parallel_decomp->mype,
							msend, msendleft,
							msendright, mi);
						exit(1);
					}
				}
#pragma omp barrier

				memcpy(kzi + mshift[8 * tid + 3], rstack,
				       mshift[8 * tid + 1] * sizeof(int));
				memcpy(kzi + msendright + mshift[8 * tid + 4],
				       lstack,
				       mshift[8 * tid + 2] * sizeof(int));
				free(lstack);
				free(rstack);
#if USE_GOLDRUSH
				gr_mainloop_end();
#endif
			}

			iteration++;
			if (iteration > 1) {
				mrecv = 0;
				MPI_Allreduce(&msend, &mrecv, 1, MPI_INT,
					      MPI_SUM, MPI_COMM_WORLD);
				if (mrecv == 0) {
					break;
				}
				/*
				   if (parallel_decomp->mype == 0) {
				   fprintf(stderr, "iteration %d, mrecv %d\n", iteration,
				   mrecv);
				   }
				 */
			}
			/*
			   //MPI_Barrier(MPI_COMM_WORLD);
			   if (parallel_decomp->mype==0)
			   printf("torodial msendleft=%d msendright=%d\n", msendleft, msendright);
			 */

			/* allocate mem for incoming particle data */
			if (msendleft + msendright >=
			    parallel_decomp->sendbuf_size) {
				fprintf(stderr,
					"Toroidal Error! PE %d, msendleft %d, msendright %d, "
					"sendbuf_size %d\n",
					parallel_decomp->mype, msendleft,
					msendright,
					parallel_decomp->sendbuf_size);
				exit(1);
			} else {
				sendleft = parallel_decomp->sendbuf;
				sendright =
				    parallel_decomp->sendbuf +
				    nparam * msendleft;
			}

#if 0
			/* Allocate space for sendright and sendleft */
			sendright =
			    (real *) malloc(nparam * msendright * sizeof(real));
			assert(sendright != NULL);

			sendleft =
			    (real *) malloc(nparam * msendleft * sizeof(real));
			assert(sendleft != NULL);
#endif

			/* pack particle data */
#pragma omp parallel for private(pos)
			for (int i = 0; i < msendright; i++) {
#if USE_GOLDRUSH
				gr_mainloop_start();
#endif
				pos = kzi[i];
				sendright[nparam * i + 0] = z0[pos];
				sendright[nparam * i + 1] = z1[pos];
				sendright[nparam * i + 2] = z2[pos];
				z2[pos] = HOLEVAL;
				sendright[nparam * i + 3] = z3[pos];
				sendright[nparam * i + 4] = z4[pos];
				sendright[nparam * i + 5] = z5[pos];
				sendright[nparam * i + 6] = z00[pos];
				sendright[nparam * i + 7] = z01[pos];
				sendright[nparam * i + 8] = z02[pos];
				z02[pos] = HOLEVAL;
				sendright[nparam * i + 9] = z03[pos];
				sendright[nparam * i + 10] = z04[pos];
				sendright[nparam * i + 11] = z05[pos];
#if USE_GOLDRUSH
				gr_mainloop_end();
#endif
			}

#pragma omp parallel for private(pos)
			for (int i = 0; i < msendleft; i++) {
#if USE_GOLDRUSH
				gr_mainloop_start();
#endif
				pos = kzi[i + msendright];
				sendleft[nparam * i + 0] = z0[pos];
				sendleft[nparam * i + 1] = z1[pos];
				sendleft[nparam * i + 2] = z2[pos];
				z2[pos] = HOLEVAL;
				sendleft[nparam * i + 3] = z3[pos];
				sendleft[nparam * i + 4] = z4[pos];
				sendleft[nparam * i + 5] = z5[pos];
				sendleft[nparam * i + 6] = z00[pos];
				sendleft[nparam * i + 7] = z01[pos];
				sendleft[nparam * i + 8] = z02[pos];
				z02[pos] = HOLEVAL;
				sendleft[nparam * i + 9] = z03[pos];
				sendleft[nparam * i + 10] = z04[pos];
				sendleft[nparam * i + 11] = z05[pos];
#if USE_GOLDRUSH
				gr_mainloop_end();
#endif
			}

			/* send # of particles to move right, get # to recv from left */
			isendtag = parallel_decomp->myrank_toroidal;
			irecvtag = parallel_decomp->left_pe;
			mrecvleft = 0;
			MPI_Sendrecv(&msendright, 1, MPI_INT,
				     parallel_decomp->right_pe, isendtag,
				     &mrecvleft, 1, MPI_INT,
				     parallel_decomp->left_pe, irecvtag,
				     parallel_decomp->toroidal_comm, &istatus1);

			/* send # of particles to move left, get # to recv from right */
			irecvtag = parallel_decomp->right_pe;
			mrecvright = 0;
			MPI_Sendrecv(&msendleft, 1, MPI_INT,
				     parallel_decomp->left_pe, isendtag,
				     &mrecvright, 1, MPI_INT,
				     parallel_decomp->right_pe, irecvtag,
				     parallel_decomp->toroidal_comm, &istatus2);

			/* allocate mem for incoming particle data */
			if (mrecvleft + mrecvright >=
			    parallel_decomp->recvbuf_size) {
				fprintf(stderr,
					"Toroidal Error! PE %d, mrecvleft %d, mrecvright %d, "
					"recvbuf_size %d\n",
					parallel_decomp->mype, mrecvleft,
					mrecvright,
					parallel_decomp->recvbuf_size);
				exit(1);
			}
			recvleft = parallel_decomp->recvbuf;
			recvright =
			    parallel_decomp->recvbuf + nparam * mrecvleft;

#if 0
			/* Allocate space for sendright and sendleft */
			recvright =
			    (real *) malloc(nparam * mrecvright * sizeof(real));
			assert(recvright != NULL);

			recvleft =
			    (real *) malloc(nparam * mrecvleft * sizeof(real));
			assert(recvleft != NULL);
#endif

			/* send particles to right neighbor, recv from left */
			irecvtag = parallel_decomp->left_pe;
			MPI_Sendrecv(sendright, msendright * nparam, MPI_MYREAL,
				     parallel_decomp->right_pe,
				     isendtag, recvleft, mrecvleft * nparam,
				     MPI_MYREAL, parallel_decomp->left_pe,
				     irecvtag, parallel_decomp->toroidal_comm,
				     &istatus1);

			/* send particles to left neighbor, recv from right */
			irecvtag = parallel_decomp->right_pe;
			MPI_Sendrecv(sendleft, msendleft * nparam, MPI_MYREAL,
				     parallel_decomp->left_pe,
				     isendtag, recvright, mrecvright * nparam,
				     MPI_MYREAL, parallel_decomp->right_pe,
				     irecvtag, parallel_decomp->toroidal_comm,
				     &istatus2);

			/* copy received data to particle arrays */
			assert(mi_end + mrecvleft + mrecvright < mimax);

			//if (parallel_decomp->mype==0)
			//  printf("toroidal mype=%d mrecvleft=%d mrecvright=%d\n", parallel_decomp->mype, mrecvleft, mrecvright);

#pragma omp parallel for private(pos)
			for (int i = 0; i < mrecvleft; i++) {
#if USE_GOLDRUSH
				gr_mainloop_start();
#endif
				pos = mi_end + i;
				z0[pos] = recvleft[nparam * i + 0];
				z1[pos] = recvleft[nparam * i + 1];
				z2[pos] = recvleft[nparam * i + 2];
				z3[pos] = recvleft[nparam * i + 3];
				z4[pos] = recvleft[nparam * i + 4];
				z5[pos] = recvleft[nparam * i + 5];
				z00[pos] = recvleft[nparam * i + 6];
				z01[pos] = recvleft[nparam * i + 7];
				z02[pos] = recvleft[nparam * i + 8];
				z03[pos] = recvleft[nparam * i + 9];
				z04[pos] = recvleft[nparam * i + 10];
				z05[pos] = recvleft[nparam * i + 11];
#if USE_GOLDRUSH
				gr_mainloop_end();
#endif
			}

#pragma omp parallel for private(pos)
			for (int i = 0; i < mrecvright; i++) {
#if USE_GOLDRUSH
				gr_mainloop_start();
#endif
				pos = mi_end + mrecvleft + i;
				z0[pos] = recvright[nparam * i + 0];
				z1[pos] = recvright[nparam * i + 1];
				z2[pos] = recvright[nparam * i + 2];
				z3[pos] = recvright[nparam * i + 3];
				z4[pos] = recvright[nparam * i + 4];
				z5[pos] = recvright[nparam * i + 5];
				z00[pos] = recvright[nparam * i + 6];
				z01[pos] = recvright[nparam * i + 7];
				z02[pos] = recvright[nparam * i + 8];
				z03[pos] = recvright[nparam * i + 9];
				z04[pos] = recvright[nparam * i + 10];
				z05[pos] = recvright[nparam * i + 11];
#if USE_GOLDRUSH
				gr_mainloop_end();
#endif
			}

			/* update m0, m_end, and other global counts */
			m0 = mi_end;
			params->mi += mrecvleft + mrecvright;
			mi_end = params->mi;
			params->holecount += msend;

#if 0
			free(sendleft);
			free(sendright);
			free(recvleft);
			free(recvright);
#endif

		}		/* while section */

	}

	if (iteration > (ntoroidal + 1)) {
		fprintf(stderr,
			"Error! Endless particle sorting loop at PE %d\n",
			parallel_decomp->mype);
		exit(1);
	}

	params->mi = mi_end;

/* periodically fill in holes */
	//if ((istep == 1) || (irk == 1)) {
	//    _mm_free(mshift);
	//    return 0;
	//}

	_mm_free(mshift);

#endif

/* radial_bin_particles move to shifti_radial */
//    radial_bin_particles(gtc_input);

	return 0;
}

int shifti_radial(gtc_bench_data_t * gtc_input)
{
#if USE_MPI
	gtc_global_params_t *params;
	gtc_particle_data_t *particle_data;
	gtc_aux_particle_data_t *aux_particle_data;
	gtc_particle_decomp_t *parallel_decomp;
	gtc_radial_decomp_t *radial_decomp;

	int mi, mimax;
	int nradial_dom, myrank_radiald;
	real a_nover_in, a_nover_out;

	int mi_end;
	real *z0, *z1, *z2, *z3, *z4, *z5;
	real *z00, *z01, *z02, *z03, *z04, *z05, *ztmp;
	int *kzi;

	int *mshift;

	int nparam;
	real pi, pi2, pi_inv;

	int m0, iteration;
	int max_threads;

  /************/

	params = &(gtc_input->global_params);
	particle_data = &(gtc_input->particle_data);
	aux_particle_data = &(gtc_input->aux_particle_data);
	parallel_decomp = &(gtc_input->parallel_decomp);
	radial_decomp = &(gtc_input->radial_decomp);

	mi = params->mi;
	mimax = params->mimax;
	pi = params->pi;

	z0 = particle_data->z0;
	z1 = particle_data->z1;
	z2 = particle_data->z2;
	z3 = particle_data->z3;
	z4 = particle_data->z4;
	z5 = particle_data->z5;

	z00 = particle_data->z00;
	z01 = particle_data->z01;
	z02 = particle_data->z02;
	z03 = particle_data->z03;
	z04 = particle_data->z04;
	z05 = particle_data->z05;
	ztmp = particle_data->ztmp;

	kzi = aux_particle_data->kzi;

	nradial_dom = radial_decomp->nradial_dom;
	myrank_radiald = radial_decomp->myrank_radiald;

	a_nover_in = radial_decomp->a_nover_in;
	a_nover_out = radial_decomp->a_nover_out;

	nparam = 12;
	pi_inv = 1.0 / pi;
	pi2 = 2.0 * pi;

	if (nradial_dom == 1)
		return 0;

	m0 = 0;
	mi_end = mi;
	iteration = 0;

	max_threads = 64;
	mshift =
	    (int *)_mm_malloc(max_threads * 8 * sizeof(int), IDEAL_ALIGNMENT);
	assert(mshift != NULL);

	while (iteration <= nradial_dom) {
		{
			real *sendright, *sendleft;
			real *recvleft, *recvright;
			int msend, mrecv, msendright, msendleft, pos;
			int mrecvleft, mrecvright;
			int isendtag, irecvtag;
			MPI_Status istatus1, istatus2;

			msend = msendright = msendleft = 0;

#pragma omp parallel
			{
#if USE_GOLDRUSH
				gr_mainloop_start();
#endif
				int mlstack_max, mrstack_max;
				int *lstack, *rstack, *stack_tmp;
				int lpos, rpos;

				int i;
				int tid, nthreads;
				real radialright, radialleft;
				real z2val, psitmp, r;

#ifdef _OPENMP
				tid = omp_get_thread_num();
				nthreads = omp_get_num_threads();
#else
				tid = 0;
				nthreads = 1;
#endif

				assert(nthreads == parallel_decomp->nthreads);
				assert(nthreads <= max_threads);

				mshift[8 * tid] = mshift[8 * tid + 1]
				    = mshift[8 * tid + 2] = mshift[8 * tid + 3]
				    = mshift[8 * tid + 4] = 0;

				mlstack_max = mrstack_max = 8192;
				lstack =
				    (int *)malloc(mlstack_max * sizeof(int));
				rstack =
				    (int *)malloc(mrstack_max * sizeof(int));
				assert(lstack != NULL);
				assert(rstack != NULL);

#pragma omp barrier

#pragma omp for
				for (i = m0; i < mi_end; i++) {
#if USE_GOLDRUSH
					gr_mainloop_start();
#endif

					z2val = z2[i];
					if (z2val == HOLEVAL) {
						continue;
					}

					psitmp = z0[i];

#if SQRT_PRECOMPUTED
					r = psitmp;
#else
					r = sqrt(2.0 * psitmp);
#endif
					if ((r < a_nover_in
					     && myrank_radiald > 0)
					    || (r > a_nover_out
						&& myrank_radiald <
						(nradial_dom - 1))) {

						mshift[8 * tid]++;

						/* particle to move right */
						if (r > a_nover_out) {
							rpos =
							    mshift[8 * tid +
								   1]++;
							if (rpos == mrstack_max) {
								/* double the size */
								//printf("radial mype=%d mrstack_max=%d\n",parallel_decomp->mype,mrstack_max);
								stack_tmp =
								    (int *)
								    malloc(2 *
									   mrstack_max
									   *
									   sizeof
									   (int));
								assert(stack_tmp
								       != NULL);
								memcpy
								    (stack_tmp,
								     rstack,
								     mrstack_max
								     *
								     sizeof
								     (int));
								free(rstack);
								rstack =
								    stack_tmp;
								mrstack_max =
								    2 *
								    mrstack_max;
							}
							rstack[rpos] = i;
						} else {	/* particle to move left */
							lpos =
							    mshift[8 * tid +
								   2]++;
							if (lpos == mrstack_max) {
								//printf("radial mype=%d mlstack_max=%d\n",parallel_decomp->mype,mlstack_max);
								stack_tmp =
								    (int *)
								    malloc(2 *
									   mlstack_max
									   *
									   sizeof
									   (int));
								assert(stack_tmp
								       != NULL);
								memcpy
								    (stack_tmp,
								     lstack,
								     mlstack_max
								     *
								     sizeof
								     (int));
								free(lstack);
								lstack =
								    stack_tmp;
								mlstack_max =
								    2 *
								    mlstack_max;
							}
							lstack[lpos] = i;
						}

					}
#if USE_GOLDRUSH
					gr_mainloop_end();
#endif
				}	// end i=m0, mi_end loop

				/* Merge partial arrays */
				if (tid == 0) {
					mshift[8 * 0 + 3] = 0;
					mshift[8 * 0 + 4] = 0;
					for (i = 1; i < nthreads; i++) {
						mshift[8 * i + 3] =
						    mshift[8 * (i - 1) + 3] +
						    mshift[8 * (i - 1) + 1];
						mshift[8 * i + 4] =
						    mshift[8 * (i - 1) + 4] +
						    mshift[8 * (i - 1) + 2];
					}
					msendright =
					    mshift[8 * (nthreads - 1) + 3] +
					    mshift[8 * (nthreads - 1) + 1];
					msendleft =
					    mshift[8 * (nthreads - 1) + 4] +
					    mshift[8 * (nthreads - 1) + 2];
					msend = msendright + msendleft;
					if (msend >= mi) {
						fprintf(stderr,
							"Error! mype %d, msend %d, left %d, right"
							" %d, mi %d\n",
							parallel_decomp->mype,
							msend, msendleft,
							msendright, mi);
						exit(1);
					}
				}
#pragma omp barrier

				memcpy(kzi + mshift[8 * tid + 3], rstack,
				       mshift[8 * tid + 1] * sizeof(int));
				memcpy(kzi + msendright + mshift[8 * tid + 4],
				       lstack,
				       mshift[8 * tid + 2] * sizeof(int));
				free(lstack);
				free(rstack);
#if USE_GOLDRUSH
				gr_mainloop_end();
#endif
			}	// end parallel 

			iteration++;
			if (iteration > 1) {
				mrecv = 0;
				MPI_Allreduce(&msend, &mrecv, 1, MPI_INT,
					      MPI_SUM, MPI_COMM_WORLD);
				if (mrecv == 0) {
					break;
				}
			}
			/*
			   if (parallel_decomp->mype==1)
			   printf("radial iteration=%d msendleft=%d msendright=%d\n",iteration, msendleft, msendright);
			 */

			/* allocate mem for incoming particle data */
			if (msendleft + msendright >=
			    parallel_decomp->sendbuf_size) {
				fprintf(stderr,
					"Radial Error! PE %d, msendleft %d, msendright %d, "
					"sendbuf_size %d\n",
					parallel_decomp->mype, msendleft,
					msendright,
					parallel_decomp->sendbuf_size);
				exit(1);
			} else {
				sendleft = parallel_decomp->sendbuf;
				sendright =
				    parallel_decomp->sendbuf +
				    nparam * msendleft;
			}

			/* pack particle data */
#pragma omp parallel for private(pos)
			for (int i = 0; i < msendright; i++) {
#if USE_GOLDRUSH
				gr_mainloop_start();
#endif
				pos = kzi[i];
				sendright[nparam * i + 0] = z0[pos];
				sendright[nparam * i + 1] = z1[pos];
				sendright[nparam * i + 2] = z2[pos];
				z2[pos] = HOLEVAL;
				sendright[nparam * i + 3] = z3[pos];
				sendright[nparam * i + 4] = z4[pos];
				sendright[nparam * i + 5] = z5[pos];
				sendright[nparam * i + 6] = z00[pos];
				sendright[nparam * i + 7] = z01[pos];
				sendright[nparam * i + 8] = z02[pos];
				z02[pos] = HOLEVAL;
				sendright[nparam * i + 9] = z03[pos];
				sendright[nparam * i + 10] = z04[pos];
				sendright[nparam * i + 11] = z05[pos];
#if USE_GOLDRUSH
				gr_mainloop_end();
#endif
			}

#pragma omp parallel for private(pos)
			for (int i = 0; i < msendleft; i++) {
#if USE_GOLDRUSH
				gr_mainloop_start();
#endif
				pos = kzi[i + msendright];
				sendleft[nparam * i + 0] = z0[pos];
				sendleft[nparam * i + 1] = z1[pos];
				sendleft[nparam * i + 2] = z2[pos];
				z2[pos] = HOLEVAL;
				sendleft[nparam * i + 3] = z3[pos];
				sendleft[nparam * i + 4] = z4[pos];
				sendleft[nparam * i + 5] = z5[pos];
				sendleft[nparam * i + 6] = z00[pos];
				sendleft[nparam * i + 7] = z01[pos];
				sendleft[nparam * i + 8] = z02[pos];
				z02[pos] = HOLEVAL;
				sendleft[nparam * i + 9] = z03[pos];
				sendleft[nparam * i + 10] = z04[pos];
				sendleft[nparam * i + 11] = z05[pos];
#if USE_GOLDRUSH
				gr_mainloop_end();
#endif
			}

			/* send # of particles to move right, get # to recv from left */
			isendtag = parallel_decomp->myrank_partd;
			irecvtag = radial_decomp->left_radial_pe;
			mrecvleft = 0;
			MPI_Sendrecv(&msendright, 1, MPI_INT,
				     radial_decomp->right_radial_pe, isendtag,
				     &mrecvleft, 1, MPI_INT,
				     radial_decomp->left_radial_pe, irecvtag,
				     parallel_decomp->partd_comm, &istatus1);

			/* send # of particles to move left, get # to recv from right */
			irecvtag = radial_decomp->right_radial_pe;
			mrecvright = 0;
			MPI_Sendrecv(&msendleft, 1, MPI_INT,
				     radial_decomp->left_radial_pe, isendtag,
				     &mrecvright, 1, MPI_INT,
				     radial_decomp->right_radial_pe, irecvtag,
				     parallel_decomp->partd_comm, &istatus2);

			/* allocate mem for incoming particle data */
			if (mrecvleft + mrecvright >=
			    parallel_decomp->recvbuf_size) {
				fprintf(stderr,
					"Radial Error! PE %d, mrecvleft %d, mrecvright %d, "
					"recvbuf_size %d\n",
					parallel_decomp->mype, mrecvleft,
					mrecvright,
					parallel_decomp->recvbuf_size);
				exit(1);
			}
			recvleft = parallel_decomp->recvbuf;
			recvright =
			    parallel_decomp->recvbuf + nparam * mrecvleft;
			/*
			   MPI_Barrier(MPI_COMM_WORLD);
			   if (parallel_decomp->mype==0)
			   printf("radial mype=%d msendleft=%d msendright=%d\n", parallel_decomp->mype, msendleft, msendright);           
			 */

			/* send particles to right neighbor, recv from left */
			irecvtag = radial_decomp->left_radial_pe;
			MPI_Sendrecv(sendright, msendright * nparam, MPI_MYREAL,
				     radial_decomp->right_radial_pe,
				     isendtag, recvleft, mrecvleft * nparam,
				     MPI_MYREAL, radial_decomp->left_radial_pe,
				     irecvtag, parallel_decomp->partd_comm,
				     &istatus1);
			/* send particles to left neighbor, recv from right */
			irecvtag = radial_decomp->right_radial_pe;
			MPI_Sendrecv(sendleft, msendleft * nparam, MPI_MYREAL,
				     radial_decomp->left_radial_pe,
				     isendtag, recvright, mrecvright * nparam,
				     MPI_MYREAL, radial_decomp->right_radial_pe,
				     irecvtag, parallel_decomp->partd_comm,
				     &istatus2);

			/* copy received data to particle arrays */
			assert(mi_end + mrecvleft + mrecvright < mimax);
			//if (parallel_decomp->mype==0)
			//printf("radial mype=%d mrecvleft=%d mrecvright=%d\n", parallel_decomp->mype, mrecvleft, mrecvright);

#pragma omp parallel for private(pos)
			for (int i = 0; i < mrecvleft; i++) {
#if USE_GOLDRUSH
				gr_mainloop_start();
#endif
				pos = mi_end + i;
				z0[pos] = recvleft[nparam * i + 0];
				z1[pos] = recvleft[nparam * i + 1];
				z2[pos] = recvleft[nparam * i + 2];
				z3[pos] = recvleft[nparam * i + 3];
				z4[pos] = recvleft[nparam * i + 4];
				z5[pos] = recvleft[nparam * i + 5];
				z00[pos] = recvleft[nparam * i + 6];
				z01[pos] = recvleft[nparam * i + 7];
				z02[pos] = recvleft[nparam * i + 8];
				z03[pos] = recvleft[nparam * i + 9];
				z04[pos] = recvleft[nparam * i + 10];
				z05[pos] = recvleft[nparam * i + 11];
#if USE_GOLDRUSH
				gr_mainloop_end();
#endif
			}

#pragma omp parallel for private(pos)
			for (int i = 0; i < mrecvright; i++) {
#if USE_GOLDRUSH
				gr_mainloop_start();
#endif
				pos = mi_end + mrecvleft + i;
				z0[pos] = recvright[nparam * i + 0];
				z1[pos] = recvright[nparam * i + 1];
				z2[pos] = recvright[nparam * i + 2];
				z3[pos] = recvright[nparam * i + 3];
				z4[pos] = recvright[nparam * i + 4];
				z5[pos] = recvright[nparam * i + 5];
				z00[pos] = recvright[nparam * i + 6];
				z01[pos] = recvright[nparam * i + 7];
				z02[pos] = recvright[nparam * i + 8];
				z03[pos] = recvright[nparam * i + 9];
				z04[pos] = recvright[nparam * i + 10];
				z05[pos] = recvright[nparam * i + 11];
#if USE_GOLDRUSH
				gr_mainloop_end();
#endif
			}

			/* update m0, m_end, and other global counts */
			m0 = mi_end;
			params->mi += mrecvleft + mrecvright;
			mi_end = params->mi;
			params->holecount += msend;

		}		// end while section

	}			// end while

	if (iteration > (nradial_dom + 1)) {
		fprintf(stderr,
			"Error! Endless particle sorting loop at PE %d\n",
			parallel_decomp->mype);
		exit(1);
	}

	params->mi = mi_end;

	/* periodically fill in holes */
	/*
	   if ((istep == 1) || (irk == 1)) {
	   _mm_free(mshift);
	   return 0;
	   } 
	 */

	_mm_free(mshift);

#endif

	return 0;

}
