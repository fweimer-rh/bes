// This file is part of hdf5_handler: an HDF5 file handler for the OPeNDAP
// data server.

// Copyright (c) 2007-2016 The HDF Group, Inc. and OPeNDAP, Inc.
//
// This is free software; you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License as published by the Free
// Software Foundation; either version 2.1 of the License, or (at your
// option) any later version.
//
// This software is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
// or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
// License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
//
// You can contact OPeNDAP, Inc. at PO Box 112, Saunderstown, RI. 02874-0112.
// You can contact The HDF Group, Inc. at 1800 South Oak Street,
// Suite 203, Champaign, IL 61820  

///////////////////////////////////////////////////////////////////////////////
/// \file h5get.cc
///  iterates all HDF5 internals.
/// 
///  This file includes all the routines to search HDF5 group, dataset, links,
///  and attributes. since we are using HDF5 C APIs, we include all c functions
///  in this file.
///
/// \author Hyo-Kyung Lee <hyoklee@hdfgroup.org>
/// \author Muqun Yang    <myang6@hdfgroup.org>
///
///////////////////////////////////////////////////////////////////////////////

#include "h5get.h"
#include "HDF5Int32.h"
#include "HDF5UInt32.h"
#include "HDF5UInt16.h"
#include "HDF5Int16.h"
#include "HDF5Byte.h"
#include "HDF5Int8.h"
#include "HDF5Int64.h"
#include "HDF5UInt64.h"
#include "HDF5Array.h"
#include "HDF5Str.h"
#include "HDF5Float32.h"
#include "HDF5Float64.h"
#include "HDF5Url.h"
#include "HDF5Structure.h"
#include "HDF5RequestHandler.h"

#include <BESDebug.h>
#include <BESSyntaxUserError.h>
#include <math.h>
#include <sstream>

using namespace libdap;


// H5Lvisit call back function.  After finding all the hard links, return 1. 
static int visit_link_cb(hid_t  group_id, const char *name, const H5L_info_t *oinfo,
    void *_op_data);

// H5OVISIT call back function. When finding the dimension scale attributes, return 1. 
static int
visit_obj_cb(hid_t o_id, const char *name, const H5O_info_t *oinfo,
    void *_op_data);

// H5Aiterate2 call back function, check if having the dimension scale attributes.
static herr_t attr_info_dimscale(hid_t loc_id, const char *name, const H5A_info_t *ainfo, void *opdata);


///////////////////////////////////////////////////////////////////////////////
/// \fn get_attr_info(hid_t dset, int index, bool is_dap4,DSattr_t *attr_inst_ptr,
///                  bool *ignoreptr)
///  will get attribute information.
///
/// This function will get attribute information: datatype, dataspace(dimension
/// sizes) and number of dimensions and put it into a data struct.
///
/// \param[in]  dset  dataset id
/// \param[in]  index  index of attribute
/// \param[in]  is_dap4 is this for DAP4
/// \param[out] attr_inst_ptr an attribute instance pointer
/// \param[out] ignoreptr  a flag to record whether it can be ignored.
/// \return pointer to attribute structure
/// \throw InternalError 
///////////////////////////////////////////////////////////////////////////////
hid_t get_attr_info(hid_t dset, int index, bool is_dap4, DSattr_t * attr_inst_ptr,
                    bool *ignore_attr_ptr)
{

    hid_t attrid = -1;

    // Always assume that we don't ignore any attributes.
    *ignore_attr_ptr = false;

    if ((attrid = H5Aopen_by_idx(dset, ".", H5_INDEX_CRT_ORDER, H5_ITER_INC,(hsize_t)index, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        string msg = "unable to open attribute by index ";
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    // Obtain the size of attribute name.
    ssize_t name_size =  H5Aget_name(attrid, 0, nullptr);
    if (name_size < 0) {
        H5Aclose(attrid);
        string msg = "unable to obtain the size of the hdf5 attribute name ";
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    vector<char> attr_name;
    attr_name.resize(name_size+1);
    // Obtain the attribute name.    
    if ((H5Aget_name(attrid, name_size+1, attr_name.data())) < 0) {
        H5Aclose(attrid);
        string msg = "unable to obtain the hdf5 attribute name  ";
        throw InternalErr(__FILE__, __LINE__, msg);
    }
    
    // Obtain the type of the attribute. 
    hid_t ty_id = -1;
    if ((ty_id = H5Aget_type(attrid)) < 0) {
        string msg = "unable to obtain hdf5 datatype for the attribute  ";
        string attrnamestr(attr_name.begin(),attr_name.end());
        msg += attrnamestr;
        H5Aclose(attrid);
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    H5T_class_t ty_class = H5Tget_class(ty_id);
    if (ty_class < 0) {
        string msg = "cannot get hdf5 attribute datatype class for the attribute ";
        string attrnamestr(attr_name.begin(),attr_name.end());
        msg += attrnamestr;
        H5Aclose(attrid);
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    // The following datatype will not be supported for mapping to DAS for both DAP2 and DAP4.
    // Note:DAP4 explicitly states that the data should be defined atomic datatype(int, string).
    // 1-D variable length of string can also be mapped to both DAS and DDS.
    // The variable length string class is H5T_STRING rather than H5T_VLEN,
    // So safe here. 
    // We also ignore the mapping of integer 64 bit for DAP2 since DAP2 doesn't
    // support 64-bit integer. In theory, DAP2 doesn't support long double
    // (128-bit or 92-bit floating point type), since this rarely happens
    // in DAP application, we simply don't consider here.
    // However, DAP4 supports 64-bit integer.
    if ((ty_class == H5T_TIME) || (ty_class == H5T_BITFIELD)
        || (ty_class == H5T_OPAQUE) || (ty_class == H5T_ENUM)
        || (ty_class == H5T_REFERENCE) ||(ty_class == H5T_COMPOUND)
        || (ty_class == H5T_VLEN) || (ty_class == H5T_ARRAY)){ 
        
        *ignore_attr_ptr = true;
        H5Tclose(ty_id);
        return attrid;
    }

    // Ignore 64-bit integer for DAP2.
    // The nested if is better to understand the code. Don't combine
    if (false == is_dap4) {
        if((ty_class == H5T_INTEGER) && (H5Tget_size(ty_id)== 8)) {//64-bit int
            *ignore_attr_ptr = true;
            H5Tclose(ty_id);
            return attrid;
        }
    }       

    // Here we ignore netCDF-4 specific attributes for DAP4 to make filenetCDF-4 work
    if (true == is_dap4 && HDF5RequestHandler::get_default_handle_dimension() == true) {
        // Remove the nullptrTERM etc.
        string attr_name_str(attr_name.begin(),attr_name.end()-1);
        if(attr_name_str == "CLASS" || attr_name_str == "NAME" || attr_name_str == "_Netcdf4Dimid" 
           || attr_name_str == "_nc3_strict" || attr_name_str=="_NCProperties" || attr_name_str=="_Netcdf4Coordinates") {
            *ignore_attr_ptr = true;
            H5Tclose(ty_id);
            return attrid;
        }
    }

    hid_t aspace_id = -1;
    if ((aspace_id = H5Aget_space(attrid)) < 0) {
        string msg = "cannot get hdf5 dataspace id for the attribute ";
        string attrnamestr(attr_name.begin(),attr_name.end());
        msg += attrnamestr;
        H5Aclose(attrid);
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    // It is better to use the dynamic allocation of the array.
    // However, since the DODS_MAX_RANK is not big and it is also
    // used in other location, we still keep the original code.
    // KY 2011-11-16

    int ndims = H5Sget_simple_extent_ndims(aspace_id);
    if (ndims < 0) {
        string msg = "cannot get hdf5 dataspace number of dimension for attribute ";
        string attrnamestr(attr_name.begin(),attr_name.end());
        msg += attrnamestr;
        H5Sclose(aspace_id);
        H5Aclose(attrid);
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    // Check if the dimension size exceeds the maximum number of dimension DAP supports
    if (ndims > DODS_MAX_RANK) {
        string msg = "number of dimensions exceeds allowed for attribute ";
        string attrnamestr(attr_name.begin(),attr_name.end());
        msg += attrnamestr;
        H5Sclose(aspace_id);
        H5Aclose(attrid);
        throw InternalErr(__FILE__, __LINE__, msg);
    }
      
    vector<hsize_t> size(ndims);
    vector<hsize_t> maxsize(ndims);

    //The HDF5 attribute should not have unlimited dimension,
    // maxsize is only a place holder.
    if (H5Sget_simple_extent_dims(aspace_id, size.data(), maxsize.data())<0){
        string msg = "cannot obtain the dim. info for the attribute ";
        string attrnamestr(attr_name.begin(),attr_name.end());
        msg += attrnamestr;
        H5Sclose(aspace_id);
        H5Aclose(attrid);
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    // Return ndims and size[ndims]. 
    hsize_t nelmts = 1;
    if (ndims) {
        for (int j = 0; j < ndims; j++)
            nelmts *= size[j];
    }
    
    size_t ty_size = H5Tget_size(ty_id);
    if (ty_size == 0) {
        string msg = "cannot obtain the dtype size for the attribute ";
        string attrnamestr(attr_name.begin(),attr_name.end());
        msg += attrnamestr;
        H5Sclose(aspace_id);
        H5Aclose(attrid);
        throw InternalErr(__FILE__, __LINE__, msg);
    }
     
    size_t need = nelmts * H5Tget_size(ty_id);

    // We want to save memory type in the struct.
    hid_t memtype = H5Tget_native_type(ty_id, H5T_DIR_ASCEND);
    if (memtype < 0){
        string msg = "cannot obtain the memory dtype for the attribute ";
        string attrnamestr(attr_name.begin(),attr_name.end());
        msg += attrnamestr;
        H5Sclose(aspace_id);
        H5Aclose(attrid);
	throw InternalErr(__FILE__, __LINE__, msg);
    }

    // Save the information to the struct
    (*attr_inst_ptr).type = memtype;
    (*attr_inst_ptr).ndims = ndims;
    (*attr_inst_ptr).nelmts = nelmts;
    (*attr_inst_ptr).need = need;
    strncpy((*attr_inst_ptr).name, attr_name.data(), name_size+1);

    // The handler assumes the size of an attribute is limited to 32-bit int
    for (int j = 0; j < ndims; j++) {
        (*attr_inst_ptr).size[j] = (int)(size[j]);
    }
   
    if(H5Sclose(aspace_id)<0) {
        H5Aclose(attrid);
        throw InternalErr(__FILE__,__LINE__,"Cannot close HDF5 dataspace ");
    }

    return attrid;
}

///////////////////////////////////////////////////////////////////////////////
/// \fn get_dap_type(hid_t type,bool is_dap4)
/// returns the string representation of HDF5 type.
///
/// This function will get the text representation(string) of the corresponding
/// DODS datatype. DODS-HDF5 subclass method will use this function.
/// Return type is different for DAP2 and DAP4.
///
/// \return string
/// \param type datatype id
/// \param is_dap4 is this for DAP4(for the calls from DMR-related routines)
///////////////////////////////////////////////////////////////////////////////
string get_dap_type(hid_t type, bool is_dap4)
{
    size_t size = 0;
    H5T_sign_t sign;
    BESDEBUG("h5", ">get_dap_type(): type="  << type << endl);
    H5T_class_t class_t = H5Tget_class(type);
    if (H5T_NO_CLASS == class_t)
        throw InternalErr(__FILE__, __LINE__, 
                          "The HDF5 datatype doesn't belong to any Class."); 
    switch (class_t) {

    case H5T_INTEGER:

        size = H5Tget_size(type);
        if (size == 0){
            throw InternalErr(__FILE__, __LINE__,
                              "size of datatype is invalid");
        }

        sign = H5Tget_sign(type);
        if (sign < 0){
            throw InternalErr(__FILE__, __LINE__,
                              "sign of datatype is invalid");
        }

        BESDEBUG("h5", "=get_dap_type(): H5T_INTEGER" <<
            " sign = " << sign <<
            " size = " << size <<
            endl);
        if (size == 1){
            // DAP2 doesn't have signed 8-bit integer, so we need map it to INT16.
            if(true == is_dap4) {
                if (sign == H5T_SGN_NONE)      
                    return BYTE;
                else
                    return INT8;
            }
            else {
                if (sign == H5T_SGN_NONE)      
                    return BYTE;    
                else
                    return INT16;
            }
        }

        if (size == 2) {
            if (sign == H5T_SGN_NONE)
                return UINT16;
            else
                return INT16;
        }

        if (size == 4) {
            if (sign == H5T_SGN_NONE)
                return UINT32;
            else
                return INT32;
        }

        if(size == 8) {
            // DAP4 supports 64-bit integer.
            if (true == is_dap4) {
                if (sign == H5T_SGN_NONE)
                    return UINT64;
                else
                    return INT64;
            }
            else
                return INT_ELSE;
        }

        return INT_ELSE;

    case H5T_FLOAT:
        size = H5Tget_size(type);
        if (size == 0){
            throw InternalErr(__FILE__, __LINE__,
                              "size of the datatype is invalid");
        }

        BESDEBUG("h5", "=get_dap_type(): FLOAT size = " << size << endl);
        if (size == 4)
            return FLOAT32;
        if (size == 8)
            return FLOAT64;

        return FLOAT_ELSE;

    case H5T_STRING:
        BESDEBUG("h5", "<get_dap_type(): H5T_STRING" << endl);
        return STRING;

    case H5T_REFERENCE:
        BESDEBUG("h5", "<get_dap_type(): H5T_REFERENCE" << endl);
        return URL;
    // Note: Currently DAP2 and DAP4 only support defined atomic types.
    // So the H5T_COMPOUND and H5_ARRAY cases should never be reached for attribute handling. 
    // However, this function may be used for variable handling.
    case H5T_COMPOUND:
        BESDEBUG("h5", "<get_dap_type(): COMPOUND" << endl);
        return COMPOUND;

    case H5T_ARRAY:
        return ARRAY;

    default:
        BESDEBUG("h5", "<get_dap_type(): Unmappable Type" << endl);
        return "Unmappable Type";
    }
}

///////////////////////////////////////////////////////////////////////////////
/// \fn get_fileid(const char *filename)
/// gets HDF5 file id.
/// 
/// This function is used because H5Fopen cannot be directly used in a C++
/// code.
/// \param filename HDF5 filename
/// \return a file handler id
///////////////////////////////////////////////////////////////////////////////
hid_t get_fileid(const char *filename)
{
    hid_t fileid = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
    if (fileid < 0){
        string msg = "cannot open the HDF5 file  ";
        string filenamestr(filename);
        msg += filenamestr;
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    return fileid;
}

///////////////////////////////////////////////////////////////////////////////
/// \fn close_fileid(hid_t fid)
/// closes HDF5 file reffered by \a fid.
/// 
/// This function closes the HDF5 file.
///
/// \param fid HDF5 file id
/// \return throws an error if it can't close the file.
///////////////////////////////////////////////////////////////////////////////
void close_fileid(hid_t fid)
{
    if (H5Fclose(fid) < 0)
        throw Error(unknown_error,
                    string("Could not close the HDF5 file."));

}

///////////////////////////////////////////////////////////////////////////////
/// \fn get_dataset(hid_t pid, const string &dname, DS_t * dt_inst_ptr)
/// For DAP2, obtain data information in a dataset datatype, dataspace(dimension sizes)
/// and number of dimensions and put these information into a pointer of data
/// struct.
///
/// \param[in] pid    parent object id(group id)
/// \param[in] dname  dataset name
/// \param[out] dt_inst_ptr  pointer to the attribute struct(* attr_inst_ptr)
///////////////////////////////////////////////////////////////////////////////

void get_dataset(hid_t pid, const string &dname, DS_t * dt_inst_ptr)
{
    BESDEBUG("h5", ">get_dataset()" << endl);

    // Obtain the dataset ID
    hid_t dset = -1;
    if ((dset = H5Dopen(pid, dname.c_str(),H5P_DEFAULT)) < 0) {
        string msg = "cannot open the HDF5 dataset  ";
        msg += dname;
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    // Obtain the datatype ID
    hid_t dtype = -1;
    if ((dtype = H5Dget_type(dset)) < 0) {
        H5Dclose(dset);
        string msg = "cannot get the the datatype of HDF5 dataset  ";
        msg += dname;
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    // Obtain the datatype class 
    H5T_class_t ty_class = H5Tget_class(dtype);
    if (ty_class < 0) {
        H5Tclose(dtype);
        H5Dclose(dset);
        string msg = "cannot get the datatype class of HDF5 dataset  ";
        msg += dname;
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    // These datatype classes are unsupported. Note we do support
    // variable length string and the variable length string class is
    // H5T_STRING rather than H5T_VLEN.
    if ((ty_class == H5T_TIME) || (ty_class == H5T_BITFIELD)
        || (ty_class == H5T_OPAQUE) || (ty_class == H5T_ENUM) || (ty_class == H5T_VLEN)) {
        string msg = "unexpected datatype of HDF5 dataset  ";
        msg += dname;
        throw InternalErr(__FILE__, __LINE__, msg);
    }
   
    hid_t dspace = -1;
    if ((dspace = H5Dget_space(dset)) < 0) {
        H5Tclose(dtype);
        H5Dclose(dset);
        string msg = "cannot get the the dataspace of HDF5 dataset  ";
        msg += dname;
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    // It is better to use the dynamic allocation of the array.
    // However, since the DODS_MAX_RANK is not big and it is also
    // used in other location, we still keep the original code.
    // KY 2011-11-17

    int ndims = H5Sget_simple_extent_ndims(dspace);
    if (ndims < 0) {
        H5Tclose(dtype);
        H5Sclose(dspace);
        H5Dclose(dset);
        string msg = "cannot get hdf5 dataspace number of dimension for dataset ";
        msg += dname;
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    // Check if the dimension size exceeds the maximum number of dimension DAP supports
    if (ndims > DODS_MAX_RANK) {
        string msg = "number of dimensions exceeds allowed for dataset ";
        msg += dname;
        H5Tclose(dtype);
        H5Sclose(dspace);
        H5Dclose(dset);
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    vector<hsize_t>size(ndims);
    vector<hsize_t>maxsize(ndims);

    // Retrieve size. DAP4 doesn't have a convention to support multi-unlimited dimension yet.
    if (H5Sget_simple_extent_dims(dspace, size.data(), maxsize.data())<0){
        string msg = "cannot obtain the dim. info for the dataset ";
        msg += dname;
        H5Tclose(dtype);
        H5Sclose(dspace);
        H5Dclose(dset);
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    // return ndims and size[ndims]. 
    hsize_t nelmts = 1;
    if (ndims !=0) {
        for (int j = 0; j < ndims; j++)
            nelmts *= size[j];
    }

    size_t dtype_size = H5Tget_size(dtype);
    if (dtype_size == 0) {
        string msg = "cannot obtain the data type size for the dataset ";
        msg += dname;
        H5Tclose(dtype);
        H5Sclose(dspace);
        H5Dclose(dset);
        throw InternalErr(__FILE__, __LINE__, msg);
    }
 
    size_t need = nelmts * dtype_size;

    hid_t memtype = H5Tget_native_type(dtype, H5T_DIR_ASCEND);
    if (memtype < 0){
        string msg = "cannot obtain the memory data type for the dataset ";
        msg += dname;
        H5Tclose(dtype);
        H5Sclose(dspace);
        H5Dclose(dset);
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    (*dt_inst_ptr).type = memtype;
    (*dt_inst_ptr).ndims = ndims;
    (*dt_inst_ptr).nelmts = nelmts;
    (*dt_inst_ptr).need = need;
    strncpy((*dt_inst_ptr).name, dname.c_str(), dname.size());
    (*dt_inst_ptr).name[dname.size()] = '\0';
    for (int j = 0; j < ndims; j++) 
        (*dt_inst_ptr).size[j] = size[j];

    if(H5Tclose(dtype)<0) {
        H5Sclose(dspace);
        H5Dclose(dset);
        throw InternalErr(__FILE__, __LINE__, "Cannot close the HDF5 datatype.");
    }

    if(H5Sclose(dspace)<0) {
        H5Dclose(dset);
        throw InternalErr(__FILE__, __LINE__, "Cannot close the HDF5 dataspace.");
    }

    if(H5Dclose(dset)<0) {
        throw InternalErr(__FILE__, __LINE__, "Cannot close the HDF5 dataset.");
    }

}

///////////////////////////////////////////////////////////////////////////////
/// \fn get_dataset_dmr(const hid_t file_id, hid_t pid, const string &dname, DS_t * dt_inst_ptr)
/// For DAP4, obtain data information in a dataset datatype, dataspace(dimension sizes)
/// ,number of dimensions,dimension and hardlink information for dimensions
/// and put these information into a pointer of data struct.
///
/// \param[in] file_id  HDF5 file_id(need for searching all hard links.)
/// \param[in] pid    parent object id(group id)
/// \param[in] dname  dataset name
/// \param[in] use_dimscale whether dimscale is used. 
/// \param[in] is_pure_dim whether this dimension is a pure dimension. 
/// \param[in\out] vector to store hardlink info. of a dataset.
/// \param[out] dt_inst_ptr  pointer to the attribute struct(* attr_inst_ptr)
///////////////////////////////////////////////////////////////////////////////
void get_dataset_dmr(const hid_t file_id, hid_t pid, const string &dname, DS_t * dt_inst_ptr,bool use_dimscale, bool is_eos5, bool &is_pure_dim, vector<link_info_t> &hdf5_hls)
{

    BESDEBUG("h5", ">get_dataset()" << endl);

    // Obtain the dataset ID
    hid_t dset = -1;
    if ((dset = H5Dopen(pid, dname.c_str(),H5P_DEFAULT)) < 0) {
        string msg = "cannot open the HDF5 dataset  ";
        msg += dname;
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    // Obtain the datatype ID
    hid_t dtype = -1;
    if ((dtype = H5Dget_type(dset)) < 0) {
        H5Dclose(dset);
        string msg = "cannot get the the datatype of HDF5 dataset  ";
        msg += dname;
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    // Obtain the datatype class 
    H5T_class_t ty_class = H5Tget_class(dtype);
    if (ty_class < 0) {
        H5Tclose(dtype);
        H5Dclose(dset);
        string msg = "cannot get the datatype class of HDF5 dataset  ";
        msg += dname;
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    // These datatype classes are unsupported. Note we do support
    // variable length string and the variable length string class is
    // H5T_STRING rather than H5T_VLEN.
    if ((ty_class == H5T_TIME) || (ty_class == H5T_BITFIELD)
        || (ty_class == H5T_OPAQUE) || (ty_class == H5T_ENUM) || (ty_class == H5T_VLEN)) {
        string msg = "unexpected datatype of HDF5 dataset  ";
        msg += dname;
        throw InternalErr(__FILE__, __LINE__, msg);
    }
   
    hid_t dspace = -1;
    if ((dspace = H5Dget_space(dset)) < 0) {
        H5Tclose(dtype);
        H5Dclose(dset);
        string msg = "cannot get the the dataspace of HDF5 dataset  ";
        msg += dname;
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    // It is better to use the dynamic allocation of the array.
    // However, since the DODS_MAX_RANK is not big and it is also
    // used in other location, we still keep the original code.
    // KY 2011-11-17

    int ndims = H5Sget_simple_extent_ndims(dspace);
    if (ndims < 0) {
        H5Tclose(dtype);
        H5Sclose(dspace);
        H5Dclose(dset);
        string msg = "cannot get hdf5 dataspace number of dimension for dataset ";
        msg += dname;
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    // Check if the dimension size exceeds the maximum number of dimension DAP supports
    if (ndims > DODS_MAX_RANK) {
        string msg = "number of dimensions exceeds allowed for dataset ";
        msg += dname;
        H5Tclose(dtype);
        H5Sclose(dspace);
        H5Dclose(dset);
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    vector<hsize_t>size(ndims);
    vector<hsize_t>maxsize(ndims);

    // Retrieve size. DAP4 doesn't have a convention to support multi-unlimited dimension yet.
    if (H5Sget_simple_extent_dims(dspace, size.data(), maxsize.data())<0){
        string msg = "cannot obtain the dim. info for the dataset ";
        msg += dname;
        H5Tclose(dtype);
        H5Sclose(dspace);
        H5Dclose(dset);
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    // return ndims and size[ndims]. 
    hsize_t nelmts = 1;
    if (ndims !=0) {
        for (int j = 0; j < ndims; j++)
            nelmts *= size[j];
    }

    size_t dtype_size = H5Tget_size(dtype);
    if (dtype_size == 0) {
        string msg = "cannot obtain the data type size for the dataset ";
        msg += dname;
        H5Tclose(dtype);
        H5Sclose(dspace);
        H5Dclose(dset);
        throw InternalErr(__FILE__, __LINE__, msg);
    }
 
    size_t need = nelmts * dtype_size;

    hid_t memtype = H5Tget_native_type(dtype, H5T_DIR_ASCEND);
    if (memtype < 0){
        string msg = "cannot obtain the memory data type for the dataset ";
        msg += dname;
        H5Tclose(dtype);
        H5Sclose(dspace);
        H5Dclose(dset);
        throw InternalErr(__FILE__, __LINE__, msg);
    }

    (*dt_inst_ptr).type = memtype;
    (*dt_inst_ptr).ndims = ndims;
    (*dt_inst_ptr).nelmts = nelmts;
    (*dt_inst_ptr).need = need;
    strncpy((*dt_inst_ptr).name, dname.c_str(), dname.size());
    (*dt_inst_ptr).name[dname.size()] = '\0';
    for (int j = 0; j < ndims; j++) 
        (*dt_inst_ptr).size[j] = size[j];

    // For DAP4 when dimension scales are used.
    if(true == use_dimscale) {
        BESDEBUG("h5", "<h5get.cc: get_dataset() use dim scale is true." << endl);

        // Some HDF5 datasets are dimension scale datasets; some are not. We need to distinguish.
        bool is_dimscale = false;

        // Dimension scales must be 1-D.
        if(1 == ndims) {

            bool has_ds_attr = false;

            try{
                has_ds_attr = has_dimscale_attr(dset);
            }
            catch(...) {
                H5Tclose(dtype);
                H5Sclose(dspace);
                H5Dclose(dset);
                throw InternalErr(__FILE__, __LINE__, "Fail to check dim. scale.");
            }

            if (true == has_ds_attr) {

#if 0
//int count = 0;
//vector<bool>dim_attr_mark;
//dim_attr_mark.resize(4);
//bool dim_attr_mark[4];
#endif
                 
                // the vector seems not working. use array.   
                int dim_attr_mark[3];
                for(int i = 0;i<3;i++)
                    dim_attr_mark[i] = 0;

                // This will check if "NAME" and "REFERENCE_LIST" exists.
#if 0
/herr_t ret = H5Aiterate2(dset, H5_INDEX_NAME, H5_ITER_INC, nullptr, attr_info, dim_attr_mark.data());
#endif
                herr_t ret = H5Aiterate2(dset, H5_INDEX_NAME, H5_ITER_INC, nullptr, attr_info_dimscale, dim_attr_mark);
                if(ret < 0) {
                    string msg = "cannot interate the attributes of the dataset ";
                    msg += dname;
                    H5Tclose(dtype);
                    H5Sclose(dspace);
                    H5Dclose(dset);
                    throw InternalErr(__FILE__, __LINE__, msg);
                }
    
                for (int i = 0; i<3;i++)
                    BESDEBUG("h5","dim_attr_mark is "<<dim_attr_mark[i] <<endl);
                // Find the dimension scale. DIM*SCALE is a must. Then NAME=VARIABLE or (REFERENCE_LIST and not PURE DIM)
                // Here a little bias towards files created by the netCDF-4 APIs. 
                // If we don't have RERERENCE_LIST in a dataset that has CLASS=DIMENSION_SCALE attribute,
                // we will ignore this orphanage dimension scale since it is not associated with other datasets.
                // However, it is an orphanage dimension scale created by the netCDF-4 APIs, we think
                // it must have a purpose to do this way by data creator. So keep this as a dimension scale.
                //
                if ((dim_attr_mark[0] && !dim_attr_mark[1]) || dim_attr_mark[2]) 
                    is_dimscale =true;
                else if(dim_attr_mark[1]) {
                    is_pure_dim = true;
                    // We need to remember if this dimension is unlimited dimension,maybe in the future. 2022-11-13
                }
            }
        }
 
        if (true == is_dimscale) {
            BESDEBUG("h5", "<h5get.cc: dname is " << dname << endl);
            BESDEBUG("h5", "<h5get.cc: get_dataset() this is  dim scale." << endl);
            BESDEBUG("h5", "<h5get.cc: dataset storage size is: " <<H5Dget_storage_size(dset)<< endl);
#if 0
            //if(H5Dget_storage_size(dset)!=0) { 
#endif
            // Save the dimension names.We Only need to provide the dimension name(not the full path).
            // We still need the dimension name fullpath for distinguishing the different dimension that
            // has the same dimension name but in the different path
            // We need to handle the special characters inside the dimension names of the HDF-EOS5 that has the dim. scales

            if (is_eos5) {
                string temp_orig_dim_name = dname.substr(dname.find_last_of("/")+1);
                string temp_dim_name = handle_string_special_characters(temp_orig_dim_name);
                string temp_dim_path = handle_string_special_characters_in_path(dname);
                (*dt_inst_ptr).dimnames.push_back(temp_dim_name);
                (*dt_inst_ptr).dimnames_path.push_back(temp_dim_path);
            }
            else { // AFAIK, NASA netCDF-4 like files are following CF name conventions. So no need to carry out the special character operations.
                (*dt_inst_ptr).dimnames.push_back(dname.substr(dname.find_last_of("/")+1));
                (*dt_inst_ptr).dimnames_path.push_back(dname);
            }
#if 0
           //}
           //else 
            //is_pure_dim = true;
#endif
            is_pure_dim = false;
        }

        else if(false == is_pure_dim) // Except pure dimension,we need to save all dimension names in this dimension. 
            obtain_dimnames(file_id,dset,ndims,dt_inst_ptr,hdf5_hls,is_eos5);
    }
    
    if(H5Tclose(dtype)<0) {
        H5Sclose(dspace);
        H5Dclose(dset);
        throw InternalErr(__FILE__, __LINE__, "Cannot close the HDF5 datatype.");
    }

    if(H5Sclose(dspace)<0) {
        H5Dclose(dset);
        throw InternalErr(__FILE__, __LINE__, "Cannot close the HDF5 dataspace.");
    }

    if(H5Dclose(dset)<0) {
        throw InternalErr(__FILE__, __LINE__, "Cannot close the HDF5 dataset.");
    }
 
}

///////////////////////////////////////////////////////////////////////////////
/// \fn check_h5str(hid_t h5type)
/// checks if type is HDF5 string type
/// 
/// \param h5type data type id
/// \return true if type is string
/// \return false otherwise
///////////////////////////////////////////////////////////////////////////////
bool check_h5str(hid_t h5type)
{
    if (H5Tget_class(h5type) == H5T_STRING)
        return true;
    else
        return false;
}


///////////////////////////////////////////////////////////////////////////////
/// \fn print_attr(hid_t type, int loc, void *sm_buf)
/// will get the printed representation of an attribute.
///
///
/// \param type  HDF5 data type id
/// \param loc    the number of array number
/// \param sm_buf pointer to an attribute
/// \return a string
///////////////////////////////////////////////////////////////////////////////
string print_attr(hid_t type, int loc, void *sm_buf) {
    union {
        unsigned char* ucp;
        char *tcp;
        short *tsp;
        unsigned short *tusp;
        int *tip;
        unsigned int*tuip;
        long *tlp;
        unsigned long*tulp;
        float *tfp;
        double *tdp;
    } gp;

    vector<char> rep;

    switch (H5Tget_class(type)) {

        case H5T_INTEGER: {

            size_t size = H5Tget_size(type);
            if (size == 0){
                throw InternalErr(__FILE__, __LINE__,
                                  "size of datatype is invalid");
            }

            H5T_sign_t sign = H5Tget_sign(type);
            if (sign < 0){
                throw InternalErr(__FILE__, __LINE__,
                                  "sign of datatype is invalid");
            }

            BESDEBUG("h5", "=get_dap_type(): H5T_INTEGER" <<
                        " sign = " << sign <<
                        " size = " << size <<
                        endl);

            // change void pointer into the corresponding integer datatype.
            // 32 should be long enough to hold one integer and one
            // floating point number.
            rep.resize(32);

            if (size == 1){
 
                if(sign == H5T_SGN_NONE) {
                    gp.ucp = (unsigned char *) sm_buf;
                    unsigned char tuchar = *(gp.ucp + loc);
                    snprintf(rep.data(), 32, "%u", tuchar);
                }

                else {
                    gp.tcp = (char *) sm_buf;
                    snprintf(rep.data(), 32, "%d", *(gp.tcp + loc));
                }
            }

            else if (size == 2) {

                if(sign == H5T_SGN_NONE) {
                    gp.tusp = (unsigned short *) sm_buf;
                    snprintf(rep.data(), 32, "%hu", *(gp.tusp + loc));
 
                }
                else {
                    gp.tsp = (short *) sm_buf;
                    snprintf(rep.data(), 32, "%hd", *(gp.tsp + loc));
 
                }
            }

            else if (size == 4) {

                if(sign == H5T_SGN_NONE) {
                    gp.tuip = (unsigned int *) sm_buf;
                    snprintf(rep.data(), 32, "%u", *(gp.tuip + loc));
 
                }
                else {
                    gp.tip = (int *) sm_buf;
                    snprintf(rep.data(), 32, "%d", *(gp.tip + loc));
                }
            }
            else if (size == 8) {

                if(sign == H5T_SGN_NONE) {
                    gp.tulp = (unsigned long *) sm_buf;
                    snprintf(rep.data(), 32, "%lu", *(gp.tulp + loc));
                }
                else {
                    gp.tlp = (long *) sm_buf;
                    snprintf(rep.data(), 32, "%ld", *(gp.tlp + loc));
                }
            }
            else 
                throw InternalErr(__FILE__, __LINE__,"Unsupported integer type, check the size of datatype.");

            break;
        }

        case H5T_FLOAT: {
            rep.resize(32);
            char gps[32];

            if (H5Tget_size(type) == 4) {
                
                float attr_val = *(float*)sm_buf;
                // Note: this comparsion is the same as isnan.
                // However, on CentOS 7, isnan() is declared in two headers and causes conflicts.
                if (attr_val!=attr_val) {
                    rep.resize(3);
                    rep[0]='N';
                    rep[1]='a';
                    rep[2]='N';
                }
                else {
                   bool is_a_fin = isfinite(attr_val);
                   // Represent the float number.
                   // Some space may be wasted. But it is okay.
                   gp.tfp = (float *) sm_buf;
                   int ll = snprintf(gps, 30, "%.10g", *(gp.tfp + loc));
#if 0
                   //int ll = strlen(gps);
#endif
                   // Add the dot to assure this is a floating number
                   if (!strchr(gps, '.') && !strchr(gps, 'e') && !strchr(gps,'E')
                      && (true == is_a_fin)){
                       gps[ll++] = '.';
                   }
   
                   gps[ll] = '\0';
                   snprintf(rep.data(), 32, "%s", gps);
                }
            } 
            else if (H5Tget_size(type) == 8) {

                double attr_val = *(double*)sm_buf;
                // Note: this comparsion is the same as isnan.
                // However, on CentOS 7, isnan() is declared in two headers and causes conflicts.
                if (attr_val!=attr_val) {
                    rep.resize(3);
                    rep[0]='N';
                    rep[1]='a';
                    rep[2]='N';
                }
                else {
                    bool is_a_fin = isfinite(attr_val);
                    gp.tdp = (double *) sm_buf;
                    int ll = snprintf(gps, 30, "%.17g", *(gp.tdp + loc));
#if 0
                    //int ll = strlen(gps);
#endif
                    if (!strchr(gps, '.') && !strchr(gps, 'e')&& !strchr(gps,'E')
                       && (true == is_a_fin)) {
                        gps[ll++] = '.';
                    }
                    gps[ll] = '\0';
                    snprintf(rep.data(), 32, "%s", gps);
                }
            } 
            else if (H5Tget_size(type) == 0){
                throw InternalErr(__FILE__, __LINE__, "H5Tget_size() failed.");
            }
            break;
        }

        case H5T_STRING: {
            size_t str_size = H5Tget_size(type);
            if(H5Tis_variable_str(type)>0) {
                throw InternalErr(__FILE__, __LINE__, 
                      "print_attr function doesn't handle variable length string, variable length string should be handled separately.");
            }
            if (str_size == 0){
                throw InternalErr(__FILE__, __LINE__, "H5Tget_size() failed.");
            }
            BESDEBUG("h5", "=print_attr(): H5T_STRING sm_buf=" << (char *) sm_buf
                << " size=" << str_size << endl);
            // Not sure why the original code add 1 byte to the buffer, perhaps to keep the c-style? KY 2021-04-12
#if 0
            //rep.resize(str_size+1);
#endif
            rep.resize(str_size);
            strncpy(rep.data(), (char *) sm_buf, str_size);

            //Also should add the NULL term at the end. We just need the data in C++.
#if 0
            //rep[str_size] = '\0';
#endif
#if 0
            char *buf = nullptr;
            // This try/catch block is here to protect the allocation of buf.
            try {

                
                buf = new char[str_size + 1];
                strncpy(buf.data(), (char *) sm_buf, str_size);
                buf[str_size] = '\0';
                // Not necessarily allocate 3 more bytes. 
                rep.resize(str_size+3);
                snprintf(rep.data(), str_size + 3, "%s", buf);
                rep[str_size + 2] = '\0';
                delete[] buf; buf = 0;
            }
            catch (...) {
                if( buf ) delete[] buf;
                throw;
            }
#endif
            break;
        }

        default:
	    break;
    } // switch(H5Tget_class(type))

    string rep_str(rep.begin(),rep.end());
    return rep_str;
}

D4AttributeType daptype_strrep_to_dap4_attrtype(const string & s){
    
    if (s == "Byte")
        return attr_byte_c;
    else if (s == "Int8")
        return attr_int8_c;
    else if (s == "UInt8") // This may never be used.
        return attr_uint8_c;
    else if (s == "Int16")
        return attr_int16_c;
    else if (s == "UInt16")
        return attr_uint16_c;
    else if (s == "Int32")
        return attr_int32_c;
    else if (s == "UInt32")
        return attr_uint32_c;
    else if (s == "Int64")
        return attr_int64_c;
    else if (s == "UInt64")
        return attr_uint64_c;
    else if (s == "Float32")
        return attr_float32_c;
    else if (s == "Float64")
        return attr_float64_c;
    else if (s == "String")
        return attr_str_c;
    else if (s == "Url")
        return attr_url_c;
    else
        return attr_null_c;


}


///////////////////////////////////////////////////////////////////////////////
/// \fn Get_bt(const string &varname,  const string  &dataset, hid_t datatype)
/// returns the pointer to the base type
///
/// This function will create a new DODS object that corresponds to HDF5
/// dataset and return the pointer of a new object of DODS datatype. If an
/// error is found, an exception of type InternalErr is thrown. 
///
/// \param varname object name
/// \param dataset name of dataset where this object comes from
/// \param datatype datatype id
/// \return pointer to BaseType
///////////////////////////////////////////////////////////////////////////////
//static BaseType *Get_bt(const string &vname,
BaseType *Get_bt(const string &vname,
                 const string &vpath,
                 const string &dataset,
                 hid_t datatype, bool is_dap4)
{
    BaseType *btp = nullptr;

    try {

        BESDEBUG("h5", ">Get_bt varname=" << vname << " datatype=" << datatype
            << endl);

        size_t size = 0;
        H5T_sign_t sign    = H5T_SGN_ERROR;
        switch (H5Tget_class(datatype)) {

        case H5T_INTEGER:
        {
            size = H5Tget_size(datatype);
            sign = H5Tget_sign(datatype);
            BESDEBUG("h5", "=Get_bt() H5T_INTEGER size = " << size << " sign = "
                << sign << endl);

            if (sign == H5T_SGN_ERROR) {
                throw InternalErr(__FILE__, __LINE__, "cannot retrieve the sign type of the integer");
            }
            if (size == 0) {
                throw InternalErr(__FILE__, __LINE__, "cannot return the size of the datatype");
            }
            else if (size == 1) { // Either signed char or unsigned char
                // DAP2 doesn't support signed char, it maps to DAP int16.
                if (sign == H5T_SGN_2) {
                    if (false == is_dap4) // signed char to DAP2 int16
                        btp = new HDF5Int16(vname, vpath, dataset);
                    else
                        btp = new HDF5Int8(vname,vpath,dataset);
                }
                else
                    btp = new HDF5Byte(vname, vpath,dataset);
            }
            else if (size == 2) {
                if (sign == H5T_SGN_2)
                    btp = new HDF5Int16(vname, vpath,dataset);
                else
                    btp = new HDF5UInt16(vname,vpath, dataset);
            }
            else if (size == 4) {
                if (sign == H5T_SGN_2){
                    btp = new HDF5Int32(vname, vpath,dataset);
                }
                else
                    btp = new HDF5UInt32(vname,vpath, dataset);
            }
            else if (size == 8) {
                if(true == is_dap4) {
                   if(sign == H5T_SGN_2) 
                      btp = new HDF5Int64(vname,vpath, dataset);
                   else
                      btp = new HDF5UInt64(vname,vpath, dataset);
                }
                else {
                    /*string err_msg = "Unsupported HDF5 64-bit Integer type:";
                    throw BESSyntaxUserError(err_msg,__FILE__,__LINE__);*/
                    string err_msg;
                    if(sign == H5T_SGN_2)
                        err_msg = invalid_type_error_msg("Int64");
                    else
                        err_msg = invalid_type_error_msg("UInt64");

                    throw BESSyntaxUserError(err_msg,__FILE__,__LINE__);
                }
            }
        }
            break;

        case H5T_FLOAT:
        {
            size = H5Tget_size(datatype);
            BESDEBUG("h5", "=Get_bt() H5T_FLOAT size = " << size << endl);

	    if (size == 0) {
                throw InternalErr(__FILE__, __LINE__, "cannot return the size of the datatype");
            }
            else if (size == 4) {
                btp = new HDF5Float32(vname,vpath, dataset);
            }
            else if (size == 8) {
                btp = new HDF5Float64(vname,vpath, dataset);
            }
        }
            break;

        case H5T_STRING:
            btp = new HDF5Str(vname, vpath,dataset);
            break;

        // The array datatype is rarely,rarely used. So this
        // part of code is not reviewed.
#if 0
        case H5T_ARRAY: {
            BaseType *ar_bt = 0;
            try {
                BESDEBUG("h5",
                    "=Get_bt() H5T_ARRAY datatype = " << datatype
                    << endl);

                // Get the base datatype of the array
                hid_t dtype_base = H5Tget_super(datatype);
                ar_bt = Get_bt(vname, dataset, dtype_base);
                btp = new HDF5Array(vname, dataset, ar_bt);
                delete ar_bt; ar_bt = 0;

                // Set the size of the array.
                int ndim = H5Tget_array_ndims(datatype);
                size = H5Tget_size(datatype);
                int nelement = 1;

		if (dtype_base < 0) {
                throw InternalErr(__FILE__, __LINE__, "cannot return the base datatype");
 	        }
		if (ndim < 0) {
                throw InternalErr(__FILE__, __LINE__, "cannot return the rank of the array datatype");
                }
		if (size == 0) {
                throw InternalErr(__FILE__, __LINE__, "cannot return the size of the datatype");
                }
                BESDEBUG(cerr
                    << "=Get_bt()" << " Dim = " << ndim
                    << " Size = " << size
                    << endl);

                hsize_t size2[DODS_MAX_RANK];
                if(H5Tget_array_dims(datatype, size2) < 0){
                    throw
                        InternalErr(__FILE__, __LINE__,
                                    string("Could not get array dims for: ")
                                      + vname);
                }


                HDF5Array &h5_ar = static_cast < HDF5Array & >(*btp);
                for (int dim_index = 0; dim_index < ndim; dim_index++) {
                    h5_ar.append_dim_ll(size2[dim_index]);
                    BESDEBUG("h5", "=Get_bt() " << size2[dim_index] << endl);
                    nelement = nelement * size2[dim_index];
                }

                h5_ar.set_did(dt_inst.dset);
                // Assign the array datatype id.
                h5_ar.set_tid(datatype);
                h5_ar.set_memneed(size);
                h5_ar.set_numdim(ndim);
                h5_ar.set_numelm(nelement);
                h5_ar.set_size(nelement);
                h5_ar.d_type = H5Tget_class(dtype_base); 
		if (h5_ar.d_type == H5T_NO_CLASS){
		    throw InternalErr(__FILE__, __LINE__, "cannot return the datatype class identifier");
		}
            }
            catch (...) {
                if( ar_bt ) delete ar_bt;
                if( btp ) delete btp;
                throw;
            }
            break;
        }
#endif

        // Reference map to DAP URL, check the technical note.
        case H5T_REFERENCE:
            btp = new HDF5Url(vname, vpath,dataset);
            break;
        
        default:
            throw InternalErr(__FILE__, __LINE__,
                              string("Unsupported HDF5 type:  ") + vname);
        }
    }
    catch (...) {
        if( btp ) delete btp;
        throw;
    }

    if (!btp)
        throw InternalErr(__FILE__, __LINE__,
                          string("Could not make a DAP variable for: ")
                          + vname);
                                                  
    BESDEBUG("h5", "<Get_bt()" << endl);
    return btp;
}


///////////////////////////////////////////////////////////////////////////////
/// \fn Get_structure(const string& varname, const string &dataset,
///     hid_t datatype)
/// returns a pointer of structure type. An exception is thrown if an error
/// is encountered.
/// 
/// This function will create a new DODS object that corresponds to HDF5
/// compound dataset and return a pointer of a new structure object of DODS.
///
/// \param varname object name
/// \param dataset name of the dataset from which this structure created
/// \param datatype datatype id
/// \param is_dap4 whether this is for dap4
/// \return pointer to Structure type
///
///////////////////////////////////////////////////////////////////////////////
//static Structure *Get_structure(const string &varname,
Structure *Get_structure(const string &varname,const string &vpath,
                                const string &dataset,
                                hid_t datatype,bool is_dap4)
{
    HDF5Structure *structure_ptr = nullptr;
    char* memb_name = nullptr;
    hid_t memb_type = -1;

    BESDEBUG("h5", ">Get_structure()" << datatype << endl);

    if (H5Tget_class(datatype) != H5T_COMPOUND)
        throw InternalErr(__FILE__, __LINE__,
                          string("Compound-to-structure mapping error for ")
                          + varname);

    try {
        structure_ptr = new HDF5Structure(varname, vpath, dataset);

        // Retrieve member types
        int nmembs = H5Tget_nmembers(datatype);
        BESDEBUG("h5", "=Get_structure() has " << nmembs << endl);
        if (nmembs < 0){
            throw InternalErr(__FILE__, __LINE__, "cannot retrieve the number of elements");
        }
        for (int i = 0; i < nmembs; i++) {
            memb_name = H5Tget_member_name(datatype, i);
            H5T_class_t memb_cls = H5Tget_member_class(datatype, i);
            memb_type = H5Tget_member_type(datatype, i);
            if (memb_name == nullptr){
                throw InternalErr(__FILE__, __LINE__, "cannot retrieve the name of the member");
            }
            if ((memb_cls < 0) || (memb_type < 0)) {
                throw InternalErr(__FILE__, __LINE__,
                                  string("Type mapping error for ")
                                  + string(memb_name) );
            }
            
            if (memb_cls == H5T_COMPOUND) {
                Structure *s = Get_structure(memb_name, memb_name, dataset, memb_type,is_dap4);
                structure_ptr->add_var(s);
                delete s; s = nullptr;
            } 
            else if(memb_cls == H5T_ARRAY) {

                BaseType *ar_bt = nullptr;
                BaseType *btp   = nullptr;
                Structure *s    = nullptr;
                hid_t     dtype_base = 0;

                try {

                    // Get the base memb_type of the array
                    dtype_base = H5Tget_super(memb_type);

                    // Set the size of the array.
                    int ndim = H5Tget_array_ndims(memb_type);
                    size_t size = H5Tget_size(memb_type);
                    int64_t nelement = 1;

                    if (dtype_base < 0) {
                        throw InternalErr(__FILE__, __LINE__, "cannot return the base memb_type");
                    }
                    if (ndim < 0) {
                        throw InternalErr(__FILE__, __LINE__, "cannot return the rank of the array memb_type");
                    }
                    if (size == 0) {
                        throw InternalErr(__FILE__, __LINE__, "cannot return the size of the memb_type");
                    }

                    hsize_t size2[DODS_MAX_RANK];
                    if(H5Tget_array_dims(memb_type, size2) < 0){
                        throw
                        InternalErr(__FILE__, __LINE__,
                                    string("Could not get array dims for: ")
                                      + string(memb_name));
                    }

                    H5T_class_t array_memb_cls = H5Tget_class(dtype_base);
                    if(array_memb_cls == H5T_NO_CLASS) {
                        throw InternalErr(__FILE__, __LINE__,
                                  string("cannot get the correct class for compound type member")
                                  + string(memb_name));
                    }
                    if(H5T_COMPOUND == array_memb_cls) {

                        s = Get_structure(memb_name, memb_name,dataset, dtype_base,is_dap4);
                        auto h5_ar = new HDF5Array(memb_name, dataset, s);
                    
                        for (int dim_index = 0; dim_index < ndim; dim_index++) {
                            h5_ar->append_dim_ll(size2[dim_index]);
                            nelement = nelement * size2[dim_index];
                        }

                        h5_ar->set_memneed(size);
                        h5_ar->set_numdim(ndim);
                        h5_ar->set_numelm(nelement);
                        h5_ar->set_length(nelement);

                        structure_ptr->add_var(h5_ar);
                        delete h5_ar;
	
                    }
                    else if (H5T_INTEGER == array_memb_cls || H5T_FLOAT == array_memb_cls || H5T_STRING == array_memb_cls) { 
                        ar_bt = Get_bt(memb_name, memb_name,dataset, dtype_base,is_dap4);
                        auto h5_ar = new HDF5Array(memb_name,dataset,ar_bt);
                    
                        for (int dim_index = 0; dim_index < ndim; dim_index++) {
                            h5_ar->append_dim(size2[dim_index]);
                            nelement = nelement * size2[dim_index];
                        }

                        h5_ar->set_memneed(size);
                        h5_ar->set_numdim(ndim);
                        h5_ar->set_numelm(nelement);
                        h5_ar->set_length(nelement);

                        structure_ptr->add_var(h5_ar);
                        delete h5_ar;
                    }
                    if( ar_bt ) delete ar_bt;
                    if( btp ) delete btp;
                    if(s) delete s;
                    H5Tclose(dtype_base);

                }
                catch (...) {
                    if( ar_bt ) delete ar_bt;
                    if( btp ) delete btp;
                    if(s) delete s;
                    H5Tclose(dtype_base);
                    throw;
                }

            }
            else if (memb_cls == H5T_INTEGER || memb_cls == H5T_FLOAT || memb_cls == H5T_STRING)  {
                BaseType *bt = Get_bt(memb_name, memb_name,dataset, memb_type,is_dap4);
                structure_ptr->add_var(bt);
                delete bt; bt = nullptr;
            }
            else {
                free(memb_name);
                memb_name = nullptr;
                throw InternalErr(__FILE__, __LINE__, "unsupported field datatype inside a compound datatype");
            }
            // Caller needs to free the memory allocated by the library for memb_name.
            if(memb_name != nullptr)
                free(memb_name);
        }
    }
    catch (...) {
        if( structure_ptr ) 
            delete structure_ptr;
        if(memb_name!= nullptr) 
            free(memb_name);
        if(memb_type != -1)
            H5Tclose(memb_type);
        throw;
    }

    BESDEBUG("h5", "<Get_structure()" << endl);

    return structure_ptr;
}

///////////////////////////////////////////////////////////////////////////////
/// \fn Get_structure(const string& varname, const string &dataset,
///     hid_t datatype)
/// returns a pointer of structure type. An exception is thrown if an error
/// is encountered.
/// 
/// This function will create a new DODS object that corresponds to HDF5
/// compound dataset and return a pointer of a new structure object of DODS.
///
/// \param varname object name
/// \param dataset name of the dataset from which this structure created
/// \param datatype datatype id
/// \param is_dap4 whether this is for dap4
/// \return pointer to Structure type
///
///////////////////////////////////////////////////////////////////////////////

// Function to use H5OVISIT to check if dimension scale attributes exist.
bool check_dimscale(hid_t fileid) {

    bool ret_value = false;
    herr_t ret_o= H5OVISIT(fileid, H5_INDEX_NAME, H5_ITER_INC, visit_obj_cb, nullptr);
    if(ret_o < 0)
        throw InternalErr(__FILE__, __LINE__, "H5OVISIT fails");
    else 
       ret_value =(ret_o >0)?true:false;

    return ret_value;
}

static int
visit_obj_cb(hid_t  group_id, const char *name, const H5O_info_t *oinfo,
    void *_op_data)
{

    int ret_value = 0;
    if(oinfo->type == H5O_TYPE_DATASET) {

        hid_t dataset = -1;
        dataset = H5Dopen2(group_id,name,H5P_DEFAULT);
        if(dataset <0) 
            throw InternalErr(__FILE__, __LINE__, "H5Dopen2 fails in the H5OVISIT call back function.");

        hid_t dspace = -1;
        dspace = H5Dget_space(dataset);
        if(dspace <0) {
            H5Dclose(dataset);
            throw InternalErr(__FILE__, __LINE__, "H5Dget_space fails in the H5OVISIT call back function.");
        }

        // We only support netCDF-4 like dimension scales, that is the dimension scale dataset is 1-D dimension.
        if(H5Sget_simple_extent_ndims(dspace) == 1) {
            try {
                if(true == has_dimscale_attr(dataset)) 
                    ret_value = 1;
            }
            catch(...) {
                H5Sclose(dspace);
                H5Dclose(dataset);
                throw;
            }

#if 0
            //vector<bool>dim_attr_mark;
            //dim_attr_mark.resize(4);
            //bool dim_attr_mark[4];
            int dim_attr_mark[4];
            for(int i =0;i<4;i++)
                dim_attr_mark[i] = 0;
            //int count = 0;
            // Check if having "class = DIMENSION_SCALE" and REFERENCE_LIST attributes.
            //herr_t ret = H5Aiterate2(dataset, H5_INDEX_NAME, H5_ITER_INC, nullptr, attr_info, &count);
            //herr_t ret = H5Aiterate2(dataset, H5_INDEX_NAME, H5_ITER_INC, nullptr, attr_info, dim_attr_mark.data());
            herr_t ret = H5Aiterate2(dataset, H5_INDEX_NAME, H5_ITER_INC, nullptr, attr_info, dim_attr_mark);
            if(ret < 0) {
                H5Sclose(dspace);
                H5Dclose(dataset);
                throw InternalErr(__FILE__, __LINE__, "H5Aiterate2 fails in the H5OVISIT call back function.");
            }

    BESDEBUG("h5", "<dset name is " << name <<endl);
    //BESDEBUG("h5", "<count is " << count <<endl);
            // Find it.
            if (dim_attr_mark[0] && dim_attr_mark[1]){
            //if (2==count || 3==count) {
                if(dspace != -1)
                    H5Sclose(dspace);
                if(dataset != -1)
                    H5Dclose(dataset);
                return 1;
            //}
            }
#endif
        }
        if(dspace != -1)
            H5Sclose(dspace);
        if(dataset != -1)
            H5Dclose(dataset);
    }
    return ret_value;
}

bool has_dimscale_attr(hid_t dataset) {

    bool ret_value = false;
    string dimscale_attr_name="CLASS";
    string dimscale_attr_value="DIMENSION_SCALE";
    htri_t dimscale_attr_exist = H5Aexists_by_name(dataset,".",dimscale_attr_name.c_str(),H5P_DEFAULT);
    if(dimscale_attr_exist <0) 
        throw InternalErr(__FILE__, __LINE__, "H5Aexists_by_name fails when checking the CLASS attribute.");
    else if(dimscale_attr_exist > 0) {//Attribute CLASS exists

        hid_t attr_id =   -1;
        hid_t atype_id =  -1;

        // Open the attribute
        attr_id = H5Aopen(dataset,dimscale_attr_name.c_str(), H5P_DEFAULT);
        if(attr_id < 0) 
            throw InternalErr(__FILE__, __LINE__, "H5Aopen fails in the attr_info call back function.");

        // Get attribute datatype and dataspace
        atype_id  = H5Aget_type(attr_id);
        if(atype_id < 0) {
            H5Aclose(attr_id);
            throw InternalErr(__FILE__, __LINE__, "H5Aget_type fails in the attr_info call back function.");
        }

        try {
            // Check if finding the attribute CLASS and the value is "DIMENSION_SCALE".
            if (H5T_STRING == H5Tget_class(atype_id)) 
                ret_value = check_str_attr_value(attr_id,atype_id,dimscale_attr_value,false);
        }
        catch(...) {
            if(atype_id != -1)
                H5Tclose(atype_id);
            if(attr_id != -1)
                H5Aclose(attr_id);
            throw;
        }
        // Close IDs.
        if(atype_id != -1)
            H5Tclose(atype_id);
        if(attr_id != -1)
            H5Aclose(attr_id);
    }
    return ret_value;

}
#if 0
///////////////////////////////////////////////////////////////////////////////
/// \fn attr_info(hid_t loc_id, const char* name, const H5A_info_t* void*opdata)
///
/// This function is the call back function for H5Aiterate2 to see if having the dimension scale attributes.
///
/// \param[in]  loc_id  object id for iterating the attributes of this object
/// \param[in]  name HDF5 attribute name returned by H5Aiterate2
/// \param[in]  ainfo pointer to the HDF5 attribute's info struct
/// \param[in] opdata pointer to the operator data passed to H5Aiterate2
/// \return returns a non-negative value if successful
/// \throw InternalError 
///////////////////////////////////////////////////////////////////////////////

static herr_t
attr_info_dimscale(hid_t loc_id, const char *name, const H5A_info_t *ainfo, void *opdata)
{
    int *countp = (int*)opdata;

    hid_t attr_id =   -1;
    hid_t atype_id =  -1;

    // Open the attribute
    attr_id = H5Aopen(loc_id, name, H5P_DEFAULT);
    if(attr_id < 0) 
        throw InternalErr(__FILE__, __LINE__, "H5Aopen fails in the attr_info call back function.");

    // Get attribute datatype and dataspace
    atype_id  = H5Aget_type(attr_id);
    if(atype_id < 0) {
        H5Aclose(attr_id);
        throw InternalErr(__FILE__, __LINE__, "H5Aget_type fails in the attr_info call back function.");
    }

    try {

#if 0
        // If finding the "REFERENCE_LIST", increases the countp.
        if ((H5T_COMPOUND == H5Tget_class(atype_id)) && (strcmp(name,"REFERENCE_LIST")==0)) {
             //(*countp)++;
             *dimattr_p = 1;
             //*dimattr_p = true;
        }
#endif
        // Check if finding the attribute CLASS and the value is "DIMENSION_SCALE".
        if (H5T_STRING == H5Tget_class(atype_id)) {
            if (strcmp(name,"CLASS") == 0) {
                string dim_scale_mark = "DIMENSION_SCALE";
                bool is_dim_scale = check_str_attr_value(attr_id,atype_id,dim_scale_mark,false);
                if(true == is_dim_scale)
                    (*countp)++;
            }
        }
    }
    catch(...) {
        if(atype_id != -1)
            H5Tclose(atype_id);
        if(attr_id != -1)
            H5Aclose(attr_id);
        throw;
    }

    // Close IDs.
    if(atype_id != -1)
        H5Tclose(atype_id);
    if(attr_id != -1)
        H5Aclose(attr_id);

    return 0;
}

#endif
///////////////////////////////////////////////////////////////////////////////
/// \fn attr_info(hid_t loc_id, const char* name, const H5A_info_t* void*opdata)
///
/// This function is the call back function for H5Aiterate2 to see if having the dimension scale attributes.
///
/// \param[in]  loc_id  object id for iterating the attributes of this object
/// \param[in]  name HDF5 attribute name returned by H5Aiterate2
/// \param[in]  ainfo pointer to the HDF5 attribute's info struct
/// \param[in] opdata pointer to the operator data passed to H5Aiterate2
/// \return returns a non-negative value if successful
/// \throw InternalError 
///////////////////////////////////////////////////////////////////////////////

static herr_t
attr_info_dimscale(hid_t loc_id, const char *name, const H5A_info_t *ainfo, void *opdata)
{

#if 0
    //bool *countp = (bool*)opdata;
    //bool *dimattr_p = (bool*)opdata;
#endif

    int *dimattr_p = (int*)opdata;

#if 0
    bool has_reference_list = false;
    bool has_dimscale = false;
    bool has_name_as_var = false;
    bool has_name_as_nc4_purdim = false;
#endif


    hid_t attr_id =   -1;
    hid_t atype_id =  -1;

    // Open the attribute
    attr_id = H5Aopen(loc_id, name, H5P_DEFAULT);
    if(attr_id < 0) 
        throw InternalErr(__FILE__, __LINE__, "H5Aopen fails in the attr_info call back function.");

    // Get attribute datatype and dataspace
    atype_id  = H5Aget_type(attr_id);
    if(atype_id < 0) {
        H5Aclose(attr_id);
        throw InternalErr(__FILE__, __LINE__, "H5Aget_type fails in the attr_info call back function.");
    }

    try {

//#if 0
        // If finding the "REFERENCE_LIST", increases the countp.
        if ((H5T_COMPOUND == H5Tget_class(atype_id)) && (strcmp(name,"REFERENCE_LIST")==0)) {
             //(*countp)++;
             *dimattr_p = 1;
             //*dimattr_p = true;
        }
//#endif
        // Check if finding the CLASS attribute.
        if (H5T_STRING == H5Tget_class(atype_id)) {
            if (strcmp(name,"NAME") == 0) {

                string pure_dimname_mark = "This is a netCDF dimension but not a netCDF variable";
                bool is_pure_dim = check_str_attr_value(attr_id,atype_id,pure_dimname_mark,true);

                BESDEBUG("h5","pure dimension name yes" << is_pure_dim <<endl);
                if(true == is_pure_dim)
                    *(dimattr_p+1) =1;
                    //*(dimattr_p+2) =true;
                else {
                    // netCDF save the variable name in the "NAME" attribute.
                    // We need to retrieve the variable name first.
                    ssize_t objnamelen = -1;
                    if ((objnamelen= H5Iget_name(loc_id,nullptr,0))<=0) {
                        string msg = "Cannot obtain the variable name length." ;
                        throw InternalErr(__FILE__,__LINE__,msg);
                    }
                    vector<char> objname;
                    objname.resize(objnamelen+1);
                    if ((objnamelen= H5Iget_name(loc_id,objname.data(),objnamelen+1))<=0) {
                        string msg = "Cannot obtain the variable name." ;
                        throw InternalErr(__FILE__,__LINE__,msg);
                    }

                    string objname_str = string(objname.begin(),objname.end());

                    // Must trim the string delimter.
                    objname_str = objname_str.substr(0,objnamelen);
                    // Remove the path
                    string normal_dimname_mark = objname_str.substr(objname_str.find_last_of("/")+1);
                    bool is_normal_dim = check_str_attr_value(attr_id,atype_id,normal_dimname_mark,false);
                    if(true == is_normal_dim)
                        *(dimattr_p+2) = 1;
                        //*(dimattr_p+3) = true;
                }
            }
        }

#if 0
            H5T_str_t str_pad = H5Tget_strpad(atype_id);

            hid_t aspace_id = -1;
            aspace_id = H5Aget_space(attr_id);
            if(aspace_id < 0) 
                throw InternalErr(__FILE__, __LINE__, "H5Aget_space fails in the attr_info call back function.");

            // CLASS is a variable-length string
            int ndims = H5Sget_simple_extent_ndims(aspace_id);
            hsize_t nelmts = 1;

            // if it is a scalar attribute, just define number of elements to be 1.
            if (ndims != 0) {

                vector<hsize_t> asize;
                vector<hsize_t> maxsize;
                asize.resize(ndims);
                maxsize.resize(ndims);

                // DAP applications don't care about the unlimited dimensions 
                // since the applications only care about retrieving the data.
                // So we don't check the maxsize to see if it is the unlimited dimension 
                // attribute.
                if (H5Sget_simple_extent_dims(aspace_id, asize.data(), maxsize.data())<0) {
                    H5Sclose(aspace_id);
                    throw InternalErr(__FILE__, __LINE__, "Cannot obtain the dim. info in the H5Aiterate2 call back function.");
                }

                // Return ndims and size[ndims]. 
                for (int j = 0; j < ndims; j++)
                    nelmts *= asize[j];
            } // if(ndims != 0)

            size_t ty_size = H5Tget_size(atype_id);
            if (0 == ty_size) {
                H5Sclose(aspace_id);
                throw InternalErr(__FILE__, __LINE__, "Cannot obtain the type size in the H5Aiterate2 call back function.");
            }

            size_t total_bytes = nelmts * ty_size;
            string total_vstring ="";
            if(H5Tis_variable_str(atype_id) > 0) {

                // Variable length string attribute values only store pointers of the actual string value.
                vector<char> temp_buf;
                temp_buf.resize(total_bytes);

                if (H5Aread(attr_id, atype_id, temp_buf.data()) < 0){
                    H5Sclose(aspace_id);
                    throw InternalErr(__FILE__,__LINE__,"Cannot read the attribute in the H5Aiterate2 call back function");
                }

                char *temp_bp = nullptr;
                temp_bp = temp_buf.data();
                char* onestring = nullptr;

                for (unsigned int temp_i = 0; temp_i <nelmts; temp_i++) {

                    // This line will assure that we get the real variable length string value.
                    onestring =*(char **)temp_bp;

                    if(onestring!= nullptr) 
                        total_vstring +=string(onestring);

                    // going to the next value.
                    temp_bp +=ty_size;
                }

                if ((temp_buf.data()) != nullptr) {
                    // Reclaim any VL memory if necessary.
                    if (H5Dvlen_reclaim(atype_id,aspace_id,H5P_DEFAULT,temp_buf.data()) < 0) {
                        H5Sclose(aspace_id);
                        throw InternalErr(__FILE__,__LINE__,"Cannot reclaim VL memory in the H5Aiterate2 call back function.");
                    }
                }

            }
            else {// Fixed-size string, need to retrieve the string value.

                // string attribute values 
                vector<char> temp_buf;
                temp_buf.resize(total_bytes);
                if (H5Aread(attr_id, atype_id, temp_buf.data()) < 0){
                    H5Sclose(aspace_id);
                    throw InternalErr(__FILE__,__LINE__,"Cannot read the attribute in the H5Aiterate2 call back function");
                }
                string temp_buf_string(temp_buf.begin(),temp_buf.end());
                total_vstring = temp_buf_string.substr(0,total_bytes);

                // Note: we need to remove the string pad or term to find DIMENSION_SCALE.
                if(str_pad != H5T_STR_ERROR) 
                    total_vstring = total_vstring.substr(0,total_vstring.size()-1);
            }
           
            // Close attribute data space ID.
            if(aspace_id != -1)
                H5Sclose(aspace_id);
            if(total_vstring == "DIMENSION_SCALE"){
                (*countp)++;
            }
#endif
        
    }
    catch(...) {
        if(atype_id != -1)
            H5Tclose(atype_id);
        if(attr_id != -1)
            H5Aclose(attr_id);
        throw;
    }

    // Close IDs.
    if(atype_id != -1)
        H5Tclose(atype_id);
    if(attr_id != -1)
        H5Aclose(attr_id);

    return 0;
}


///////////////////////////////////////////////////////////////////////////////
/// \fn obtain_dimnames(hid_t dset, int ndims, DS_t * dt_inst_ptr, vector<link_info_t> & hdf5_hls)
/// Obtain the dimension names of an HDF5 dataset and save the dimension names.
/// \param[in] dset   HDF5 dataset ID
/// \param[in] ndims  number of dimensions
/// \param[out] dt_inst_ptr  pointer to the dataset struct that saves the dim. names
/// \param[in/out] hdf5_hls  vector that stores the hard link info of this dimension.

///////////////////////////////////////////////////////////////////////////////
void obtain_dimnames(const hid_t file_id,hid_t dset,int ndims, DS_t *dt_inst_ptr,vector<link_info_t> & hdf5_hls, bool is_eos5) {

    htri_t has_dimension_list = -1;
    
    string dimlist_name = "DIMENSION_LIST";
    has_dimension_list = H5Aexists(dset,dimlist_name.c_str());

    if(has_dimension_list > 0 && ndims > 0) {

        hobj_ref_t rbuf;
        vector<hvl_t> vlbuf;
        vlbuf.resize(ndims);

        hid_t attr_id     = -1;
        hid_t atype_id    = -1;
        hid_t amemtype_id = -1;
        hid_t aspace_id   = -1;
        hid_t ref_dset    = -1;

        try {        
            attr_id = H5Aopen(dset,dimlist_name.c_str(),H5P_DEFAULT);
            if (attr_id <0 ) {
                string msg = "Cannot open the attribute " + dimlist_name + " of HDF5 dataset "+ string(dt_inst_ptr->name);
                throw InternalErr(__FILE__, __LINE__, msg);
            }

            atype_id = H5Aget_type(attr_id);
            if (atype_id <0) {
                string msg = "Cannot get the datatype of the attribute " + dimlist_name + " of HDF5 dataset "+ string(dt_inst_ptr->name);
                throw InternalErr(__FILE__, __LINE__, msg);
            }

            amemtype_id = H5Tget_native_type(atype_id, H5T_DIR_ASCEND);
            if (amemtype_id < 0) {
                string msg = "Cannot get the memory datatype of the attribute " + dimlist_name + " of HDF5 dataset "+ string(dt_inst_ptr->name);
                throw InternalErr(__FILE__, __LINE__, msg);
 
            }

            if (H5Aread(attr_id,amemtype_id,vlbuf.data()) <0)  {
                string msg = "Cannot obtain the referenced object for the variable  " + string(dt_inst_ptr->name);
                throw InternalErr(__FILE__, __LINE__, msg);
            }

            vector<char> objname;

            // The dimension names of variables will be the HDF5 dataset names dereferenced from the DIMENSION_LIST attribute.
            for (int i = 0; i < ndims; i++) {

                if(vlbuf[i].p == nullptr) {
                    stringstream sindex ;
                    sindex <<i;
                    string msg = "For variable " + string(dt_inst_ptr->name) + "; "; 
                    msg = msg + "the dimension of which the index is "+ sindex.str() + " doesn't exist. "; 
                    throw InternalErr(__FILE__, __LINE__, msg);
                }

                rbuf =((hobj_ref_t*)vlbuf[i].p)[0];

                if ((ref_dset = H5RDEREFERENCE(attr_id, H5R_OBJECT, &rbuf)) < 0) {
                    string msg = "Cannot dereference from the DIMENSION_LIST attribute  for the variable " + string(dt_inst_ptr->name);
                    throw InternalErr(__FILE__, __LINE__, msg);
                }

                ssize_t objnamelen = -1;
                if ((objnamelen= H5Iget_name(ref_dset,nullptr,0))<=0) {
                    string msg = "Cannot obtain the dimension name length for the variable " + string(dt_inst_ptr->name);
                    throw InternalErr(__FILE__,__LINE__,msg);
                }

                objname.resize(objnamelen+1);
                if ((objnamelen= H5Iget_name(ref_dset,objname.data(),objnamelen+1))<=0) {
                    H5Dclose(ref_dset);
                    string msg = "Cannot obtain the dimension name for the variable " + string(dt_inst_ptr->name);
                    throw InternalErr(__FILE__,__LINE__,msg);
                }

                auto objname_str = string(objname.begin(),objname.end());

                // Must trim the string delimter.
                string trim_objname = objname_str.substr(0,objnamelen);
 
                // We need to check if there are hardlinks for this variable. 
                // If yes, we need to find the hardlink that has the shortest path and at the ancestor group
                // of all links.
                H5O_info_t obj_info;
                if(H5OGET_INFO(ref_dset,&obj_info)<0) {
                    H5Dclose(ref_dset);
                    string msg = "Cannot obtain the object info for the dimension variable " + objname_str;
                    throw InternalErr(__FILE__,__LINE__,msg);
                }
          
                // This dimension indeed has hard links.
                if(obj_info.rc > 1) {

                    // 1. Search the hdf5_hls to see if the address is inside
                    //    if yes, 
                    //       obtain the hard link which has the shortest path, use this as the dimension name.
                    //    else 
                    //       search all the hardlinks with the callback.
                    //       obtain the shortest path, add this to hdf5_hls.

                    bool link_find = false;

                    // If finding the object in the hdf5_hls, obtain the hardlink and make it the dimension name(trim_objname).
                    for (const auto & hdf5_hl:hdf5_hls) {
#if (H5_VERS_MAJOR == 1 && ((H5_VERS_MINOR == 12) || (H5_VERS_MINOR == 13)))
                        int token_cmp = -1;                                                                                 
                        if(H5Otoken_cmp(ref_dset,&(obj_info.token),&(hdf5_hl.link_addr),&token_cmp) <0)                   
                            throw InternalErr(__FILE__,__LINE__,"H5Otoken_cmp failed");
                        if(!token_cmp) {                    
#else
                        if(obj_info.addr == hdf5_hl.link_addr) { 
#endif
                            trim_objname = '/'+hdf5_hl.slink_path;
                            link_find = true;
                            break;
                        }
                    }

                    // The hard link is not in the hdf5_hls, need to iterate all objects and find those hardlinks.
                    if(link_find == false) {

#if (H5_VERS_MAJOR == 1 && ((H5_VERS_MINOR == 12) || (H5_VERS_MINOR == 13)))
                        typedef struct {
                            unsigned link_unvisited;
                            H5O_token_t  link_addr;
                            vector<string> hl_names;
                        } t_link_info_t;
#else
                        typedef struct {
                            unsigned link_unvisited;
                            haddr_t  link_addr;
                            vector<string> hl_names;
                        } t_link_info_t;
#endif
    
                        t_link_info_t t_li_info;
                        t_li_info.link_unvisited = obj_info.rc;

#if (H5_VERS_MAJOR == 1 && ((H5_VERS_MINOR == 12) || (H5_VERS_MINOR == 13)))
                        memcpy(&t_li_info.link_addr,&obj_info.token,sizeof(H5O_token_t));
#else
                        t_li_info.link_addr = obj_info.addr;
#endif

                        if(H5Lvisit(file_id, H5_INDEX_NAME, H5_ITER_NATIVE, visit_link_cb, (void*)&t_li_info) < 0) {
                            H5Dclose(ref_dset);
                            string err_msg;
                            err_msg = "Find all hardlinks: H5Lvisit failed to iterate all the objects";
                            throw InternalErr(__FILE__,__LINE__,err_msg);
                        }
#if 0
for(int i = 0; i<t_li_info.hl_names.size();i++)
        cerr<<"hl name is "<<t_li_info.hl_names[i] <<endl;
#endif
                  
                       string shortest_hl = obtain_shortest_ancestor_path(t_li_info.hl_names);
#if 0
//cerr<<"shortest_hl is "<<shortest_hl <<endl;
#endif
                       if(shortest_hl =="") {
                            H5Dclose(ref_dset);
                            string err_msg;
                            err_msg = "The shortest hardlink is not located under an ancestor group of all links.";
                            err_msg +="This is not supported by netCDF4 data model and the current Hyrax DAP4 implementation.";
                            throw InternalErr(__FILE__,__LINE__,err_msg);
                       }
    
                       // Save this link that holds the shortest path for future use.
                       link_info_t new_hdf5_hl;
#if (H5_VERS_MAJOR == 1 && ((H5_VERS_MINOR == 12) || (H5_VERS_MINOR == 13)))
                        memcpy(&new_hdf5_hl.link_addr,&obj_info.token,sizeof(H5O_token_t));
#else
                       new_hdf5_hl.link_addr = obj_info.addr;
#endif
                       new_hdf5_hl.slink_path = shortest_hl;
                       hdf5_hls.push_back(new_hdf5_hl);
                       trim_objname = '/'+shortest_hl;

                   }
                }

                // Need to save the dimension names STOP: ADD
                // If this is an HDF-EOS5 file and it is using the dimension scales, we need to change the 
                // non-alphanumeric/underscore characters inside the path and the name to underscore.
                if (is_eos5) {
                    string temp_orig_dim_name = trim_objname.substr(trim_objname.find_last_of("/")+1);
                    string temp_dim_name = handle_string_special_characters(temp_orig_dim_name);
                    string temp_dim_path = handle_string_special_characters_in_path(trim_objname);
                    dt_inst_ptr->dimnames.push_back(temp_dim_name);
                    dt_inst_ptr->dimnames_path.push_back(temp_dim_path);
                }
                else {
                    dt_inst_ptr->dimnames.push_back(trim_objname.substr(trim_objname.find_last_of("/")+1));
                    dt_inst_ptr->dimnames_path.push_back(trim_objname);
                }

                if(H5Dclose(ref_dset)<0) {
                    throw InternalErr(__FILE__,__LINE__,"Cannot close the HDF5 dataset in the function obtain_dimnames().");
                }
                objname.clear();
            }// for (vector<Dimension *>::iterator ird is var->dims.begin()
            if(vlbuf.empty()== false) {

                if ((aspace_id = H5Aget_space(attr_id)) < 0) {
                    string msg = "Cannot close the HDF5 attribute space successfully for <DIMENSION_LIST> of the variable "+string(dt_inst_ptr->name);
                    throw InternalErr(__FILE__,__LINE__,msg);
                }

                if (H5Dvlen_reclaim(amemtype_id,aspace_id,H5P_DEFAULT,(void*)vlbuf.data())<0) {
                    throw InternalErr(__FILE__,__LINE__,"Cannot reclaim the variable length memory in the function obtain_dimnames()");
                }

                H5Sclose(aspace_id);
           
            }

            H5Tclose(atype_id);
            H5Tclose(amemtype_id);
            H5Aclose(attr_id);
    
        }

        catch(...) {

            if(atype_id != -1)
                H5Tclose(atype_id);

            if(amemtype_id != -1)
                H5Tclose(amemtype_id);

            if(aspace_id != -1)
                H5Sclose(aspace_id);

            if(attr_id != -1)
                H5Aclose(attr_id);

            throw;
        }
 
    }
    return ;
}

void write_vlen_str_attrs(hid_t attr_id,hid_t ty_id, const DSattr_t * attr_inst_ptr,D4Attribute *d4_attr, AttrTable* d2_attr,bool is_dap4){

    BESDEBUG("h5","attribute name " << attr_inst_ptr->name <<endl);
    BESDEBUG("h5","attribute size " <<attr_inst_ptr->need <<endl);
    BESDEBUG("h5","attribute type size " <<(int)(H5Tget_size(ty_id))<<endl); 

    bool is_utf8_str = false;

    // Note: We don't need to handle DAP4 here since the utf8 flag for DAP4 can be set before coming to this function
    // See h5dmr.cc around the line 956.
    if (is_dap4 == false) {
        H5T_cset_t c_set_type = H5Tget_cset(ty_id);
        if (c_set_type < 0)
            throw InternalErr(__FILE__, __LINE__, "Cannot get hdf5 character set type for the attribute.");
        if (HDF5RequestHandler::get_escape_utf8_attr() == false && (c_set_type == 1))
            is_utf8_str = true;
    }

    hid_t temp_space_id = H5Aget_space(attr_id);
    BESDEBUG("h5","attribute calculated size "<<(int)(H5Tget_size(ty_id)) *(int)(H5Sget_simple_extent_npoints(temp_space_id)) <<endl);
    if(temp_space_id <0) {
        H5Tclose(ty_id);
        H5Aclose(attr_id);
        if(d4_attr)
            delete d4_attr;
        throw InternalErr(__FILE__, __LINE__, "unable to read HDF5 attribute data");
    }

    vector<char> temp_buf;
    // Variable length string attribute values only store pointers of the actual string value.
    temp_buf.resize((size_t)attr_inst_ptr->need);
                
    if (H5Aread(attr_id, ty_id, temp_buf.data()) < 0) {
        H5Tclose(ty_id);
        H5Aclose(attr_id);
        H5Sclose(temp_space_id);
        if(d4_attr)
            delete d4_attr;
        throw InternalErr(__FILE__, __LINE__, "unable to read HDF5 attribute data");
    }

    char *temp_bp;
    temp_bp = temp_buf.data();
    for (unsigned int temp_i = 0; temp_i <attr_inst_ptr->nelmts; temp_i++) {

        // This line will assure that we get the real variable length string value.
        char* onestring =*(char **)temp_bp;

        // Change the C-style string to C++ STD string just for easy appending the attributes in DAP.
        if (onestring !=nullptr) {
            string tempstring(onestring);
            if(true == is_dap4)
                d4_attr->add_value(tempstring);
	    else {
                if (is_utf8_str)
                    d2_attr->append_attr(attr_inst_ptr->name,"String",tempstring,true);
                else 
		    d2_attr->append_attr(attr_inst_ptr->name,"String",tempstring);
            }
        }

        temp_bp +=H5Tget_size(ty_id);
    }
    if (temp_buf.empty() != true) {

        // Reclaim any VL memory if necessary.
        herr_t ret_vlen_claim;
        ret_vlen_claim = H5Dvlen_reclaim(ty_id,temp_space_id,H5P_DEFAULT,temp_buf.data());
        if(ret_vlen_claim < 0){
            H5Tclose(ty_id);
            H5Aclose(attr_id);
            H5Sclose(temp_space_id);
            if(d4_attr)
                delete d4_attr;
            throw InternalErr(__FILE__, __LINE__, "Cannot reclaim the memory buffer of the HDF5 variable length string.");
        }
                 
        temp_buf.clear();
    }
    H5Sclose(temp_space_id);
}

bool check_str_attr_value(hid_t attr_id,hid_t atype_id,const string & value_to_compare,bool check_substr) {

    bool ret_value = false;

    H5T_str_t str_pad = H5Tget_strpad(atype_id);
    if(str_pad == H5T_STR_ERROR) 
        throw InternalErr(__FILE__, __LINE__, "Fail to obtain string pad.");

    hid_t aspace_id = -1;
    aspace_id = H5Aget_space(attr_id);
    if(aspace_id < 0) 
        throw InternalErr(__FILE__, __LINE__, "Fail to obtain attribute space.");

    int ndims = H5Sget_simple_extent_ndims(aspace_id);
    if(ndims < 0) {
        H5Sclose(aspace_id);
        throw InternalErr(__FILE__, __LINE__, "Fail to obtain number of dimensions.");
    }

    hsize_t nelmts = 1;

    // if it is a scalar attribute, just define number of elements to be 1.
    if (ndims != 0) {

        vector<hsize_t> asize;
        asize.resize(ndims);
        if (H5Sget_simple_extent_dims(aspace_id, asize.data(), nullptr)<0) {
            H5Sclose(aspace_id);
            throw InternalErr(__FILE__, __LINE__, "Fail to obtain the dimension info.");
        }

        // Return ndims and size[ndims]. 
        for (int j = 0; j < ndims; j++)
            nelmts *= asize[j];
    } // if(ndims != 0)

    size_t ty_size = H5Tget_size(atype_id);
    if (0 == ty_size) {
        H5Sclose(aspace_id);
        throw InternalErr(__FILE__, __LINE__, "Fail to obtain the type size.");
    }

    size_t total_bytes = nelmts * ty_size;
    string total_vstring ="";
    if(H5Tis_variable_str(atype_id) > 0) {

        // Variable length string attribute values only store pointers of the actual string value.
        vector<char> temp_buf;
        temp_buf.resize(total_bytes);

        if (H5Aread(attr_id, atype_id, temp_buf.data()) < 0){
            H5Sclose(aspace_id);
            throw InternalErr(__FILE__,__LINE__,"Fail to read the attribute.");
        }

        char *temp_bp = nullptr;
        temp_bp = temp_buf.data();

        for (unsigned int temp_i = 0; temp_i <nelmts; temp_i++) {

            // This line will assure that we get the real variable length string value.
            char* onestring =*(char **)temp_bp;

            if(onestring!= nullptr) 
                total_vstring +=string(onestring);

            // going to the next value.
            temp_bp +=ty_size;
        }

        if ((temp_buf.data()) != nullptr) {
            // Reclaim any VL memory if necessary.
            if (H5Dvlen_reclaim(atype_id,aspace_id,H5P_DEFAULT,temp_buf.data()) < 0) {
                H5Sclose(aspace_id);
                throw InternalErr(__FILE__,__LINE__,"Fail to reclaim VL memory.");
            }
        }

    }
    else {// Fixed-size string, need to retrieve the string value.

        // string attribute values 
        vector<char> temp_buf;
        temp_buf.resize(total_bytes);
        if (H5Aread(attr_id, atype_id, temp_buf.data()) < 0){
            H5Sclose(aspace_id);
            throw InternalErr(__FILE__,__LINE__,"Fail to read the attribute.");
        }
        string temp_buf_string(temp_buf.begin(),temp_buf.end());
        total_vstring = temp_buf_string.substr(0,total_bytes);

        // Note: we need to remove the string pad or term to find DIMENSION_SCALE.
        if(str_pad != H5T_STR_ERROR) 
            total_vstring = total_vstring.substr(0,total_vstring.size()-1);
    }
           
    // Close attribute data space ID.
    if(aspace_id != -1)
        H5Sclose(aspace_id);

    if(false == check_substr) {
        if(total_vstring == value_to_compare)
            ret_value = true;
    }
    else {
        if(total_vstring.size()>=value_to_compare.size()) {
            if( 0 == total_vstring.compare(0,value_to_compare.size(),value_to_compare))
                ret_value = true;
        }
    }
    return ret_value;
}

// Call back function used by H5Lvisit that iterates all HDF5 objects.
static int 
visit_link_cb(hid_t  group_id, const char *name, const H5L_info_t *linfo,
    void *_op_data)
{
#if (H5_VERS_MAJOR == 1 && ((H5_VERS_MINOR == 12) || (H5_VERS_MINOR == 13)))
     typedef struct {
        unsigned link_unvisited;
        H5O_token_t  link_addr;
        vector<string> hl_names;
     } t_link_info_t;
#else
    typedef struct {
        unsigned link_unvisited;
        haddr_t link_addr;
        vector<string> hl_names;
    } t_link_info_t;
#endif
   
    t_link_info_t *op_data = (t_link_info_t *)_op_data;
    int ret = 0;

    // We only need the hard link info.
    if(linfo->type == H5L_TYPE_HARD) {
#if (H5_VERS_MAJOR == 1 && ((H5_VERS_MINOR == 12) || (H5_VERS_MINOR == 13)))
    int token_cmp = -1;
    if(H5Otoken_cmp(group_id,&(op_data->link_addr),&(linfo->u.token),&token_cmp) <0)
        throw InternalErr(__FILE__,__LINE__,"H5Otoken_cmp failed");
    if(!token_cmp) {
#else
        if(op_data->link_addr == linfo->u.address) {
#endif
            op_data->link_unvisited = op_data->link_unvisited -1;
            string tmp_str(name,name+strlen(name));
            op_data->hl_names.push_back(tmp_str);
            // Once visiting all hard links, stop. 
            if(op_data->link_unvisited == 0) 
                ret = 1;
        }

    }
    return ret;
 
}

// Obtain the shortest path of all hard links of the object.
std::string obtain_shortest_ancestor_path(const std::vector<std::string> & hls) {

    vector<string> hls_path;
    char slash = '/';
    bool hl_under_root = false;
    string ret_str ="";
    unsigned i = 0;

    for (i= 0; i<hls.size(); i++) {

        size_t path_pos = hls[i].find_last_of(slash);

        // The hard link may be under the root group, 
        // This is the shortest path, we will return this path.
        if(path_pos == std::string::npos) {
            //Found
            hl_under_root = true;
            break; 
        }
        else {
            string tmp_str = hls[i].substr(0,path_pos+1);
            hls_path.push_back(tmp_str);
        }
    }

    if(hl_under_root)
        ret_str =  hls[i];

    else {
        // We just need to find the minimum size.
        unsigned short_path_index = 0;
        unsigned min_path_size = hls_path[0].size();

        // Find the shortest path index
        for (unsigned j = 1; j <hls_path.size();j++) {
            if(min_path_size>hls_path[j].size()) {
                min_path_size = hls_path[j].size();
                short_path_index = j;
            }
        }
        string tmp_sp = hls_path[short_path_index];
        ret_str = hls[short_path_index];

        //check if all hardlinks have a common ancestor link
        // If not, set the return value be the empty string.
        for (const auto &hl_p:hls_path) {
            if(hl_p.find(tmp_sp)!=0) {
                ret_str ="";
                break;               
            }
        }       
    }
    return ret_str;
    
}

// change a non-alphanumeric character to an underscore(_)
string handle_string_special_characters(string &s) {

    if ("" == s) return s;
    string insertString(1, '_');
    
    // Always start with _ if the first character is not a letter
    if (true == isdigit(s[0])) s.insert(0, insertString);
    
    for (unsigned int i = 0; i < s.size(); i++)
        if ((false == isalnum(s[i])) && (s[i] != '_')) s[i] = '_';

    return s;

}

string handle_string_special_characters_in_path(const string &instr) {

    string outstr;
    char sep='/';

    size_t start_sep_pos = 0;
    size_t sep_pos;
    while ((sep_pos =instr.find(sep,start_sep_pos))!=string::npos) {

        string temp_str;
        // Either  "//" or "/?../" between the '/'s. 
        if (sep_pos > start_sep_pos) {
            temp_str = instr.substr(start_sep_pos,sep_pos-start_sep_pos);
            outstr = outstr+handle_string_special_characters(temp_str)+sep;
        }
        else
            outstr = outstr+sep;
        start_sep_pos = sep_pos+1;
        if (instr.size() <=start_sep_pos)
            break;

    }
    //The last part of the string or the string without / need to be handled.
    if (start_sep_pos == 0) {
        string temp_str= instr;
        outstr = handle_string_special_characters(temp_str);
    }
    else if(instr.size() >start_sep_pos) {
        string temp_str = instr.substr(start_sep_pos);
        outstr = outstr + handle_string_special_characters(temp_str);
    }

    return outstr;
}

string invalid_type_error_msg(
        const string& var_type
){
    stringstream msg;

    msg << "Your request was for a response that uses the DAP2 data model. ";
    msg << "This dataset contains variables whose data type ( "<< var_type << " ) is not compatible with that data model, causing this request to FAIL. ";
    msg << "To access this dataset ask for the DAP4 binary response encoding.";

    return msg.str();
}
