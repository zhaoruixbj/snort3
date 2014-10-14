/*
** Copyright (C) 2014 Cisco and/or its affiliates. All rights reserved.
** Copyright (C) 2005-2013 Sourcefire, Inc.
** Copyright (C) 1998-2005 Martin Roesch <roesch@sourcefire.com>
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

/* bo
 *
 * Purpose: Detects Back Orifice traffic by brute forcing the weak encryption
 *          of the program's network protocol and detects the magic cookie
 *          that it's servers and clients require to communicate with each
 *          other.
 *
 * Effect: Analyzes UDP traffic for the BO magic cookie, reports if it finds
 *         traffic matching the profile.
 *
 */

/*
 * The following text describes a couple of ways in which the Back Orifice
 * signature is calculated.  The snort runtime generated an array of 65K
 * possible signatures, of which two are described here.  Refer back to
 * this simplified algorithm when evaluating the snort code below.
 *
 * Back Orifice magic cookie is "*!*QWTY?", which is located in the first
 * eight bytes of the packet.  But it is encrypted using an XOR.  Here we'll
 * generate a sequence of eight bytes which will decrypt (XOR) into the
 * magic cookie.  This test uses the NON-DEFAULT KEY to initialize (seed) the
 * "random" number generator.  The default seed results in the following
 * sequence of bytes:  E4 42 FB 83 41 B3 4A F0.  When XOR'd against the
 * magic cookie, we have our packet data which looks like a Back Orifice
 * signature:
 *
 *   Cookie:  2A 21 2A 51 57 54 59 3F
 *   Random:  E4 42 FB 83 41 B3 4A F0
 *   -------  -- -- -- -- -- -- -- --
 *   Result:  CE 63 D1 D2 16 E7 13 CF  (XOR'd result)
 *
 * For demonstration purposes:
 *
 *   static long holdrand = 1L;
 *   static int LocalBoRand()
 *   {
 *       return(((holdrand = holdrand * 214013L + 2531011L) >> 16) & 0x7fff);
 *   }
 *   ...
 *
 *   int BoRandValues_NonDefaultKey[8];
 *   holdrand = BO_DEFAULT_KEY;    (seed value)
 *   BoRandValues_NonDefaultKey[0] = LocalBoRand() % 256;  --> 228 (0xe4)
 *   BoRandValues_NonDefaultKey[1] = LocalBoRand() % 256;  -->  66 (0x42)
 *   BoRandValues_NonDefaultKey[2] = LocalBoRand() % 256;  --> 251 (0xfb)
 *   BoRandValues_NonDefaultKey[3] = LocalBoRand() % 256;  --> 131 (0x83)
 *   BoRandValues_NonDefaultKey[4] = LocalBoRand() % 256;  -->  65 (0x41)
 *   BoRandValues_NonDefaultKey[5] = LocalBoRand() % 256;  --> 179 (0xb3)
 *   BoRandValues_NonDefaultKey[6] = LocalBoRand() % 256;  -->  74 (0x4a)
 *   BoRandValues_NonDefaultKey[7] = LocalBoRand() % 256;  --> 240 (0xf0)
 *
 *
 * The following test uses the DEFAULT KEY to initialize (seed) the
 * "random" number generator.  The default seed results in the following
 * sequence of bytes:  26 27 F6 85 97 15 AD 1D.  When XOR'd against the
 * magic cookie, we have our packet data which looks like a Back Orifice
 * signature:
 *
 *   Cookie:  2A 21 2A 51 57 54 59 3F
 *   Random:  26 27 F6 85 97 15 AD 1D
 *   -------  -- -- -- -- -- -- -- --
 *   Result:  0C 06 DC D4 C0 41 F4 22  (XOR'd result)
 *
 * For demonstration purposes:
 *
 *   int BoRandValues_DefaultKey[8];
 *   holdrand = 0;    (seed value)
 *   BoRandValues_DefaultKey[0] = LocalBoRand() % 256;  -->  38 (0x26)
 *   BoRandValues_DefaultKey[1] = LocalBoRand() % 256;  -->  39 (0x27)
 *   BoRandValues_DefaultKey[2] = LocalBoRand() % 256;  --> 246 (0xf6)
 *   BoRandValues_DefaultKey[3] = LocalBoRand() % 256;  --> 133 (0x85)
 *   BoRandValues_DefaultKey[4] = LocalBoRand() % 256;  --> 151 (0x97)
 *   BoRandValues_DefaultKey[5] = LocalBoRand() % 256;  -->  21 (0x15)
 *   BoRandValues_DefaultKey[6] = LocalBoRand() % 256;  --> 173 (0xad)
 *   BoRandValues_DefaultKey[7] = LocalBoRand() % 256;  -->  29 (0x1d)
 *
 * Notes:
 *
 *   10/13/2005 marc norton - This has a lot of changes  to the runtime
 *   decoding and testing.  The '% 256' op was removed,
 *   the xor op is bit wise so modulo is not needed,
 *   the char casting truncates to one byte,
 *   and len testing has been modified as was the xor decode copy and
 *   final PONG test.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <sys/types.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "snort.h"
#include "snort_types.h"
#include "detect.h"
#include "protocols/packet.h"
#include "event.h"
#include "parser.h"
#include "snort_debug.h"
#include "mstring.h"
#include "util.h"
#include "profiler.h"
#include "snort_types.h"
#include "framework/inspector.h"
#include "framework/module.h"
#include "protocols/udp.h"

#define BO_DEFAULT_KEY     31337
#define BO_MAGIC_SIZE      8
#define BO_MIN_SIZE        18
#define BO_DEFAULT_PORT    31337

#define BO_TYPE_PING       1
#define BO_FROM_UNKNOWN    0
#define BO_FROM_CLIENT     1
#define BO_FROM_SERVER     2

#define BO_BUF_SIZE         8
#define BO_BUF_ATTACK_SIZE  1024

#define s_name "back_orifice"

#define s_help \
    "back orifice detection"

/* global keyvalue for the BoRand() function */
static THREAD_LOCAL long holdrand = 1L;

// these are const during runtime
static uint16_t lookup1[65536][3];
static uint16_t lookup2[65536];

static THREAD_LOCAL ProfileStats boPerfStats;
static THREAD_LOCAL SimpleStats bostats;

//-------------------------------------------------------------------------
// bo module
//-------------------------------------------------------------------------

#define GID_BO  105

#define BO_TRAFFIC_DETECT         1
#define BO_CLIENT_TRAFFIC_DETECT  2
#define BO_SERVER_TRAFFIC_DETECT  3
#define BO_SNORT_BUFFER_ATTACK    4

#define BO_TRAFFIC_DETECT_STR \
    "BO traffic detected"
#define BO_CLIENT_TRAFFIC_DETECT_STR \
    "BO client traffic detected"
#define BO_SERVER_TRAFFIC_DETECT_STR \
    "BO server traffic detected"
#define BO_SNORT_BUFFER_ATTACK_STR \
    "BO Snort buffer attack"

static const RuleMap bo_rules[] =
{
    { BO_TRAFFIC_DETECT, BO_TRAFFIC_DETECT_STR },
    { BO_CLIENT_TRAFFIC_DETECT, BO_CLIENT_TRAFFIC_DETECT_STR },
    { BO_SERVER_TRAFFIC_DETECT, BO_SERVER_TRAFFIC_DETECT_STR },
    { BO_SNORT_BUFFER_ATTACK, BO_SNORT_BUFFER_ATTACK_STR },

    { 0, nullptr }
};

class BoModule : public Module
{
public:
    BoModule() : Module(s_name, s_help)
    { };

    const RuleMap* get_rules() const
    { return bo_rules; };

    unsigned get_gid() const
    { return GID_BO; };

    const char** get_pegs() const;
    PegCount* get_counts() const;
    ProfileStats* get_profile() const;
};

const char** BoModule::get_pegs() const
{ return simple_pegs; }

PegCount* BoModule::get_counts() const
{ return (PegCount*)&bostats; }

ProfileStats* BoModule::get_profile() const
{ return &boPerfStats; }

//-------------------------------------------------------------------------
// bo implementation
//-------------------------------------------------------------------------

/*
 * Function: BoRand()
 *
 * Purpose: Back Orifice "encryption" algorithm
 *
 * Arguments: None.
 *
 * Returns: key to XOR with current char to be "encrypted"
 */
static char BoRand(void)
{
    holdrand = holdrand * 214013L + 2531011L;
    return (char) (((holdrand  >> 16) & 0x7fff) & 0xFF);
}

/*
 * Precalculate the known cyphertext into a prefix and suffix lookup table
 * to recover the key.  Using this in the bo_eval() function below is much
 * faster than the old brute force method
 */
static void PrecalcPrefix(void)
{
    uint8_t cookie_cyphertext[BO_MAGIC_SIZE];
    const char *cookie_plaintext = "*!*QWTY?";
    int key;
    int cookie_index;
    const char *cp_ptr;       /* cookie plaintext indexing pointer */
    uint16_t cyphertext_referent;

    memset(lookup1, 0, sizeof(lookup1));
    memset(lookup2, 0, sizeof(lookup2));

    for(key=0;key<65536;key++)
    {
        /* setup to generate cyphertext for this key */
        holdrand = key;
        cp_ptr = cookie_plaintext;

        /* convert the plaintext cookie to cyphertext for this key */
        for(cookie_index=0;cookie_index<BO_MAGIC_SIZE;cookie_index++)
        {
            cookie_cyphertext[cookie_index] =(uint8_t)(*cp_ptr^(BoRand()));
            cp_ptr++;
        }

        /*
         * generate the key lookup mechanism from the first 2 characters of
         * the cyphertext
         */
        cyphertext_referent = (uint16_t) (cookie_cyphertext[0] << 8) & 0xFF00;
        cyphertext_referent |= (uint16_t) (cookie_cyphertext[1]) & 0x00FF;

        /* if there are any keyspace collisions that's going to suck */
        if(lookup1[cyphertext_referent][0] != 0)
        {
            if(lookup1[cyphertext_referent][1] != 0)
            {
                lookup1[cyphertext_referent][2] = (uint16_t)key;
            }
            else
            {
                lookup1[cyphertext_referent][1] = (uint16_t)key;
            }
        }
        else
        {
            lookup1[cyphertext_referent][0] = (uint16_t)key;
        }

        /*
         * generate the second lookup from the last two characters of
         * the cyphertext
         */
        cyphertext_referent = (uint16_t) (cookie_cyphertext[6] << 8) & 0xFF00;
        cyphertext_referent |= (uint16_t) (cookie_cyphertext[7]) & 0x00FF;

        /*
         * set the second lookup with the current key
         */
        lookup2[key] = cyphertext_referent;
    }
}

/*
 * Purpose: Attempt to guess the direction this packet is going in.
 *
 * Arguments: p        => pointer to the current packet data struct
 *            pkt_data => pointer to data after magic cookie
 *
 * Returns: BO_FROM_UNKNOWN  if direction unknown
 *          BO_FROM_CLIENT   if direction from client to server
 *          BO_FROM_SERVER   if direction from server to client
 *
 * Reference: http://www.magnux.org/~flaviovs/boproto.html
 *    BO header structure:
 *      Mnemonic    Size in bytes
 *      -------------------------
 *      MAGIC       8
 *      LEN         4
 *      ID          4
 *      T           1
 *      DATA        variable
 *      CRC         1
 *
 */
static int BoGetDirection(Packet *p, char *pkt_data)
{
    uint32_t len = 0;
    uint32_t id = 0;
    uint32_t l, i;
    char type;
    static THREAD_LOCAL char buf1[BO_BUF_SIZE];
    char plaintext;

    /* Check for the default port on either side */
    if ( p->ptrs.dp == BO_DEFAULT_PORT )
    {
        return BO_FROM_CLIENT;
    }
    else if ( p->ptrs.sp == BO_DEFAULT_PORT )
    {
        return BO_FROM_SERVER;
    }

    /* Didn't find default port, so look for ping packet */

    /* Get length from BO header - 32 bit int */
    for ( i = 0; i < 4; i++ )
    {
        plaintext = (char) (*pkt_data ^ BoRand());
        l = (uint32_t) plaintext;
        len += l << (8*i);
        pkt_data++;
    }

    /* Get ID from BO header - 32 bit int */
    for ( i = 0; i < 4; i++ )
    {
        plaintext = (char) (*pkt_data ^ BoRand() );
        l = ((uint32_t) plaintext) & 0x000000FF;
        id += l << (8*i);
        pkt_data++;
    }

    DEBUG_WRAP(DebugMessage(DEBUG_PLUGIN, "Data length = %lu\n", len););
    DEBUG_WRAP(DebugMessage(DEBUG_PLUGIN, "ID = %lu\n", id););

    /* Do more len checking */

    if ( len >= BO_BUF_ATTACK_SIZE )
    {
        SnortEventqAdd(GID_BO, BO_SNORT_BUFFER_ATTACK);
        return BO_FROM_UNKNOWN;
    }

    /* Adjust for BO packet header length */
    if (len <= BO_MIN_SIZE)
    {
        /* Need some data, or we can't figure out client or server */
        return BO_FROM_UNKNOWN;
    }
    else
    {
        len -= BO_MIN_SIZE;
    }

    if( len > 7 )
    {
        len = 7; /* we need no more than  7 variable chars */
    }

    /* Continue parsing BO header */
    type = (char) (*pkt_data ^ BoRand());
    pkt_data++;

    /* check to make sure we don't run off end of packet */
    if ((uint32_t)(p->dsize - ((uint8_t *)pkt_data - p->data)) < len)
    {
        /* We don't have enough data to inspect */
        return BO_FROM_UNKNOWN;
    }

    if ( type & 0x80 )
    {
        DEBUG_WRAP(DebugMessage(DEBUG_PLUGIN, "Partial packet\n"););
    }
    if ( type & 0x40 )
    {
        DEBUG_WRAP(DebugMessage(DEBUG_PLUGIN, "Continued packet\n"););
    }

    /* Extract type of BO packet */
    type = type & 0x3F;

    DEBUG_WRAP(DebugMessage(DEBUG_PLUGIN, "Type = 0x%x\n", type););

    /* Only examine data if this is a ping request or response */
    if ( type == BO_TYPE_PING )
    {
        if ( len < 7 )
        {
            return BO_FROM_CLIENT;
        }

        for(i=0;i<len;i++ ) /* start at 0 to advance the BoRand() function properly */
        {
            buf1[i] = (char) (pkt_data[i] ^ BoRand());
            if ( buf1[i] == 0 )
            {
                return BO_FROM_UNKNOWN;
            }
        }

        if( ( buf1[3] == 'P' || buf1[3] == 'p' ) &&
            ( buf1[4] == 'O' || buf1[4] == 'o' ) &&
            ( buf1[5] == 'N' || buf1[5] == 'n' ) &&
            ( buf1[6] == 'G' || buf1[6] == 'g' ) )
        {
            return BO_FROM_SERVER;
        }
        else
        {
            return BO_FROM_CLIENT;
        }
    }

    return BO_FROM_UNKNOWN;
}

//-------------------------------------------------------------------------
// class stuff
//-------------------------------------------------------------------------

class BackOrifice : public Inspector {
public:
    BackOrifice() { };

    void show(SnortConfig*);
    void eval(Packet*);
};

void BackOrifice::show(SnortConfig*)
{
    LogMessage("%s\n", s_name);
}

void BackOrifice::eval(Packet *p)
{
    uint16_t cyphertext_referent;
    uint16_t cyphertext_suffix;
    uint16_t key;
    const char *magic_cookie = "*!*QWTY?";
    char *pkt_data;
    const char *magic_data;
    char *end;
    char plaintext;
    int i;
    int bo_direction = 0;
    PROFILE_VARS;

    // preconditions - what we registered for
    assert(p->is_udp());

    /* make sure it's at least 19 bytes long */
    if(p->dsize < BO_MIN_SIZE)
    {
        return;
    }

    MODULE_PROFILE_START(boPerfStats);
    ++bostats.total_packets;

    /*
     * take the first two characters of the packet and generate the
     * first reference that gives us a reference key
     */
    cyphertext_referent = (uint16_t) (p->data[0] << 8) & 0xFF00;
    cyphertext_referent |= (uint16_t) (p->data[1]) & 0x00FF;

    /*
     * generate the second referent from the last two characters
     * of the cyphertext
     */
    cyphertext_suffix = (uint16_t) (p->data[6] << 8) & 0xFF00;
    cyphertext_suffix |= (uint16_t) (p->data[7]) & 0x00FF;

    for(i=0;i<3;i++)
    {
        /* get the key from the cyphertext */
        key = lookup1[cyphertext_referent][i];

        /*
         * if the lookup from the proposed key matches the cyphertext reference
         * then we've probably go the right key and can proceed to full
         * decryption using the key
         *
         * moral of the story: don't use a lame keyspace
         */
        if(lookup2[key] == cyphertext_suffix)
        {
            holdrand = key;
            pkt_data = (char*)p->data;
            end = (char*)p->data + BO_MAGIC_SIZE;
            magic_data = magic_cookie;

            while(pkt_data<end)
            {
                plaintext = (char) (*pkt_data ^ BoRand());

                if(*magic_data != plaintext)
                {
                    DEBUG_WRAP(DebugMessage(DEBUG_PLUGIN,
                            "Failed check one on 0x%X : 0x%X\n",
                            *magic_data, plaintext););
                    MODULE_PROFILE_END(boPerfStats);
                    return;
                }

                magic_data++;
                pkt_data++;
            }

            /* if we fall thru there's a detect */
            DEBUG_WRAP(DebugMessage(DEBUG_PLUGIN,
                        "Detected Back Orifice Data!\n");
            DebugMessage(DEBUG_PLUGIN, "hash value: %d\n", key););

            bo_direction = BoGetDirection(p, pkt_data);

            if ( bo_direction == BO_FROM_CLIENT )
            {
                SnortEventqAdd(GID_BO, BO_CLIENT_TRAFFIC_DETECT);
                DEBUG_WRAP(DebugMessage(DEBUG_PLUGIN, "Client packet\n"););
            }
            else if ( bo_direction == BO_FROM_SERVER )
            {
                SnortEventqAdd(GID_BO, BO_SERVER_TRAFFIC_DETECT);
                DEBUG_WRAP(DebugMessage(DEBUG_PLUGIN, "Server packet\n"););
            }
            else
            {
                SnortEventqAdd(GID_BO, BO_TRAFFIC_DETECT);
            }
        }
    }

    MODULE_PROFILE_END(boPerfStats);

    return;
}

//-------------------------------------------------------------------------
// api stuff
//-------------------------------------------------------------------------

static Module* mod_ctor()
{ return new BoModule; }

static void mod_dtor(Module* m)
{ delete m; }

void bo_init()
{
    PrecalcPrefix();
}

static Inspector* bo_ctor(Module*)
{
    return new BackOrifice;
}

static void bo_dtor(Inspector* p)
{
    delete p;
}

static const InspectApi bo_api =
{
    {
        PT_INSPECTOR,
        s_name,
        s_help,
        INSAPI_PLUGIN_V0,
        0,
        mod_ctor,
        mod_dtor
    },
    IT_PROTOCOL, 
    (uint16_t)PktType::UDP,
    nullptr, // buffers
    nullptr, // service
    bo_init,
    nullptr, // pterm
    nullptr, // tinit
    nullptr, // tterm
    bo_ctor,
    bo_dtor,
    nullptr, // ssn
    nullptr  // reset
};

#ifdef BUILDING_SO
SO_PUBLIC const BaseApi* snort_plugins[] =
{
    &bo_api.base,
    nullptr
};
#else
const BaseApi* sin_bo = &bo_api.base;
#endif

