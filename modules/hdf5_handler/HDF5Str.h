// This file is part of hdf5_handler a HDF5 file handler for the OPeNDAP
// data server.

// Author: Hyo-Kyung Lee <hyoklee@hdfgroup.org> and Muqun Yang
// <myang6@hdfgroup.org> 

// Copyright (c) 2009-2016 The HDF Group, Inc. and OPeNDAP, Inc.
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

#ifndef _hdf5str_h
#define _hdf5str_h 

#include <string>
#include <H5Ipublic.h>

#include <limits.h>

#if 0
#ifndef STR_FLAG
#define STR_FLAG 1
#endif

#ifndef STR_NOFLAG
#define STR_NOFLAG 0
#endif
#endif

#include <libdap/Str.h>
#include "h5get.h"


////////////////////////////////////////////////////////////////////////////////
/// \file HDF5Str.h
///
/// \brief This class that translates HDF5 string into DAP string for the default option.
/// 
/// \author Hyo-Kyung Lee   (hyoklee@hdfgroup.org)
/// \author Kent Yang       (myang6@hdfgroup.org)
/// \author James Gallagher (jgallagher@opendap.org)
///
/// Copyright (c) 2007-2016 HDF Group
/// 
/// All rights reserved.
////////////////////////////////////////////////////////////////////////////////
class HDF5Str:public libdap::Str {
 private:
    std::string var_path;

 public:

    /// Constructor
    HDF5Str(const std::string &n, const std::string &vpath, const std::string &d);
    ~ HDF5Str() override = default;

    /// Clone this instance.
    /// 
    /// Allocate a new instance and copy *this into it. This method must
    /// perform a deep copy.
    /// \return A newly allocated copy of this class    
    libdap::BaseType *ptr_duplicate() override;

    /// Reads HDF5 string data into local buffer  
    bool read() override;


};

#endif
