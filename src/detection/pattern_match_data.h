//--------------------------------------------------------------------------
// Copyright (C) 2014-2015 Cisco and/or its affiliates. All rights reserved.
// Copyright (C) 2002-2013 Sourcefire, Inc.
// Copyright (C) 1998-2002 Martin Roesch <roesch@sourcefire.com>
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License Version 2 as published
// by the Free Software Foundation.  You may not use, modify or distribute
// this program under any other version of the GNU General Public License.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//--------------------------------------------------------------------------

#ifndef PATTERN_MATCH_DATA_H
#define PATTERN_MATCH_DATA_H

#include <ctype.h>
#include <sys/time.h>

#include "main/snort_types.h"
#include "detection/treenodes.h"

struct PmdLastCheck
{
    struct timeval ts;
    uint64_t packet_number;
    uint32_t rebuild_flag;
};

struct PatternMatchData
{
    // used by both
    uint8_t negated;        /* search for "not this pattern" */
    uint8_t fp;             /* For fast_pattern arguments */
    uint8_t no_case;        /* Toggle case sensitivity */
    uint8_t relative;       /* do relative pattern searching */

    uint16_t fp_offset;
    uint16_t fp_length;

    int offset;             /* pattern search start offset */
    int depth;              /* pattern search depth */

    unsigned pattern_size;  /* size of app layer pattern */
    char* pattern_buf;      /* app layer pattern to match on */

    // not used by ips_content
    int8_t fp_only;
    uint8_t pm_type;

    unsigned replace_size;  /* size of app layer replace pattern */
    char* replace_buf;      /* app layer pattern to replace with */

    // FIXIT-L wasting some memory here:
    // - this is not used by content option logic directly
    // - and only used on current eval (not across packets)
    // (partly mitigated by only allocating if excpetion_flag is set)
    //
    /* Set if fast pattern matcher found a content in the packet,
       but the rule option specifies a negated content. Only
       applies to negative contents that are not relative */
    PmdLastCheck* last_check;
};

#endif

