#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <omp.h>
#include <sys/time.h>

#if USE_MPI
#include <mpi.h>
#endif

#include "RngStream.h"
#include "bench_gtc.h"

#if USE_ADIOS
#include "adios.h"
#endif

#if USE_GOLDRUSH
#include "goldrush.h"
#endif

#if USE_GE
#include "common.h"
#include "ge.h"
#endif

double timer()
{

#if !USE_MPI
	struct timeval tp;
	gettimeofday(&tp, NULL);
	return ((double)tp.tv_sec + ((double)tp.tv_usec) * 1e-6);
#else
	//MPI_Barrier(MPI_COMM_WORLD);
	return MPI_Wtime();
#endif

}

int main(int argc, char **argv)
{

	FILE *fp;
	gtc_bench_data_t *gtc_input;
	int micell;
	int mstep, ndiag, irun;

	int mype, numberpe, ntoroidal;

	double elt, elt_main, setup_time, push_time, shift_time, charge_time,
	    poisson_time, shift_toroidal_time, shift_radial_time, sorting_time,
	    smooth_time, field_time, poisson_init_time, collision_time,
	    remap_time, moments_time, restart_time, total_time;

	setup_time = push_time = shift_time = charge_time =
	    poisson_time = poisson_init_time = smooth_time = sorting_time =
	    collision_time = remap_time = field_time = total_time =
	    shift_toroidal_time = shift_radial_time = moments_time =
	    restart_time = 0.0;

#if USE_MPI
	fprintf(stderr, "init MPI\n");
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &numberpe);
	MPI_Comm_rank(MPI_COMM_WORLD, &mype);
#else
	numberpe = 1;
	mype = 0;
#endif

#if USE_ADIOS
	fprintf(stderr, "init ADIOS\n");
	adios_init("gtcp.xml", MPI_COMM_WORLD);
#endif

#if USE_GOLDRUSH
	fprintf(stderr, "init GoldRush\n");
	gr_init(MPI_COMM_WORLD);
#endif

	if (mype == 0) {

		if (argc != 4) {
			usage(argv[0]);
#if USE_MPI
			MPI_Abort(MPI_COMM_WORLD, 2);
#else
			exit(1);
#endif
		}
	}
#if USE_MPI
	MPI_Barrier(MPI_COMM_WORLD);
#endif

	fprintf(stderr, "Finish Initialization\n");
	if (mype == 0) {
		fp = fopen(argv[1], "r");
		if (fp == NULL) {
			fprintf(stderr, "*** Cannot open input file. ***\n");
			usage(argv[0]);
#if USE_MPI
			MPI_Abort(MPI_COMM_WORLD, 1);
#else
			exit(1);
#endif
		}
		fclose(fp);
	}
#if USE_MPI
	MPI_Barrier(MPI_COMM_WORLD);
#endif

	micell = atoi(argv[2]);

	if (mype == 0) {
		if (micell <= 0) {
			fprintf(stderr,
				"*** Number of particles per cell should be a positive"
				" integer. ***\n");
			usage(argv[0]);
#if USE_MPI
			MPI_Abort(MPI_COMM_WORLD, 1);
#else
			exit(1);
#endif
		}
	}
#if USE_MPI
	MPI_Barrier(MPI_COMM_WORLD);
#endif

	ntoroidal = atoi(argv[3]);

	if (mype == 0) {
		if (ntoroidal <= 0) {
			fprintf(stderr,
				"*** Number of toroidal domains should be a positive"
				" integer. ***\n");
			usage(argv[0]);
#if USE_MPI
			MPI_Abort(MPI_COMM_WORLD, 1);
#else
			exit(1);
#endif
		}
	}
#if USE_MPI
	MPI_Barrier(MPI_COMM_WORLD);
#endif

	elt = timer();

	gtc_input = (gtc_bench_data_t *) malloc(sizeof(gtc_bench_data_t));

	(gtc_input->parallel_decomp).numberpe = numberpe;
	(gtc_input->parallel_decomp).ntoroidal = ntoroidal;
	(gtc_input->parallel_decomp).mype = mype;

	read_input_file(argv[1], &(gtc_input->global_params),
			&(gtc_input->parallel_decomp),
			&(gtc_input->radial_decomp));

	(gtc_input->global_params).micell = micell;

	setup(gtc_input);

#if USE_GE
	// make sure ge_detect_init is after getc_input, because of array size
	double threshold = 7;

	int array_size = (gtc_input->global_params.mzeta + 1)
	    * gtc_input->diagnosis_data.nloc_calc_moments
	    * gtc_input->parallel_decomp.nthreads * 7;

	//int array_size =  gtc_input->diagnosis_data.nloc_calc_moments;

	ge_detect_init(MEAN_LINEAR, array_size, 5, threshold, 1);
#endif

	/* get particle data from input files */
	irun = (gtc_input->global_params).irun;
#if RESTART
	if (irun == 1)
		restart_read(gtc_input);
#endif

	chargei_init(gtc_input);

	if (irun == 0)
		calc_moments(gtc_input);

	setup_time = timer() - elt;

	mstep = (gtc_input->global_params).mstep;
	ndiag = (gtc_input->global_params).ndiag;
#if USE_GE
	mstep = mstep - (gtc_input->global_params).mstepall;
#endif

#ifdef __bgq__
	if (mype == 0)
		print_mem_usage(&mype);
#endif

	elt_main = timer();

	for (istep = 1; istep <= mstep; istep++) {
		if (mype == 0)
			printf("** istep=%d start\n", istep);

		for (irk = 1; irk <= 2; irk++) {

			/* idiag=0, do time history diagnosis */
			idiag = ((irk + 1) % 2) + istep % ndiag;
			//if (mype==0) printf("istep=%d irk=%d\n", istep, irk);
			/* smooth potentials, diagnostics */
			elt = timer();
			//if (mype==0) printf("smooth\n");s
			smooth(3, gtc_input);
			smooth_time += timer() - elt;

			/* field */
			elt = timer();
			//if (mype==0) printf("field\n");
			field(gtc_input);
			field_time += timer() - elt;

			/* push ion */
			elt = timer();
			//if (mype==0) printf("pushi\n");
			pushi(gtc_input);
			push_time += timer() - elt;
			elt = timer();

			if (idiag == 0)
				diagnosis(gtc_input);

			/* shift in toroidal and radial dimensions */
			elt = timer();
			//if (mype==0) printf("shift_toroidal\n");
			shifti_toroidal(gtc_input);
			shift_toroidal_time += timer() - elt;

			elt = timer();
			//if (mype==0) printf("shifti_radial\n"); 
			shifti_radial(gtc_input);
			shift_radial_time += timer() - elt;

			elt = timer();
			//if (mype==0) printf("sorting\n");
			radial_bin_particles(gtc_input);
			sorting_time += timer() - elt;

			elt = timer();
			//if (mype==0) printf("collision\n");
			// Fokker Planck collision operator
			if (irk == 2 && (gtc_input->global_params).tauii > 0)
				collision(gtc_input);
			collision_time += timer() - elt;

			elt = timer();
			//if (mype==0) printf("remapping\n");
			remap(gtc_input);
			remap_time += timer() - elt;

			/* ion perturbed density */
			elt = timer();
			//if (mype==0) printf("charge\n");
			chargei(gtc_input);
			charge_time += timer() - elt;

			/* smooth ion density */
			elt = timer();
			//if (mype==0) printf("smooth\n");
			smooth(0, gtc_input);
			smooth_time += timer() - elt;

			/* solve GK Poission equation using adiabatic electron */
			elt = timer();
			//if (mype==0) printf("poisson\n");
			poisson(0, gtc_input);
			if ((istep == 1) && (irk == 1))
				poisson_init_time = timer() - elt;
			else
				poisson_time += timer() - elt;
		}

		/* write particle data to restart files */
		elt = timer();
#if USE_GE			// changed by chao
		// simulate fault injection
#if GE_INJECT
		if ((istep + 1) % 50 == 0)
			gtc_input->field_data.phi[1] = 100;
#endif
		int res = ge_detect_verify((gtc_input->diagnosis_data).moments,
					   array_size,
					   istep +
					   (gtc_input->global_params).mstepall);
#if USE_GE_RESTART
		if (res == GE_FAULT) {
#if USE_GOLDRUSH
			gr_finalize();
#endif
			adios_finalize(mype);
			ge_detect_finalize();
#if USE_MPI
			MPI_Finalize();
#endif
			exit(23);
		}
#endif
#endif

#if RESTART
		if (istep % gtc_input->global_params.restart_freq == 0)
			restart_write(gtc_input);
#endif
		restart_time += timer() - elt;

		fprintf(stderr, "%d : momentsoutput=%d\n", mype,
			gtc_input->global_params.mmomentsoutput);

		/* output moments for diagnosis */
		elt = timer();
		if ((gtc_input->global_params.mstepall +
		     istep) % gtc_input->global_params.mmomentsoutput == 0)
			calc_moments(gtc_input);
		moments_time += timer() - elt;

		if (istep >= (mstep - 1000) && istep % 10 == 0)
			output(gtc_input);
	}
	total_time = timer() - elt_main;

	total_time = total_time - poisson_init_time;

	gtc_mem_free(gtc_input);
	free(gtc_input);

	if (mype == 0) {
		fprintf(stderr, "%d time steps\n"
			"Total time:   %9.6f s\n"
			"Charge        %9.6f s (%3.4f)\n"
			"Push          %9.6f s (%3.4f)\n"
			"Shift_t       %9.6f s (%3.4f)\n"
			"Shift_r       %9.6f s (%3.4f)\n"
			"Sorting       %9.6f s (%3.4f)\n"
			"Collision     %9.6f s (%3.4f)\n"
			"Remapping     %9.6f s (%3.4f)\n"
			"Poisson       %9.6f s (%3.4f)\n"
			"Field         %9.6f s (%3.4f)\n"
			"Smooth        %9.6f s (%3.4f)\n"
			"Restart       %9.6f s (%3.4f)\n"
			"Moments       %9.6f s (%3.4f)\n"
			"Setup         %9.6f s\n"
			"Poisson Init  %9.6f s\n",
			mstep, total_time,
			charge_time, (charge_time / total_time) * 100.0,
			push_time, (push_time / total_time) * 100.0,
			shift_toroidal_time,
			(shift_toroidal_time / total_time) * 100.0,
			shift_radial_time,
			(shift_radial_time / total_time) * 100.0, sorting_time,
			(sorting_time / total_time) * 100.0, collision_time,
			(collision_time / total_time) * 100.0, remap_time,
			(remap_time / total_time) * 100.0, poisson_time,
			(poisson_time / total_time) * 100.0, field_time,
			(field_time / total_time) * 100.0, smooth_time,
			(smooth_time / total_time) * 100.0, restart_time,
			(restart_time / total_time) * 100.0, moments_time,
			(moments_time / total_time) * 100.0, setup_time,
			poisson_init_time);
	}
#if USE_GOLDRUSH
	gr_finalize();
#endif

#if USE_ADIOS
	adios_finalize(mype);
#endif

#if USE_GE			// changed by chao
	ge_detect_finalize();
#endif

#if USE_MPI
	MPI_Finalize();
#endif

	return 0;
}
