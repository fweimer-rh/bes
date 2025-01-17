/////////////////////////////////////////////////////////////////////////////
//  This file is part of the hdf4 data handler for the OPeNDAP data server.
//
// Author:   Kent Yang <myang6@hdfgroup.org>
// Copyright (c) 2010-2012 The HDF Group
// The idea is borrowed from HDF4 OPeNDAP handler that is implemented by
// James Gallagher<jgallagher@opendap.org>

#ifndef HDF5_DDS_H_
#define HDF5_DDS_H_

#include "config_hdf5.h"


#include "hdf5.h"


#include <libdap/DDS.h>
#include <libdap/InternalErr.h>


/**
 * This specialization of DDS is used to manage the 'resource' of the open
 * HDF5 dataset handle so that the BES will close that handle once the
 * framework is done working with the file. This provides a way for the
 * code in gdal_dds.cc to read binary objects from the file using the gdal
 * library and embed those in instances of Grid. Those Grid variables are
 * used later on (but during the same service request, so the binary data
 * are still valid). When the DDS is deleted by the BES, the HDF5DDS()
 * destructor closes the file.
 *
 * @todo Change DataDDS to DDS if we can... Doing that will enable the
 * handler to use this to close the library using this class. That is not
 * strictly needed, but it would make both the DDS and DataDDS responses
 * work the same way.
 */
class HDF5DDS : public libdap::DDS {
private:
    hid_t fileid = -1;

    void m_duplicate(const HDF5DDS &src) 
    { 
        fileid = src.fileid;
    }

public:
    explicit HDF5DDS( const libdap::DDS *ddsIn) : libdap::DDS(*ddsIn) {}

    HDF5DDS(const HDF5DDS &rhs) : libdap::DDS(rhs) {
        m_duplicate(rhs);
    }

    HDF5DDS & operator= (const HDF5DDS &rhs) {
        if (this == &rhs)
            return *this;

        libdap::DDS::operator=(rhs);

        m_duplicate(rhs);

        return *this;
    }

    ~HDF5DDS() override {

        if (fileid != -1)
            H5Fclose(fileid);

    }

    // I think this should be set_fileid(...). jhrg 2/18/16
    void setHDF5Dataset(const hid_t fileid_in) { 
        fileid = fileid_in;
    }

};

#endif



