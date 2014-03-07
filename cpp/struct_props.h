/*
 * This file is protected by Copyright. Please refer to the COPYRIGHT file distributed with this
 * source distribution.
 *
 * This file is part of REDHAWK Basic Components TuneFilterDecimate.
 *
 * REDHAWK Basic Components TuneFilterDecimate is free software: you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * REDHAWK Basic Components TuneFilterDecimate is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this
 * program.  If not, see http://www.gnu.org/licenses/.
 */
#ifndef STRUCTPROPS_H
#define STRUCTPROPS_H

/*******************************************************************************************

    AUTO-GENERATED CODE. DO NOT MODIFY

*******************************************************************************************/
#include <ossie/CorbaUtils.h>

struct filterProps_struct {
    filterProps_struct ()
    {
        FFT_size = 128;
        TransitionWidth = 800;
        Ripple = 0.01;
    };

    static std::string getId() {
        return std::string("filterProps");
    };

    CORBA::ULong FFT_size;
    double TransitionWidth;
    double Ripple;
};

inline bool operator>>= (const CORBA::Any& a, filterProps_struct& s) {
    CF::Properties* temp;
    if (!(a >>= temp)) return false;
    CF::Properties& props = *temp;
    for (unsigned int idx = 0; idx < props.length(); idx++) {
        if (!strcmp("FFT_size", props[idx].id)) {
            if (!(props[idx].value >>= s.FFT_size)) return false;
        }
        else if (!strcmp("TransitionWidth", props[idx].id)) {
            if (!(props[idx].value >>= s.TransitionWidth)) return false;
        }
        else if (!strcmp("Ripple", props[idx].id)) {
            if (!(props[idx].value >>= s.Ripple)) return false;
        }
    }
    return true;
};

inline void operator<<= (CORBA::Any& a, const filterProps_struct& s) {
    CF::Properties props;
    props.length(3);
    props[0].id = CORBA::string_dup("FFT_size");
    props[0].value <<= s.FFT_size;
    props[1].id = CORBA::string_dup("TransitionWidth");
    props[1].value <<= s.TransitionWidth;
    props[2].id = CORBA::string_dup("Ripple");
    props[2].value <<= s.Ripple;
    a <<= props;
};

inline bool operator== (const filterProps_struct& s1, const filterProps_struct& s2) {
    if (s1.FFT_size!=s2.FFT_size)
        return false;
    if (s1.TransitionWidth!=s2.TransitionWidth)
        return false;
    if (s1.Ripple!=s2.Ripple)
        return false;
    return true;
};

inline bool operator!= (const filterProps_struct& s1, const filterProps_struct& s2) {
    return !(s1==s2);
};


#endif
