adios_groupsize = 4 \
                + 4 \
                + 4 \
                + 4 \
                + 4 \
                + 4 \
                + 4 \
                + 8 * (1) * (nloc_calc_moments) * (7);
adios_group_size (adios_handle, adios_groupsize, &adios_totalsize);
adios_write (adios_handle, "mgrid", &mgrid);
adios_write (adios_handle, "ntoroidal", &ntoroidal);
adios_write (adios_handle, "igrid_moments_in", &igrid_moments_in);
adios_write (adios_handle, "igrid_nover_in", &igrid_nover_in);
adios_write (adios_handle, "nloc_nover", &nloc_nover);
adios_write (adios_handle, "nloc_calc_moments", &nloc_calc_moments);
adios_write (adios_handle, "myrank_toroidal", &myrank_toroidal);
adios_write (adios_handle, "moments", momentstmp+7*(igrid_nover_in-igrid_moments_in));
