/*
** Copyright (C) 2014 Cisco and/or its affiliates. All rights reserved.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
// ips_tag.cc author Russ Combs <rucombs@cisco.com>
// FIXIT-L add TagOption::eval() instead of special case

#include <sys/types.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "snort_types.h"
#include "detection/treenodes.h"
#include "snort_debug.h"
#include "snort.h"
#include "detection/detection_defines.h"
#include "detection/tag.h"
#include "framework/ips_option.h"
#include "framework/parameter.h"
#include "framework/module.h"

#define s_name "tag"

//-------------------------------------------------------------------------
// module
//-------------------------------------------------------------------------

static const Parameter s_params[] =
{
    { "~", Parameter::PT_ENUM, "session|host_src|host_dst", nullptr,
      "log all packets in session or all packets to or from host" },

    { "packets", Parameter::PT_INT, "1:", nullptr,
      "tag this many packets" },

    { "seconds", Parameter::PT_INT, "1:", nullptr,
      "tag for this many seconds" },

    { "bytes", Parameter::PT_INT, "1:", nullptr,
      "tag for this many bytes" },

    { nullptr, Parameter::PT_MAX, nullptr, nullptr, nullptr }
};

#define s_help \
    "rule option to log additional packets"

class TagModule : public Module
{
public:
    TagModule() : Module(s_name, s_help, s_params)
    { tag = nullptr; };

    bool set(const char*, Value&, SnortConfig*);
    bool begin(const char*, int, SnortConfig*);
    bool end(const char*, int, SnortConfig*);

    TagData* get_data();
    TagData* tag;
};

TagData* TagModule::get_data()
{
    TagData* tmp = tag;
    tag = nullptr;
    return tmp;
}

bool TagModule::begin(const char*, int, SnortConfig*)
{
    if ( !tag )
        tag = (TagData*)SnortAlloc(sizeof(*tag));

    return true;
}

bool TagModule::end(const char*, int, SnortConfig*)
{
    if ( !tag->tag_metric )
        tag->tag_metric = TAG_METRIC_UNLIMITED;

    return true;
}

// FIXIT-L error if named option is set multiple times (general problem)
// eg: tag:session, packets 10, packets 20;
bool TagModule::set(const char*, Value& v, SnortConfig*)
{
    if ( v.is("~") )
    {
        if ( v == "session" )
            tag->tag_type = TAG_SESSION;

        else if ( v == "host_src" )
        {
            tag->tag_type = TAG_HOST;
            tag->tag_direction = TAG_HOST_SRC;
        }
        else
        {
            tag->tag_type = TAG_HOST;
            tag->tag_direction = TAG_HOST_DST;
        }
    }
    else if ( v.is("packets") )
    {
        tag->tag_metric |= TAG_METRIC_PACKETS;
        tag->tag_packets = v.get_long();
    }
    else if ( v.is("seconds") )
    {
        tag->tag_metric |= TAG_METRIC_SECONDS;
        tag->tag_seconds = v.get_long();
    }
    else if ( v.is("bytes") )
    {
        tag->tag_metric |= TAG_METRIC_BYTES;
        tag->tag_bytes = v.get_long();
    }
    else
        return false;

    return true;
}

//-------------------------------------------------------------------------
// api methods
//-------------------------------------------------------------------------

static Module* mod_ctor()
{
    return new TagModule;
}

static void mod_dtor(Module* m)
{
    delete m;
}

static IpsOption* tag_ctor(Module* p, OptTreeNode* otn)
{
    TagModule* m = (TagModule*)p;
    otn->tag = m->get_data();
    return nullptr;
}

static const IpsApi tag_api =
{
    {
        PT_IPS_OPTION,
        s_name,
        s_help,
        IPSAPI_PLUGIN_V0,
        0,
        mod_ctor,
        mod_dtor
    },
    OPT_TYPE_META,
    1, PROTO_BIT__NONE,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    tag_ctor,
    nullptr,
    nullptr
};

#ifdef BUILDING_SO
SO_PUBLIC const BaseApi* snort_plugins[] =
{
    &tag_api.base,
    nullptr
};
#else
const BaseApi* ips_tag = &tag_api.base;
#endif

