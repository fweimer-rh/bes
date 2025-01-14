// This file is part of the hdf5 data handler for the OPeNDAP data server.

/////////////////////////////////////////////////////////////////////////////


#include "HDF5CFGeoCFProj.h"

using namespace std;
using namespace libdap;


HDF5CFGeoCFProj::HDF5CFGeoCFProj(const string & n, const string &d ) : Byte(n, d)
{
}

BaseType *HDF5CFGeoCFProj::ptr_duplicate()
{
    return new HDF5CFGeoCFProj(*this);
}

bool HDF5CFGeoCFProj::read()
{
    // Just return a dummy value.
    char buf='p';
    set_read_p(true);
    set_value((dods_byte)buf);

    return true;

}

