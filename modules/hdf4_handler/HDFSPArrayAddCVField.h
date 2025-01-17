/////////////////////////////////////////////////////////////////////////////
// This file is part of the hdf4 data handler for the OPeNDAP data server.
// It retrieves the missing fields for some special NASA HDF4 data products.
// The products include TRMML2_V6,TRMML3B_V6,CER_AVG,CER_ES4,CER_CDAY,CER_CGEO,CER_SRB,CER_SYN,CER_ZAVG,OBPGL2,OBPGL3
// To know more information about these products,check HDFSP.h.
// Some third-dimension coordinate variable values are not provided.
// What we do here is to provide natural number series(1,2,3, ...) for
// these missing values. It doesn't make sense to visualize or analyze
// with vertical cross-section. One can check the data level by level.

//  Authors:   MuQun Yang <myang6@hdfgroup.org>
// Copyright (c) 2010-2012 The HDF Group
/////////////////////////////////////////////////////////////////////////////

#ifndef HDFSPARRAY_ADDCVFIELD_H
#define HDFSPARRAY_ADDCVFIELD_H

#include "mfhdf.h"
#include "hdf.h"

#include <libdap/Array.h>
#include "HDFSPEnumType.h"

class HDFSPArrayAddCVField:public libdap::Array
{
    public:
        HDFSPArrayAddCVField (int32 dtype, SPType sptype, const std::string & fieldname, int tnumelm, const std::string & n = "", libdap::BaseType * v = nullptr):
            libdap::Array (n, v), 
            dtype(dtype),
            sptype(sptype),
            name(fieldname),
            tnumelm (tnumelm)
        {
        }

        ~ HDFSPArrayAddCVField () override = default;

        // Standard way of DAP handlers to pass the coordinates of the subsetted region to the handlers
        // Return the number of elements to read. 
        int format_constraint (int *cor, int *step, int *edg);

        libdap::BaseType *ptr_duplicate () override
        {
            return new HDFSPArrayAddCVField (*this);
        }

        bool read () override;

    private:


        /// Data type
        int32 dtype;

        /// Special HDF4 products we support(TRMML2_V6,TRMML3B_V6,OBPG etc.)
        SPType sptype;

        /// Field name(Different CV may have different names.)
        std::string name;
     
        int tnumelm;
        // TRMM version 7 nlayer values are from the document
        void Obtain_trmm_v7_layer(int, std::vector<int>&,std::vector<int>&);

        // TRMM version 7 nthrash values are from the document
        void Obtain_trmml3s_v7_nthrash(int, std::vector<int>&,std::vector<int>&);

};


#endif
