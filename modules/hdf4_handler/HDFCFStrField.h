/////////////////////////////////////////////////////////////////////////////
// This file is part of the hdf4 data handler for the OPeNDAP data server.
// It retrieves the HDF4 DFNT_CHAR >1D array and then send to DAP as a DAP string array for the CF option.
//  Authors:   MuQun Yang <myang6@hdfgroup.org>  Eunsoo Seo
// Copyright (c) 2010-2012 The HDF Group
/////////////////////////////////////////////////////////////////////////////

#ifndef HDFCFSTR_FIELD_H
#define HDFCFSTR_FIELD_H

#include <libdap/Array.h>
#include "HDFCFUtil.h"


class HDFCFStrField:public libdap::Array
{
    public:
    HDFCFStrField (int rank, 
                   const std::string & filename, 
                   bool is_vdata, 
                   const int h4fd, 
                   int32 fieldref,  
                   int32 fieldorder, 
                   const std::string & fieldname,  
                   const std::string & n = "", 
                   libdap::BaseType * v = nullptr):
        Array (n, v),
        rank (rank),
        filename(filename),
        is_vdata(is_vdata), 
        h4fd(h4fd),
        fieldref(fieldref),
        fieldorder(fieldorder),
        fieldname(fieldname)
        {
        }

        ~ HDFCFStrField () override = default;

        // Standard way to pass the coordinates of the subsetted region from the client to the handlers
        int format_constraint (int *cor, int *step, int *edg);

        BaseType *ptr_duplicate () override
        {
            return new HDFCFStrField (*this);
        }

        // Read the data.
        bool read () override;

    private:

        // Field array rank
        int rank;

        // file name
        std::string filename;

        bool is_vdata;

        int h4fd;

        int32 fieldref;

        int32 fieldorder;

        // field name
        std::string fieldname;

};


#endif
